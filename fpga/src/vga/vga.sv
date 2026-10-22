module vga #(
) (
    input logic clk,
    reset,
    enable,
    input color_pkg::color_t color,
    output logic [3:0] vga_r,
    vga_g,
    vga_b,
    output vga_sync_params::x_coordinate_t coord_x,
    output vga_sync_params::y_coordinate_t coord_y,
    output logic    active_area,
    pixel_pulse,
    h_sync,
    v_sync,
    v_blank,
    h_blank,
    frame_start,
    near_frame_end
);
  import vga_sync_params::*;
  vga_clock_enable_pulse #(
      .N(PixelClockDelay)
  ) pixel_pulser (
      .clk(clk),
      .reset(reset),
      .enable(enable),
      .pulse(pixel_pulse)
  );

  vga_frame_counter #() vga_frame_counter (
      .clk(clk),
      .reset(reset),
      .enable(enable),
      .pixel_pulse(pixel_pulse),
      .vertical_count(coord_y),
      .horizontal_count(coord_x)
  );

  vga_decoding_circuit #() vga_decoding_circuit (
      .horizontal_count(coord_x),
      .vertical_count(coord_y),
      .h_sync(h_sync),
      .v_sync(v_sync),
      .active_area(active_area)
  );

  vga_phase vga_phase (
      .clk(clk),
      .reset(reset),
      .coord_x(coord_x),
      .coord_y(coord_y),
      .pixel_pulse(pixel_pulse),
      .v_blank(v_blank),
      .h_blank(h_blank),
      .frame_start(frame_start),
      .near_frame_end(near_frame_end)
  );

  assign vga_r = color.red;
  assign vga_g = color.green;
  assign vga_b = color.blue;

endmodule
