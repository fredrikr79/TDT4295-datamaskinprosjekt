module main #(
  parameter bit ON_BOARD_MEMORY = 1'b1
)(
    input wire clk_in,
    input wire user_btn,

    output wire [4:0] vga_r,
    vga_b,
    output wire [5:0] vga_g,

    output wire vga_hsync,
    vga_vsync,


    // SPI Signals
    input wire ck_ss,
    ck_sck,
    ck_rst,

    inout wire [7:0] octo_spi,

    output wire led_r_n,
    led_g_n,
    led_b_n,
    ready,
    acknack,

    // Memory signals
    output logic mem_clk,
    output logic mem_clk_n,
    output logic mem_cs_n,
    output logic mem_reset_n,
    inout wire [7:0] mem_data_bus,
    inout wire mem_rwds
);


  wire mem_run_clock;
  wire clk, mem_clk_raw;
  // Main onboard clock
  clk_wiz_0 clk_master (
    .clk_in(clk_in),
    .clk(clk),
    .mem_clk(mem_clk_raw)
  );
  assign mem_clk = mem_run_clock ? mem_clk_raw : 0;

  logic active_area, pixel_pulse;
  vga_sync_params::x_coordinate_t coord_x;
  vga_sync_params::y_coordinate_t coord_y;
  color_pkg::color_t color;

  // Sim running
  logic sim_running;

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

  logic pulse_new_row, pulse_transaction_pair_done, pulse_row_done;
  sim_pkg::row_buffer_t buffers[3];
  sim_pkg::row_buffer_t res_buffers[2];

  // Critical error detection (unrecoverable errors arising outside the SPI module)
  wire error_critical;
  // TODO drive from submodules
  assign error_critical = 1'b0;

  // vga vga_controller (
  //     .clk(clk),
  //     .reset(sw[3]),
  //     .enable(sw[2]),
  //     .h_sync(vga_hsync),
  //     .v_sync(vga_vsync),
  //     .coord_x(coord_x),
  //     .coord_y(coord_y),
  //     .active_area(active_area),
  //     .pixel_pulse(pixel_pulse),
  //     .vga_r(vga_r),
  //     .vga_g(vga_g),
  //     .vga_b(vga_b),
  //     .color(color)
  // );


//   // TODO: Implement HUD layer. Temporarily always transparent. Remove after implementing the HUD layer.
//   assign layers[LayerHud] = layer_pkg::TransparentPixel;

//   compositor #(
//       .NumLayers(NumLayers)
//   ) layer_compositor (
//       .active_area(active_area),
//       .layers(layers),
//       .color(color)
//   );

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

  spi spi_controller (
      .clk(clk),
      .ck_ss(ck_ss),
      .ck_sck(ck_sck),
      .ck_rst(ck_rst),
      .octo_spi(octo_spi),
      .acknack(acknack),
      .led_r_n(led_r_n),
      .led_g_n(led_g_n),
      .led_b_n(led_b_n),
      .ready(ready),

      .out_almost_empty(out_almost_empty),
      .out_empty(out_empty),
      .in_almost_full(in_almost_full),
      .in_full(in_full),

      // Sim running
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


  // simulator simulation_core (
  //   .clk(clk),
  //   .pulse_new_row(pulse_new_row),
  //   .sim_running(sim_running),
  //   .buffers(buffers),
  //   .pulse_transaction_pair_done(pulse_transaction_pair_done),
  //   .pulse_row_done(pulse_row_done),
  //   .res_buffer(res_buffers)
  // );

  if(ON_BOARD_MEMORY) begin : g_mem_onboard
    memory_onboard memory (
      .clk(clk),
      .reset(~ck_rst),
      .enable(1'b1),
      .transaction_toggle(mem_toggle),

      .next_transaction(mem_transaction),
      .address(mem_addr),
      .length(mem_len),
      .write_data(mem_in),

      // Info to FPGA
      .busy(mem_busy),
      .data_on_cable(mem_data_on_wire),
      .read_data(mem_out)
    );
  end else begin : g_mem

    memory memory (
      .clk(clk),
      .reset(~ck_rst),
      .enable(1'b1),
      .transaction_toggle(mem_toggle),
      .next_transaction(mem_transaction),
      .address(mem_addr),
      .length(mem_len),
      .write_data(mem_in),
      .busy(mem_busy),
      .run_clock(mem_run_clock),
      .data_on_cable(mem_data_on_wire),
      .read_data(mem_out),

      // Actual memory pins
      .cs_n(mem_cs_n),
      .reset_n(mem_reset_n),
      .dq(mem_data_bus),
      .rwds(mem_rwds)
    );
  end

endmodule

