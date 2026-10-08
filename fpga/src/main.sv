module main (
    input wire       clk,
    input wire [3:2] sw,
    input wire [3:0] btn,

    output wire [3:0] vga_r,
    vga_g,
    vga_b,

    output wire vga_hsync,
    vga_vsync,


    // SPI Signals
    input wire ck_ss,
    ck_sck,
    ck_rst,

    inout wire [7:0] octo_spi,

    inout wire acknack,

    output wire led,
    led0_r,
    led0_g,
    led0_b,
    ready,
    out_almost_empty,
    out_empty,
    in_almost_full,
    in_full
);
  logic active_area, pixel_pulse;
  vga_sync_params::x_coordinate_t coord_x;
  vga_sync_params::y_coordinate_t coord_y;
  color_pkg::color_t color;

  localparam int unsigned NumLayers = 3;
  localparam int unsigned LayerCells = 0;
  localparam int unsigned LayerGol = 1;
  localparam int unsigned LayerHud = 2;

  layer_pkg::layer_px_t layers[NumLayers];

  vga vga_controller (
      .clk(clk),
      .reset(sw[3]),
      .enable(sw[2]),
      .h_sync(vga_hsync),
      .v_sync(vga_vsync),
      .coord_x(coord_x),
      .coord_y(coord_y),
      .active_area(active_area),
      .pixel_pulse(pixel_pulse),
      .vga_r(vga_r),
      .vga_g(vga_g),
      .vga_b(vga_b),
      .color(color)
  );

  cells cell_controller (
      .clk(clk),
      .sw(sw),
      .active_area(active_area),
      .pixel_pulse(pixel_pulse),
      .coord_x(coord_x),
      .coord_y(coord_y),
      .pixel(layers[LayerCells])
  );

  gol gol_controller (
      .clk(clk),
      .reset(sw[3]),
      .active_area(active_area),
      .pixel_pulse(pixel_pulse),
      .pixel(layers[LayerGol])
  );

  compositor #(
      .NumLayers(NumLayers)
  ) layer_compositor (
      .active_area(active_area),
      .layers(layers),
      .color(color)
  );

  spi spi_controller (
      .clk(clk),
      .ck_ss(ck_ss),
      .ck_sck(ck_sck),
      .ck_rst(ck_rst),
      .octo_spi(octo_spi),
      .acknack(acknack),
      .led(led),
      .led0_r(led0_r),
      .led0_g(led0_g),
      .led0_b(led0_b),
      .ready(ready),

      .out_almost_empty(out_almost_empty),
      .out_empty(out_empty),
      .in_almost_full(in_almost_full),
      .in_full(in_full)
  );

endmodule

