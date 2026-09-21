module main (
    input  wire       clk,
    input  wire [3:2] sw,
    input wire [3:0] btn,
    output wire [3:0] vga_r,
    vga_g,
    vga_b,
    output wire       vga_hsync,
    vga_vsync
);
  logic active_area;
  vga_sync_params::x_coordinate_t coord_x;
  vga_sync_params::y_coordinate_t coord_y;

  color_pkg::color_t color;

  vga_controller vga_controller_unit (
      .clk(clk),
      .reset(sw[3]),
      .enable(sw[2]),
      .h_sync(vga_hsync),
      .v_sync(vga_vsync),
      .coord_x(coord_x),
      .coord_y(coord_y),
      .active_area(active_area),
      .pixel_pulse(pixel_pulse)
  );
  vga_game_example vga_game_example (
      .clk(clk),
      .reset(sw[3]),
      .enable(sw[2]),
      .up(btn[0]),
      .down(btn[1]),
      .left(btn[2]),
      .right(btn[3]),
      .coord_x(coord_x),
      .coord_y(coord_y),
      .active_area(active_area),
      .color(color)
  );
  assign vga_r = color.red;
  assign vga_g = color.green;
  assign vga_b = color.blue;

endmodule

