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

  // Memory signals
  logic mem_busy, mem_data_on_wire, mem_toggle;
  logic [31:0] mem_addr;
  logic [7:0] mem_len, mem_in, mem_out;
  memory_pkg::transaction_e mem_transaction;


  vga vga_controller (
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

  cells cell_controller (
      .clk(clk),
      .sw(sw),
      .active_area(active_area),
      .pixel_pulse(pixel_pulse),
      .coord_x(coord_x),
      .coord_y(coord_y),
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
      .in_full(in_full),

      // Memory module fall-through signals
      .mem_busy(mem_busy),
      .mem_data_on_wire(mem_data_on_wire),
      .mem_toggle(mem_toggle),
      .mem_addr(mem_addr),
      .mem_len(mem_len),
      .mem_in(mem_in),
      .mem_out(mem_out),
      .mem_transaction(mem_transaction)
  );

endmodule

