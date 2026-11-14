module fb_compositor #(
    parameter int unsigned BurstBytes = memory_pkg::MaxMessageLength,
    parameter int unsigned LineBytes  = mem_map_pkg::GridWidth
) (
    input logic clk,
    input logic reset,

    // Simulation layer. sim_buffers pulses pulse_row_commit when a finished row is in res_buffer[commit_res_buffer].;
    input logic pulse_row_commit,
    input mem_map_pkg::fb_y_t commit_y,
    input logic commit_res_buffer,
    input logic pulse_frame_restart,
    input sim_pkg::row_buffer_t res_buffer[2],

    // HUD layer. Combinational lookup for the pixel currently being emitted;
    // an opaque HUD pixel replaces the simulation one.
    output mem_map_pkg::fb_x_t hud_x,
    output mem_map_pkg::fb_y_t hud_y,
    input logic hud_opaque,
    input color_pkg::hud_color_t hud_color,
    input logic hud_row_ready,

    // Fb wires
    input logic fb_sel,
    output logic frame_written,

    // High only at a frame boundary
    output logic frame_idle,
    output logic row_idle,

    // Memory client ports
    output logic mem_toggle,
    output memory_pkg::transaction_e mem_transaction,
    output memory_pkg::address_t mem_address,
    output memory_pkg::message_length_t mem_length,
    output logic [7:0] mem_write_data,
    input logic mem_busy,
    input logic mem_data_on_cable,
    output logic overrun
);

  localparam int unsigned GridHeight = mem_map_pkg::GridHeight;  // 360
  localparam int unsigned RowBits = LineBytes * 8;  // 1280
  localparam int unsigned ScreenBits = mem_map_pkg::ScreenWidth * 8;  // 1280
  localparam int unsigned CountBits = $clog2(ScreenBits + 1);  // 11
  localparam int unsigned RowCountBits = $clog2(GridHeight + 1);  // 9

  // The row being written out to a fb, and to which fb.
  logic [RowBits-1:0] row_shift;
  logic [RowBits-1:0] res_copy;
  mem_map_pkg::fb_y_t row_y;
  logic row_writing_active;
  logic wr_buffer; // which fb to write to

  logic [CountBits-1:0] bytes_requested, bytes_moved;
  logic [RowCountBits-1:0] rows_written;
  logic restart_pending; // sim restarted while compositor was busy

  logic [CountBits-1:0] remaining;
  assign remaining = CountBits'(mem_map_pkg::ScreenWidth) - bytes_requested;

  memory_pkg::message_length_t burst_length;
  always_comb begin
    if (32'(remaining) > BurstBytes) burst_length = memory_pkg::message_length_t'(BurstBytes);
    else burst_length = memory_pkg::message_length_t'(remaining);
  end

  // Compositing sim with hud
  assign mem_transaction = memory_pkg::WRITE;
  assign hud_x = mem_map_pkg::fb_x_t'(bytes_moved);
  assign hud_y = row_y;
  assign mem_write_data = hud_opaque ? hud_color : row_shift[7:0]; // Data to write each cycle

  logic can_accept_commit;
  assign can_accept_commit = !row_writing_active; // Accept new row from sim if idle

  logic first_row_of_frame;
  assign first_row_of_frame = (rows_written == '0);
  assign row_idle   = !row_writing_active;
  assign frame_idle = !row_writing_active && first_row_of_frame;

  // A row finished being written out.
  logic advance_row;
  assign advance_row = row_writing_active
    && (32'(bytes_moved) >= mem_map_pkg::ScreenWidth)
    && !mem_busy && rowStride == 2'd3 ;

  // How many times should a row_shift pixel be written before the next one is evaluated.
  logic [1:0] colStride = 2'b0;
  logic [1:0] rowStride = 2'b0;

  always_ff @(posedge clk) begin
    frame_written <= 1'b0;
    overrun <= 1'b0;

    if (reset) begin
      row_shift <= '0;
      row_y <= '0;
      row_writing_active <= 1'b0;
      wr_buffer <= 1'b1;
      bytes_requested <= '0;
      bytes_moved <= '0;
      rows_written <= '0;
      restart_pending <= 1'b0;
      mem_toggle <= 1'b0;
      mem_address <= '0;
      mem_length <= '0;
    end else begin
      if (pulse_frame_restart) begin
        if (row_writing_active) restart_pending <= 1'b1;
        else rows_written <= '0;
      end

      if (pulse_row_commit) begin
        if (!can_accept_commit) begin
          overrun <= 1'b1;
        end else begin
          // Set which fb to write to.
          if (first_row_of_frame) wr_buffer <= ~fb_sel;

          // Load in the row to be written to the fb in a makeshift shift-register.
          row_shift <= res_buffer[commit_res_buffer][RowBits-1:0];
          res_copy <= res_buffer[commit_res_buffer][RowBits-1:0];
          row_y <= commit_y * sim_pkg::GridScale;
          bytes_requested <= '0;
          bytes_moved <= '0;
          row_writing_active <= 1'b1;
        end
      end else if (row_writing_active) begin
        if (mem_data_on_cable) begin
          if(colStride == 2'd3) begin
            row_shift   <= {8'h00, row_shift[RowBits-1:8]};  // Shift one byte
          end
          bytes_moved <= bytes_moved + 1'b1;
          colStride <= colStride + 1'd1;
        end

        // Initiate next burst
        if (hud_row_ready && !mem_busy && bytes_moved == bytes_requested
            && 32'(bytes_requested) < mem_map_pkg::ScreenWidth) begin
          mem_address <= mem_map_pkg::fb_line_addr(wr_buffer, row_y)
              + memory_pkg::address_t'(bytes_requested);
          mem_length <= burst_length;
          mem_toggle <= ~mem_toggle;
          bytes_requested <= bytes_requested + CountBits'(burst_length);
        end

        // Done!
        if (32'(bytes_moved) >= mem_map_pkg::ScreenWidth && !mem_busy) begin
          if (rowStride >= 2'd3) begin
            row_writing_active <= 1'b0;
          end else begin
            row_y <= row_y + 1'b1;
            bytes_requested <= '0;
            bytes_moved <= '0;
            row_shift <= res_copy;
          end
          rowStride <= rowStride + 1'b1;
        end
      end

      if (advance_row) begin
        if (32'(rows_written) + 1 >= GridHeight / sim_pkg::GridScale) begin
          rows_written    <= '0;
          restart_pending <= 1'b0;
          frame_written   <= 1'b1;
        end else if (restart_pending) begin
          // If we ever are overrun, we need a restart of that frame.
          rows_written    <= '0;
          restart_pending <= 1'b0;
        end else begin
          rows_written <= rows_written + 1'b1;
        end
      end
    end
  end

endmodule
