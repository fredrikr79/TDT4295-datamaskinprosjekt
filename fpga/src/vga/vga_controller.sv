module vga_controller #(
) (
    input logic clk,
    reset,
    enable,
    output logic h_sync,
    v_sync,
    output vga_sync_params::x_coordinate_t coord_x,
    output vga_sync_params::y_coordinate_t coord_y,
    output logic active_area,
    output logic pixel_pulse
);
  import vga_sync_params::*;
  clock_enable_pulse #(
      .N(PixelClockDelay)
  ) pixel_pulser (
      .clk(clk),
      .reset(reset),
      .enable(enable),
      .pulse(pixel_pulse)
  );

  frame_counter #() vga_frame_counter (
      .clk(clk),
      .reset(reset),
      .enable(enable),
      .pixel_pulse(pixel_pulse),
      .vertical_count(coord_y),
      .horizontal_count(coord_x)
  );

  decoding_circuit #() vga_decoding_circuit (
      .horizontal_count(coord_x),
      .vertical_count(coord_y),
      .h_sync(h_sync),
      .v_sync(v_sync),
      .active_area(active_area)
  );

endmodule
