module hud_line_buffer #(
    parameter int unsigned BurstBytes = memory_pkg::MaxMessageLength,
    parameter int unsigned LineBytes  = mem_map_pkg::ScreenWidth,
    // Must be a power of 2
    parameter int unsigned MaxActiveSprites = 64 // to avoid overworking
) (
    input logic clk,
    input logic reset,

    input hud_pkg::sprite_inst_entry_t sprite_entry,
    output hud_pkg::sprite_mem_id lookup_mem_id,
    input logic [31:0] lookup_mem_addr,

    input logic pulse_frame_restart,
    input logic pulse_line_start,
    input mem_map_pkg::fb_y_t line_y,
    output logic row_ready,

    input mem_map_pkg::fb_x_t px_x,
    output logic px_opaque,
    output color_pkg::hud_color_t px_color,

    output hud_pkg::sprite_lut_id sprite_read_id,
    output logic mem_toggle,
    output memory_pkg::transaction_e mem_transaction,
    output memory_pkg::address_t mem_address,
    output memory_pkg::message_length_t mem_length,
    input logic mem_busy,
    input logic mem_data_on_cable,
    input logic [7:0] mem_read_data,
    output logic shortlist_overflow // if more than 64 sprites are active on screen, we lose some.
);

  typedef enum logic [2:0] {
    SCAN,      // Cycle all sprites in the LUT and check if active. Add to shortlist if so.
    SCAN_DRAIN, // Wait for delayed RAM response for last LUT.
    SCAN_DRAIN2, // [TIMING] LUT read latency is 2 cycles, so the drain takes two.
    IDLE,      // Wait for row_commit from simulator.
    ROW_CLEAR, // Blank all bits in px_opaque_store
    ROW_PICK,  // Choose next row to check for HUD elements.
    ROW_ADDR,  // [TIMING] Work out the sprite row's memory address from the registers set in ROW_PICK.
    ROW_FETCH  // Burst pixels from chosen sprite and write to px_opaque_store and px_color_store.
  } state_e;

  state_e state;

  localparam int unsigned PageBytes = mem_map_pkg::PageBytes;  // 1024
  localparam int unsigned XBits = $clog2(LineBytes);  // 10
  localparam int unsigned ListBits = $clog2(MaxActiveSprites + 1);  // 7

  // addresses ListBits
  localparam int unsigned IdxBits = $clog2(MaxActiveSprites);  // 6
  localparam int unsigned ByteCountBits = $clog2(2 * LineBytes + 1);  // 11

  localparam logic [15:0] SeeThroughColor = 16'hF81F;

  // Line buffer: 640-bit vector for which pixels to render from HUD
  logic [LineBytes-1:0] px_opaque_store;
  color_pkg::hud_color_t px_color_store[LineBytes];

  assign px_opaque = px_opaque_store[px_x];
  assign px_color  = px_color_store[px_x];

  // Holds up to 64 LUT indexes
  logic [hud_pkg::LutBits-1:0] shortlist[MaxActiveSprites];
  logic [ListBits-1:0] shortlist_len;

  // When scanning the LUT for active sprites, track index of sprite we're on
  logic [hud_pkg::LutBits-1:0] scan_i;

  // Index in shortlist for the current sprite we're working on
  logic [ListBits-1:0] shortlist_i;

  hud_pkg::sprite_inst_entry_t current_sprite;
  assign sprite_read_id = state == SCAN ? scan_i : shortlist[IdxBits'(shortlist_i)];
  assign current_sprite = sprite_entry;

  logic sprite_entry_valid;
  assign sprite_entry_valid = sprite_read_id == current_sprite.id;

  logic scan_rd_q;
  logic scan_rd_q2;  // [TIMING] scan_rd_q delayed to match the 2-cycle LUT read
  assign lookup_mem_id = current_sprite.mem_id;

  logic rows_overlap, visible_on_row;
  // [TIMING] 17 bits instead of 32: every operand is at most 16 bits, so every sum and
  // compare below is exact in 17 bits and the carry chains are half as long.
  logic [16:0] span_x0, span_x1, row_in_sprite, x_end, y_end;

  // Find out if sprite needs to be rendered on this row, and if so, which part of it.
  always_comb begin
    row_in_sprite = 17'(line_y) - 17'(current_sprite.y);
    y_end = 17'(current_sprite.y) + 17'(current_sprite.h);
    rows_overlap = (17'(line_y) >= 17'(current_sprite.y)) && (17'(line_y) < y_end);
    span_x0 = 17'(current_sprite.x);
    x_end = 17'(current_sprite.x) + 17'(current_sprite.w);
    span_x1 = (x_end > 17'(LineBytes)) ? 17'(LineBytes) : x_end;
    visible_on_row = current_sprite.active && rows_overlap
      && (span_x0 < LineBytes) && (span_x1 > span_x0);
  end

  logic [31:0] sprite_row_addr;
  logic [ByteCountBits-1:0] span_bytes;
  logic [ByteCountBits-1:0] bytes_req, bytes_recv;
  logic [XBits-1:0] dest_x;
  logic high_byte_seen;
  logic [7:0] pixel_hi;

  // [TIMING] ROW_PICK used to do BRAM read -> (line_y - y) -> 32x32 multiply -> + lookup_mem_addr
  // in one cycle (-3.3 ns). ROW_PICK now only registers the operands; ROW_ADDR does the
  // multiply-add from registers. The row inside the sprite is < h (16 bits) and w is 16 bits,
  // so a 16x16 multiply (one DSP) gives the same 32-bit result as the old 32x32 one.
  logic [15:0] row_in_sprite_q, sprite_w_q;
  logic [31:0] base_addr_q;

  logic [31:0] next_addr;
  assign next_addr = sprite_row_addr + 32'(bytes_req);
  logic [31:0] to_page_end, bytes_remaining;
  memory_pkg::message_length_t burst_len;

  // Setup memory transaction length
  always_comb begin
    to_page_end = PageBytes - (next_addr % PageBytes);
    bytes_remaining = 32'(span_bytes) - 32'(bytes_req);
    burst_len = memory_pkg::message_length_t'(BurstBytes);
    if (bytes_remaining < 32'(burst_len))
      burst_len = memory_pkg::message_length_t'(bytes_remaining);
    if (to_page_end < 32'(burst_len)) burst_len = memory_pkg::message_length_t'(to_page_end);
  end

  assign mem_transaction = memory_pkg::READ;


  logic commit_pending;  // a commit arrived while we were not IDLE

  always_ff @(posedge clk) begin
    if (reset) begin
      state <= SCAN;
      scan_i <= '0;
      shortlist_i <= '0;
      shortlist_len <= '0;
      shortlist_overflow <= 1'b0;
      row_ready <= 1'b0;
      commit_pending <= 1'b0;
      px_opaque_store <= '0;
      mem_toggle <= 1'b0;
      mem_address <= '0;
      mem_length <= '0;
      sprite_row_addr <= '0;
      row_in_sprite_q <= '0;
      sprite_w_q <= '0;
      base_addr_q <= '0;
      span_bytes <= '0;
      bytes_req <= '0;
      bytes_recv <= '0;
      dest_x <= '0;
      high_byte_seen <= 1'b0;
      pixel_hi <= '0;
      scan_rd_q <= 1'b0;
      scan_rd_q2 <= 1'b0;
    end else begin
      scan_rd_q <= (state == SCAN) && !pulse_frame_restart;
      scan_rd_q2 <= scan_rd_q && !pulse_frame_restart;

      if (pulse_line_start) begin
        row_ready <= 1'b0;
        if (state != IDLE) commit_pending <= 1'b1;
      end

      if (pulse_frame_restart) begin
        // Reset state machine
        state <= SCAN;
        row_ready <= 1'b0;
        commit_pending <= 1'b1;
        shortlist_i <= '0;
        bytes_req <= '0;
        bytes_recv <= '0;
        scan_i <= '0;
        shortlist_len <= '0;
        shortlist_overflow <= 1'b0;
      end else begin
        case (state)
          SCAN, SCAN_DRAIN, SCAN_DRAIN2: begin
            // Find active sprites and add them to the shortlist
            if (current_sprite.active && scan_rd_q2) begin
              if (32'(shortlist_len) < MaxActiveSprites) begin
                shortlist[IdxBits'(shortlist_len)] <= current_sprite.id[hud_pkg::LutBits-1:0];
                shortlist_len <= shortlist_len + 1'b1;
              end else begin
                shortlist_overflow <= 1'b1;
              end
            end

            if (state == SCAN_DRAIN2) begin
              state <= IDLE;
            end else if (state == SCAN_DRAIN) begin
              state <= SCAN_DRAIN2;
            end
            else if (32'(scan_i) == hud_pkg::SpriteLutDepth - 1) begin
              scan_i <= '0;
              state  <= SCAN_DRAIN;
            end else begin
              scan_i <= scan_i + 1'b1;
            end
          end

          IDLE: begin
            if (pulse_line_start || commit_pending) begin
              commit_pending <= 1'b0;
              row_ready <= 1'b0;
              state <= ROW_CLEAR;
            end
          end

          ROW_CLEAR: begin
            px_opaque_store <= '0;
            shortlist_i <= '0;
            state <= ROW_PICK;
          end

          ROW_PICK: begin
            if (32'(shortlist_i) >= 32'(shortlist_len)) begin
              // Row done
              row_ready <= 1'b1;
              state <= IDLE;
            end else if (visible_on_row && sprite_entry_valid) begin
              // Sprite shows up on this row
              row_in_sprite_q <= row_in_sprite[15:0];
              sprite_w_q <= current_sprite.w;
              base_addr_q <= lookup_mem_addr;
              span_bytes <= ByteCountBits'((span_x1 - span_x0) * 2);
              bytes_req <= '0;
              bytes_recv <= '0;
              dest_x <= XBits'(span_x0);
              high_byte_seen <= 1'b0;
              state <= ROW_ADDR;
            end else if(sprite_entry_valid) begin
              // Not on this row, go to next sprite
              shortlist_i <= shortlist_i + 1'b1;
            end
          end

          ROW_ADDR: begin
            sprite_row_addr <= base_addr_q + ((32'(row_in_sprite_q) * 32'(sprite_w_q)) << 1);
            state <= ROW_FETCH;
          end

          ROW_FETCH: begin
            if (mem_data_on_cable) begin
              bytes_recv <= bytes_recv + 1'b1;
              high_byte_seen <= ~high_byte_seen;

              // A pixel is two bytes, so we assign the first byte to pixel_hi, second byte is mem_read_data next cycle
              if (!high_byte_seen) begin
                pixel_hi <= mem_read_data;
              end else begin
                if ({pixel_hi, mem_read_data} != SeeThroughColor) begin
                  px_opaque_store[dest_x] <= 1'b1;
                  px_color_store[dest_x]
                    <= color_pkg::rgb565_to_hud_color({pixel_hi, mem_read_data});
                end
                dest_x <= dest_x + 1'b1;
              end
            end

            if (!mem_busy && bytes_recv == bytes_req && 32'(bytes_req) < 32'(span_bytes)) begin
              // Start next transaction if needed
              mem_address <= memory_pkg::address_t'(next_addr);
              mem_length <= burst_len;
              mem_toggle <= ~mem_toggle;
              bytes_req <= bytes_req + ByteCountBits'(burst_len);
            end

            if (32'(bytes_recv) >= 32'(span_bytes) && !mem_busy) begin
              // Go to next sprite
              shortlist_i <= shortlist_i + 1'b1;
              state  <= ROW_PICK;
            end
          end

          default: state <= SCAN;
        endcase
      end
    end
  end

endmodule
