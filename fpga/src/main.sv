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
  logic active_area, pixel_pulse;
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

  wire we;
  wire [cell_pkg::AddrBits-1:0] write_addr, read_addr;
  cell_pkg::cell_word_t write_data, read_data;

  cell_memory memory (
      .clk       (clk),
      .we        (we),
      .write_addr(write_addr),
      .write_data(write_data),
      .read_addr (read_addr),
      .read_data (read_data)
  );

  cell_renderer renderer (
      .active_area(active_area),
      .coord_x    (coord_x),
      .coord_y    (coord_y),
      .read_addr  (read_addr),
      .read_data  (read_data),
      .color      (color)
  );
  assign vga_r = color.red;
  assign vga_g = color.green;
  assign vga_b = color.blue;

endmodule

