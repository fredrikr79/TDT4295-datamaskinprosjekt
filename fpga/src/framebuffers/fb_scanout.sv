module fb_scanout #(
    parameter int unsigned BurstBytes = memory_pkg::MaxMessageLength,
    parameter int unsigned LineBytes  = mem_map_pkg::GridWidth
) (
    input logic clk,
    input logic reset,

    input logic active_area,
    input vga_sync_params::x_coordinate_t coord_x,
    input vga_sync_params::y_coordinate_t coord_y,
    input logic line_end,
    input logic near_frame_end,
    input logic fb_sel,

    // Memory client ports
    output logic mem_toggle,
    output memory_pkg::transaction_e mem_transaction,
    output memory_pkg::address_t mem_address,
    output memory_pkg::message_length_t mem_length,
    input logic mem_busy,
    input logic mem_data_on_cable,
    input logic [7:0] mem_read_data,

    output layer_pkg::layer_px_t pixel,
    output logic underrun // Used by testbench to measure if fetching lines is behind schedule.
);

  localparam int unsigned AddrBits = $clog2(LineBytes);

  logic fill_we;
  logic [7:0] fill_data;

  fb_line_fetcher #(
      .BurstBytes(BurstBytes),
      .LineBytes (LineBytes)
  ) fetcher (
      .clk(clk),
      .reset(reset),
      .coord_y(coord_y),
      .line_end(line_end),
      .near_frame_end(near_frame_end),
      .fb_sel(fb_sel),
      .mem_toggle(mem_toggle),
      .mem_transaction(mem_transaction),
      .mem_address(mem_address),
      .mem_length(mem_length),
      .mem_busy(mem_busy),
      .mem_data_on_cable(mem_data_on_cable),
      .mem_read_data(mem_read_data),
      .fill_we(fill_we),
      .fill_data(fill_data)
  );

  logic beam_in_line;
  assign beam_in_line = active_area && (32'(coord_x) < LineBytes);

  logic [AddrBits-1:0] read_x;
  assign read_x = AddrBits'(coord_x);

  logic [7:0] palette_index;

  fb_line_buffer #(
      .LineBytes(LineBytes)
  ) line_buffer (
      .clk(clk),
      .reset(reset),
      .fill_we(fill_we),
      .fill_data(fill_data),
      .swap_buffer(line_end),
      .read_x(read_x),
      .read_en(beam_in_line),
      .read_data(palette_index),
      .underrun(underrun)
  );

  color_pkg::color_t pixel_color;

  palette colour_table (
      .index(palette_pkg::palette_index_t'(palette_index)),
      .color(pixel_color)
  );

  logic beam_in_line_d1;
  always_ff @(posedge clk) begin
    if (reset) beam_in_line_d1 <= 1'b0;
    else beam_in_line_d1 <= beam_in_line;
  end

  always_comb begin
    pixel = layer_pkg::TransparentPixel;
    if (beam_in_line_d1) pixel = layer_pkg::gen_layer_px(pixel_color);
  end
endmodule
