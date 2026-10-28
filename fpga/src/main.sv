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

    output wire [3:0] led,

    output wire led0_r,
    led0_g,
    led0_b,
    ready,
    out_almost_empty,
    out_empty,
    in_almost_full,
    in_full
);
  logic active_area, pixel_pulse, v_blank, h_blank, frame_start, line_end;
  vga_sync_params::x_coordinate_t coord_x;
  vga_sync_params::y_coordinate_t coord_y;
  color_pkg::color_t color;

  // Sim running
  logic sim_running = 1'b1; // Temporarily 1

  // Memory signals
  logic mem_busy, mem_data_on_wire, mem_toggle;
  logic [31:0] mem_addr;
  logic [15:0]  mem_len;
  logic [7:0] mem_in, mem_out;
  memory_pkg::transaction_e mem_transaction;

  localparam int unsigned NumLayers  = 3;
  localparam int unsigned LayerCells = 0;
  localparam int unsigned LayerGol   = 1;
  localparam int unsigned LayerHud   = 2;

  layer_pkg::layer_px_t layers[NumLayers];

  // Critical error detection (unrecoverable errors arising outside the SPI module)
  wire error_critical;

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
      .color(color),
      .v_blank(v_blank),
      .h_blank(h_blank),
      .frame_start(frame_start),
      .near_frame_end(near_frame_end)
  );

  cells cell_controller (
      .clk(clk),
      .sw(sw),
      .sim_running(sim_running),
      .active_area(active_area),
      .pixel_pulse(pixel_pulse),
      .coord_x(coord_x),
      .coord_y(coord_y),
      .pixel(layers[LayerCells])
  );

  gol gol_controller (
      .clk(clk),
      .reset(sw[3]),
      .sim_running(sim_running),
      .active_area(active_area),
      .pixel_pulse(pixel_pulse),
      .pixel(layers[LayerGol])
  );

  // TODO: Implement HUD layer. Temporarily always transparent. Remove after implementing the HUD layer.
  assign layers[LayerHud] = layer_pkg::TransparentPixel;

  compositor #(
      .NumLayers(NumLayers)
  ) layer_compositor (
      .active_area(active_area),
      .layers(layers),
      .color(color)
  );

  // HUD sprites LUT declaration
  hud_pkg::sprite_inst_entry_t spriteLUT[hud_pkg::SpriteLutDepth];

  hud_pkg::sprite_op_e sprite_req_op;
  wire sprite_req_valid, sprite_id_found, write_sprite_valid;
  hud_pkg::sprite_mem_id sprite_mem_id;
  hud_pkg::sprite_inst_entry_t sprite_req_body;
  wire [hud_pkg::SpriteLutDepth-1:0] sprite_id_bitmap;
  hud_pkg::sprite_lut_id sprite_available_id;

  hud hud_controller (
      .clk(clk),
      .req_valid(sprite_req_valid),
      .sprite_mem_id(sprite_mem_id),
      .sprite_w_mem_addr(mem_addr),
      .sprite_req_op(sprite_req_op),
      .sprite_req_body(sprite_req_body),
      .spriteLUT(spriteLUT),
      .sprite_id_bitmap(sprite_id_bitmap),
      .sprite_available_id(sprite_available_id),
      .sprite_id_found(sprite_id_found),
      .write_sprite_valid(write_sprite_valid)
  );

  // HUD sprites LUT declaration
  hud_pkg::sprite_inst_entry_t spriteLUT [hud_pkg::SpriteLutDepth];
  hud hud_controller (
      .clk(clk),
      .req_valid(sprite_req_valid),
      .sprite_mem_id(sprite_mem_id),
      .sprite_w_mem_addr(mem_addr),
      .sprite_req_op(sprite_req_op),
      .sprite_req_body(sprite_req_body),
      .spriteLUT(spriteLUT),
      .sprite_id_bitmap(sprite_id_bitmap),
      .sprite_available_id(sprite_available_id),
      .sprite_id_found(sprite_id_found),
      .write_sprite_valid(write_sprite_valid)
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

//       // Sim running
      .sim_running(sim_running),

      // Memory module fall-through signals
      .mem_in(mem_in),
      .mem_out(mem_out),
      .mem_len(mem_len),
      .mem_addr(mem_addr),
      .mem_transaction(mem_transaction),
      .mem_busy(mem_busy),
      .mem_data_on_wire(mem_data_on_wire),
      .mem_toggle(mem_toggle),

      // Sprite LUT transaction signals
      .sprite_available_id(sprite_available_id),
      .sprite_id_found(sprite_id_found),
      .sprite_id_bitmap(sprite_id_bitmap),
      .sprite_req_valid(sprite_req_valid),
      .sprite_req_body(sprite_req_body),
      .sprite_req_op(sprite_req_op),
      .write_sprite_valid(write_sprite_valid),
      .write_sprite_mem_id(sprite_mem_id),

      // Critical error line
      .error_critical(error_critical)
  );

endmodule

