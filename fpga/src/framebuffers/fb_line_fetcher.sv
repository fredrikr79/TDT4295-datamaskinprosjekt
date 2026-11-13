module fb_line_fetcher #(
    parameter int unsigned BurstBytes = memory_pkg::MaxMessageLength,
    parameter int unsigned LineBytes  = mem_map_pkg::GridWidth
) (
    input logic clk,
    input logic reset,

    input vga_sync_params::y_coordinate_t coord_y,
    input logic line_end,
    input logic near_frame_end,
    input logic fb_sel,

    output logic mem_toggle,
    output memory_pkg::transaction_e mem_transaction,
    output memory_pkg::address_t mem_address,
    output memory_pkg::message_length_t mem_length,
    input logic mem_busy,
    input logic mem_data_on_cable,
    input logic [7:0] mem_read_data,

    output logic fill_we,
    output logic [7:0] fill_data
);

  import vga_sync_params::TotalHeight;
  import config_pkg::ActiveHeight;

  localparam int unsigned CountBits = $clog2(LineBytes + 1);  // 11

  logic [CountBits-1:0] bytes_received;  // delivered into the line buffer
  logic [CountBits-1:0] bytes_requested;
  logic line_in_progress;

  mem_map_pkg::fb_y_t next_line;
  always_comb begin
    if (32'(coord_y) >= TotalHeight - 1) next_line = '0;
    else next_line = mem_map_pkg::fb_y_t'(coord_y + 1'b1);
  end

  logic fetch_needed;
  assign fetch_needed = (32'(coord_y) < ActiveHeight - 1) || near_frame_end; // + Fetch when new frame about to start
  logic [CountBits-1:0] remaining;
  assign remaining = CountBits'(LineBytes) - bytes_requested;

  memory_pkg::message_length_t burst_length;
  always_comb begin
    if (32'(remaining) > BurstBytes) burst_length = memory_pkg::message_length_t'(BurstBytes);
    else burst_length = memory_pkg::message_length_t'(remaining);
  end

  assign mem_transaction = memory_pkg::READ;
  assign fill_we   = line_in_progress && mem_data_on_cable;
  assign fill_data = mem_read_data;

  always_ff @(posedge clk) begin
    if (reset) begin
      bytes_received <= '0;
      bytes_requested <= '0;
      line_in_progress <= 1'b0;
      mem_toggle <= 1'b0;
      mem_address <= '0;
      mem_length <= '0;
    end else begin
      if (line_end) begin
        bytes_received  <= '0;
        bytes_requested <= '0;
        line_in_progress    <= 1'b1;
      end else if (line_in_progress) begin
        if (mem_data_on_cable) bytes_received <= bytes_received + 1'b1;

        if (fetch_needed && !mem_busy && (bytes_received == bytes_requested)
            && 32'(bytes_requested) < LineBytes) begin
          mem_address <= mem_map_pkg::fb_line_addr(fb_sel, next_line)
                       + memory_pkg::address_t'(bytes_requested);
          mem_length  <= burst_length;
          mem_toggle  <= ~mem_toggle;
          bytes_requested <= bytes_requested + CountBits'(burst_length);
        end

        if (32'(bytes_received) >= LineBytes) line_in_progress <= 1'b0;
      end
    end
  end

endmodule
