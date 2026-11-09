module sim_buffers #(
    parameter int unsigned BurstBytes = memory_pkg::MaxMessageLength
) (
    input logic clk,
    input logic reset,
    input logic sim_running,
    input logic fb_ready,

    output logic pulse_new_row,
    output sim_pkg::row_buffer_t buffers[3],

    input logic pulse_transaction_pair_done, // pulse_transaction_pair_done: save res_row to memory
    input logic pulse_row_done, // pulse_row_done: fetch new row from memory
    input sim_pkg::row_buffer_t res_buffer[2],

    // Row-commit handshake for the framebuffer compositor.
    output logic pulse_row_commit,
    output mem_map_pkg::fb_y_t commit_y,
    output logic commit_res_buffer,
    output logic pulse_frame_restart,

    output logic mem_toggle,
    output memory_pkg::transaction_e mem_transaction,
    output memory_pkg::address_t mem_address,
    output memory_pkg::message_length_t mem_length,
    output logic [7:0] mem_write_data,
    input logic mem_busy,
    input logic mem_data_on_cable,
    input logic [7:0] mem_read_data
);

  localparam int unsigned GridHeight = sim_pkg::CellGridHeight;  // 360
  localparam int unsigned RowBits = sim_pkg::CellGridWidth * sim_pkg::BitsPerPixel;  // 5120
  localparam int unsigned RowBytes = RowBits / 8;  // 640
  localparam int unsigned CountBits = $clog2(RowBytes + 1);  // 11
  // Wide enough to hold row_index + 2 without wrapping at the bottom edge.
  localparam int unsigned RowIdxBits = $clog2(GridHeight + 2);  // 9
  typedef logic [RowIdxBits-1:0] row_idx_t;

  typedef enum logic [2:0] {
    FRAME_START,   // empty the window and pull row 0 in
    PRIME_FIRST,   // row 0 into buffers[2]
    PRIME_SECOND,  // row 1 into buffers[1]
    START_ROW,     // signal simulator to start processing the buffers
    RUN_ROW,       // wait on pulse_row_done, and shift buffers when this happens, if pulse_transaction_pair_done, write to memory as well.
    WRITE_BACK,    // write one or two res_buffer rows to memory
    PREFETCH       // start fetching of row_index + 1 or start over from beginning of a frame
  } state_e;

  state_e state;
  row_idx_t row_index;

  logic single_row_edgecase;
  logic write_lower;  // if the lower row of a transaction pair is queued for write to memory

  // Controls if transfers to/from memory should be initiated
  logic start_xfer, start_is_write, start_buffer, xfer_active, xfer_write, xfer_idle;
  row_idx_t start_row, xfer_row;
  assign xfer_idle = !xfer_active && !start_xfer;

  logic [CountBits-1:0] bytes_requested, bytes_moved;
  logic [RowBits-1:0] write_shift;
  assign mem_write_data = write_shift[7:0];

  logic [CountBits-1:0] remaining;
  assign remaining = CountBits'(RowBytes) - bytes_requested;

  memory_pkg::message_length_t burst_length;
  always_comb begin
    if (32'(remaining) > BurstBytes) burst_length = memory_pkg::message_length_t'(BurstBytes);
    else burst_length = memory_pkg::message_length_t'(remaining);
  end

  function automatic memory_pkg::address_t row_addr(input row_idx_t y);
    return mem_map_pkg::cell_word_addr(sim_pkg::grid_x_t'(0), sim_pkg::grid_y_t'(y));
  endfunction

  always_ff @(posedge clk) begin
    pulse_new_row <= 1'b0;
    start_xfer <= 1'b0;
    pulse_row_commit <= 1'b0;
    pulse_frame_restart <= 1'b0;

    if (reset) begin
      state <= FRAME_START;
      pulse_frame_restart <= 1'b1;
      commit_y <= '0;
      commit_res_buffer <= 1'b0;
      row_index <= '0;
      single_row_edgecase <= 1'b1;
      write_lower <= 1'b0;
      start_is_write <= 1'b0;
      start_buffer <= 1'b0;
      start_row <= '0;
      xfer_active <= 1'b0;
      xfer_write <= 1'b0;
      xfer_row <= '0;
      bytes_requested <= '0;
      bytes_moved <= '0;
      write_shift <= '0;
      mem_toggle <= 1'b0;
      mem_transaction <= memory_pkg::READ;
      mem_address <= '0;
      mem_length <= '0;
      buffers[0] <= '0;
      buffers[1] <= '0;
      buffers[2] <= '0;
    end else begin
      // Burst engine, controlled by the sequencer below
      if (start_xfer) begin
        // Initiation of transfer
        xfer_row <= start_row;
        xfer_write <= start_is_write;
        mem_transaction <= start_is_write ? memory_pkg::WRITE : memory_pkg::READ;

        if (start_is_write) write_shift <= res_buffer[start_buffer][RowBits-1:0];
        bytes_requested <= '0;
        bytes_moved <= '0;
        xfer_active <= 1'b1;
      end else if (xfer_active) begin
        if (mem_data_on_cable) begin
          bytes_moved <= bytes_moved + 1'b1;
          if (xfer_write) begin
            write_shift <= {8'h00, write_shift[RowBits-1:8]}; // Shift one byte
          end else begin
            buffers[2] <= sim_pkg::row_buffer_t'({mem_read_data, buffers[2][RowBits-1:8]});
          end
        end

        if (!mem_busy && bytes_moved == bytes_requested && 32'(bytes_requested) < RowBytes) begin
          mem_address <= row_addr(xfer_row) + memory_pkg::address_t'(bytes_requested);
          mem_length <= burst_length;
          mem_toggle <= ~mem_toggle;
          bytes_requested <= bytes_requested + CountBits'(burst_length);
        end

        if (32'(bytes_moved) >= RowBytes && !mem_busy) xfer_active <= 1'b0;
      end

      // Memory management sequencer
      case (state)
        FRAME_START: begin
          row_index <= '0;
          single_row_edgecase <= 1'b1;
          write_lower <= 1'b0;
          buffers[0] <= '0;
          buffers[1] <= '0;
          buffers[2] <= '0;

          // Wait until compositor is ready to write a new frame
          if (fb_ready) begin
            // Writes first row to buffer[2] next pulse
            start_xfer <= 1'b1;
            start_is_write <= 1'b0;
            start_row <= row_idx_t'(0);
            state <= PRIME_FIRST;
          end
        end

        PRIME_FIRST: begin
          if (xfer_idle) begin
            buffers[0] <= '0;
            buffers[1] <= buffers[2];
            buffers[2] <= '0;

            // Writes second row to buffer[2] next pulse
            start_xfer <= 1'b1;
            start_is_write <= 1'b0;
            start_row <= row_idx_t'(1);
            state <= PRIME_SECOND;
          end
        end

        PRIME_SECOND: begin
          if (xfer_idle) state <= START_ROW;
        end

        START_ROW: begin
          if (sim_running) begin
            pulse_new_row <= 1'b1;
            state <= RUN_ROW;
          end
        end

        RUN_ROW: begin
          if (pulse_row_done) begin
            // Shift buffers
            buffers[0] <= buffers[1];
            buffers[1] <= buffers[2];
            buffers[2] <= '0;

            // Start a write transaction to mem if pair is done
            if (pulse_transaction_pair_done) begin
              start_xfer <= 1'b1;
              start_is_write <= 1'b1;
              pulse_row_commit <= 1'b1;

              if (!single_row_edgecase) begin
                start_row <= row_index - row_idx_t'(1);
                start_buffer <= 1'b1;
                write_lower <= 1'b1;
                commit_y <= mem_map_pkg::fb_y_t'(row_index - row_idx_t'(1));
                commit_res_buffer <= 1'b1;
              end else begin
                // When odd_major_evaluation, row 0 and 359 don't get the pair treatment.
                // Therefore we just write back the single row
                start_row <= row_index;
                start_buffer <= row_index[0];
                write_lower <= 1'b0;
                commit_y <= mem_map_pkg::fb_y_t'(row_index);
                commit_res_buffer <= row_index[0];
              end

              single_row_edgecase <= 1'b1;
              state <= WRITE_BACK;
            end else begin
              // The row that activated pulse_row_done is an upper row of a transaction pair
              single_row_edgecase <= 1'b0;
              if (32'(row_index) + 2 < GridHeight) begin
                start_xfer <= 1'b1;
                start_is_write <= 1'b0;
                start_row <= row_index + row_idx_t'(2);
              end
              state <= PREFETCH;
            end
          end
        end

        WRITE_BACK: begin
          if (xfer_idle) begin
            if (write_lower) begin
              write_lower <= 1'b0;
              start_xfer <= 1'b1;
              start_is_write <= 1'b1;
              start_row <= xfer_row + row_idx_t'(1);
              start_buffer <= 1'b0;
              pulse_row_commit <= 1'b1;
              commit_y <= mem_map_pkg::fb_y_t'(xfer_row + row_idx_t'(1));
              commit_res_buffer <= 1'b0;
            end else begin
              if (32'(row_index) + 2 < GridHeight) begin
                start_xfer <= 1'b1;
                start_is_write <= 1'b0;
                start_row <= row_index + row_idx_t'(2);
              end
              state <= PREFETCH;
            end
          end
        end

        PREFETCH: begin
          if (xfer_idle) begin
            if (32'(row_index) >= GridHeight - 1) begin
              state <= FRAME_START;
              pulse_frame_restart <= 1'b1;
            end else begin
              row_index <= row_index + row_idx_t'(1);
              state <= START_ROW;
            end
          end
        end

        default: begin
          state <= FRAME_START;
          pulse_frame_restart <= 1'b1;
        end
      endcase
    end
  end

endmodule
