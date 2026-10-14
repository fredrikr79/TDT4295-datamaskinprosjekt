module frame_counter (
    input wire clk,
    input wire reset,
    input wire enable,
    input wire pixel_pulse,
    output vga_sync_params::x_coordinate_t horizontal_count,
    output vga_sync_params::y_coordinate_t vertical_count
);
  wire enable_horizontal_counter, enable_vertical_counter;
  assign enable_horizontal_counter = enable & pixel_pulse;
  assign enable_vertical_counter = enable & pixel_pulse && 32'(horizontal_count)
      == vga_sync_params::TotalWidth - 1;

  mod_n_counter #(
      .N(vga_sync_params::TotalHeight)
  ) vertical_counter (
      .clk(clk),
      .reset(reset),
      .enable(enable_vertical_counter),
      .count(vertical_count)
  );

  mod_n_counter #(
      .N(vga_sync_params::TotalWidth)
  ) horizontal_counter (
      .clk(clk),
      .reset(reset),
      .enable(enable_horizontal_counter),
      .count(horizontal_count)
  );

endmodule
