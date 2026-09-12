module main (
    input  wire       clk,
    input  wire [3:3] sw,
    input wire [3:0] btn,
    output wire [3:0] vga_r,
    vga_g,
    vga_b,
    output wire       vga_hsync,
    vga_vsync
);
  wire h_sync, v_sync, active_area;
  wire [9:0] coord_x, coord_y;
  reg enable = 'b1;

  color_pkg::color_t color;

  vga_controller vga_controller_unit (
      .clk(clk),
      .reset(sw[3]),
      .enable(enable),
      .h_sync(h_sync),
      .v_sync(v_sync),
      .coord_x(coord_x),
      .coord_y(coord_y),
      .active_area(active_area)
  );
  vga_game_example vga_game_example (
      .clk(clk),
      .reset(sw[3]),
      .enable(enable),
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

  // 640x480@60 needs NEGATIVE sync polarity; vga_controller drives its
  // sync pulses active-high, so invert here.
  assign vga_hsync = ~h_sync;
  assign vga_vsync = ~v_sync;
endmodule

