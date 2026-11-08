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

    // PSRAM pins (Infineon S70KL1283, octal xSPI)
    output wire mem_out_clk,
    mem_out_clk_n,
    mem_cs_n,
    mem_reset_n,

    inout wire [7:0] mem_dq,

    inout wire mem_rwds,

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
  logic active_area, pixel_pulse, v_blank, h_blank, frame_start, line_end, near_frame_end;
  vga_sync_params::x_coordinate_t coord_x;
  vga_sync_params::y_coordinate_t coord_y;
  color_pkg::color_t color;

  // Sim running
  logic sim_running = 1'b1; // Temporarily 1

  // Memory signals
  logic spi_mem_toggle;
  logic [31:0] spi_mem_addr;
  memory_pkg::message_length_t spi_mem_len;
  logic [7:0] spi_mem_in, mem_out;
  memory_pkg::transaction_e spi_mem_transaction;

  localparam int unsigned NumLayers  = 2;
  localparam int unsigned LayerCells = 0;
  localparam int unsigned LayerHud   = 1;

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
      .near_frame_end(near_frame_end),
      .line_end(line_end)
  );


//   layer_pkg::layer_px_t cells_pixel_unused;
//   cells cell_controller (
//       .clk(clk),
//       .sw(sw),
//       .sim_running(sim_running),
//       .active_area(active_area),
//       .pixel_pulse(pixel_pulse),
//       .coord_x(coord_x),
//       .coord_y(coord_y),
//       .pixel(cells_pixel_unused) // Temporary for implementing frame buffer
//   );

  logic pulse_new_row;
  sim_pkg::row_buffer_t sim_window [3];
  logic pulse_transaction_pair_done, pulse_row_done; // pulse_row_done flytt inn ny rad, pulse_transaction_pair_done
  sim_pkg::row_buffer_t sim_res_buffer [2];

  logic sim_mem_toggle;
  logic [31:0] sim_mem_addr;
  memory_pkg::message_length_t sim_mem_len;
  logic [7:0] sim_mem_in;
  memory_pkg::transaction_e sim_mem_transaction;

  sim_buffers sim_line_memory_reader(
    .clk(clk),
    .reset(sw[3]),
    .sim_running(sim_running),

    .pulse_new_row(pulse_new_row),
    .buffers(sim_window),
    .pulse_transaction_pair_done(pulse_transaction_pair_done),
    .pulse_row_done(pulse_row_done),
    .res_buffer(sim_res_buffer),

    .mem_toggle(sim_mem_toggle),
    .mem_transaction(sim_mem_transaction),
    .mem_address(sim_mem_addr),
    .mem_length(sim_mem_len),
    .mem_write_data(sim_mem_in),
    .mem_busy(mem_client_busy[memory_pkg::MemClientSimID]),
    .mem_data_on_cable(mem_client_data_on_cable[memory_pkg::MemClientSimID]),
    .mem_read_data(mem_out)
  );


  simulator sim_controller (
    .clk(clk),
    .pulse_new_row(pulse_new_row),
    .sim_running(sim_running),
    .buffers(sim_window),
    .pulse_transaction_pair_done(pulse_transaction_pair_done),
    .pulse_row_done(pulse_row_done),
    .res_buffer(sim_res_buffer)
  );

  // Framebuffer
  logic fb_sel;
  assign fb_sel = 1'b0;

  logic fb_underrun;
  logic fb_mem_toggle;
  memory_pkg::transaction_e fb_mem_transaction;
  memory_pkg::address_t fb_mem_address;
  memory_pkg::message_length_t fb_mem_length;

  fb_scanout fb_display (
      .clk(clk),
      .reset(sw[3]),
      .active_area(active_area),
      .coord_x(coord_x),
      .coord_y(coord_y),
      .line_end(line_end),
      .near_frame_end(near_frame_end),
      .fb_sel(fb_sel),

      .mem_toggle(fb_mem_toggle),
      .mem_transaction(fb_mem_transaction),
      .mem_address(fb_mem_address),
      .mem_length(fb_mem_length),
      .mem_busy(mem_client_busy[memory_pkg::MemClientScanoutID]),
      .mem_data_on_cable(mem_client_data_on_cable[memory_pkg::MemClientScanoutID]),
      .mem_read_data(mem_out),

      .pixel(layers[LayerCells]),
      .underrun(fb_underrun)
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
      .sprite_w_mem_addr(spi_mem_addr),
      .sprite_req_op(sprite_req_op),
      .sprite_req_body(sprite_req_body),
      .spriteLUT(spriteLUT),
      .sprite_id_bitmap(sprite_id_bitmap),
      .sprite_available_id(sprite_available_id),
      .sprite_id_found(sprite_id_found),
      .write_sprite_valid(write_sprite_valid)
  );

  // Controls who is allowed the external memory bus
  logic [memory_pkg::NumMemClients-1:0] mem_allow;
  mem_access_policy access_policy (
      .v_blank(v_blank),
      .near_frame_end(near_frame_end),
      .sim_running(sim_running),
      .allow(mem_allow)
  );

  logic [memory_pkg::NumMemClients-1:0] mem_client_toggle;
  memory_pkg::transaction_e mem_client_transaction[memory_pkg::NumMemClients];
  memory_pkg::address_t mem_client_address[memory_pkg::NumMemClients];
  memory_pkg::message_length_t mem_client_length_burst[memory_pkg::NumMemClients];
  logic [7:0] mem_client_write_data[memory_pkg::NumMemClients];
  logic [memory_pkg::NumMemClients-1:0] mem_client_busy;
  logic [memory_pkg::NumMemClients-1:0] mem_client_data_on_cable;
  logic [memory_pkg::NumMemClients-1:0] mem_client_grant; // Used by testbenches

  always_comb begin
    mem_client_toggle = '0;
    for (int unsigned i = 0; i < memory_pkg::NumMemClients; i++) begin
      mem_client_transaction[i] = memory_pkg::NONE;
      mem_client_address[i]     = '0;
      mem_client_length_burst[i]      = '0;
      mem_client_write_data[i]  = '0;
    end

    // SPI
    mem_client_toggle[memory_pkg::MemClientSpiID]      = spi_mem_toggle;
    mem_client_transaction[memory_pkg::MemClientSpiID] = spi_mem_transaction;
    mem_client_address[memory_pkg::MemClientSpiID]     = spi_mem_addr;
    mem_client_length_burst[memory_pkg::MemClientSpiID]= spi_mem_len;
    mem_client_write_data[memory_pkg::MemClientSpiID]  = spi_mem_in;

    // Scan-out fb
    mem_client_toggle[memory_pkg::MemClientScanoutID]       = fb_mem_toggle;
    mem_client_transaction[memory_pkg::MemClientScanoutID]  = fb_mem_transaction;
    mem_client_address[memory_pkg::MemClientScanoutID]      = fb_mem_address;
    mem_client_length_burst[memory_pkg::MemClientScanoutID] = fb_mem_length;

    // Sim
    mem_client_toggle[memory_pkg::MemClientSimID]      = sim_mem_toggle;
    mem_client_transaction[memory_pkg::MemClientSimID] = sim_mem_transaction;
    mem_client_address[memory_pkg::MemClientSimID]     = sim_mem_addr;
    mem_client_length_burst[memory_pkg::MemClientSimID]= sim_mem_len;
    mem_client_write_data[memory_pkg::MemClientSimID]  = sim_mem_in;

    // TODO: Add the other clients here when they implement the external SRAM
  end

  memory memory_subsystem (
      .clk(clk),
      .reset(sw[3]),
      .enable(sw[2]),

      .client_toggle(mem_client_toggle),
      .client_next_transaction(mem_client_transaction),
      .client_address(mem_client_address),
      .client_length(mem_client_length_burst),
      .client_write_data(mem_client_write_data),
      .client_allow(mem_allow),

      .client_busy(mem_client_busy),
      .client_data_on_cable(mem_client_data_on_cable),
      .client_grant(mem_client_grant),
      .read_data(mem_out),

      .out_clk(mem_out_clk),
      .out_clk_n(mem_out_clk_n),
      .cs_n(mem_cs_n),
      .reset_n(mem_reset_n),
      .dq(mem_dq),
      .rwds(mem_rwds)
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
      .mem_in(spi_mem_in),
      .mem_out(mem_out),
      .mem_len(spi_mem_len),
      .mem_addr(spi_mem_addr),
      .mem_transaction(spi_mem_transaction),
      .mem_busy(mem_client_busy[memory_pkg::MemClientSpiID]),
      .mem_allow(mem_allow[memory_pkg::MemClientSpiID]),
      .mem_data_on_wire(mem_client_data_on_cable[memory_pkg::MemClientSpiID]),
      .mem_toggle(spi_mem_toggle),

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

