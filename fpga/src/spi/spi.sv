module spi #(
    parameter bit DEBUG = 1'd1
) (
    input wire clk,
    input wire ck_ss,
    input wire ck_sck,
    input wire ck_rst,
    input wire error_critical,

    // Octal SPI data bus
    inout wire [7:0] octo_spi,
    inout wire       acknack,

    output wire [3:0] led,
    output wire       led0_r,
    output wire       led0_g,
    output wire       led0_b,
    output wire       ready,

    output wire out_almost_empty,
    output wire out_empty,
    output wire in_almost_full,
    output wire in_full,

    output wire sim_running,

    // Memory
    input logic [7:0] mem_out,
    output logic [7:0] mem_in,
    mem_len,
    input logic mem_busy,
    mem_data_on_wire,
    output logic [31:0] mem_addr,
    output memory_pkg::transaction_e mem_transaction,

    // Sprite LUT
    input logic sprite_id_found,
    input logic [7:0]
    sprite_available_id,
    output hud_pkg::sprite_inst_entry_t sprite_req_body,
    output hud_pkg::sprite_op_e sprite_req_op,
    output logic write_sprite_valid,
    mem_toggle,
    sprite_req_valid
);

  import spi_pck::*;

  // FIFO states
  reg in_empty;
  reg out_full;

  // STATE / CMD TRACKERS
  spi_pck::state_e state;
  spi_pck::opcode_e opcode;
  reg proc_done;

  // READY
  assign ready = (state == ST_READY) || (state == ST_TX_IDLE);

  // BOOT CONDITION
  wire in_ready;
  wire out_ready;
  wire boot_done;
  assign boot_done = in_ready && out_ready;

  // FPGA RD / WR permissions
  wire out_w_en;
  wire in_r_en;

  // data registers
  wire [7:0] in_fifo;
  logic [7:0] out_fifo;

  // FPGA side read/write conditions
  // signals are MUX'ed, it assumes that only one process drives in_r at any point in time.
  wire proc_r_en, rx_r_en, tx_r_en;
  wire proc_w_en;

  assign in_r_en  = proc_r_en | rx_r_en | tx_r_en;
  assign out_w_en = proc_w_en;

  // CDC synchronizations CK_SCK -> CLK
  reg [2:0] ck_ss_sync = 3'b000;
  reg [2:0] ck_rst_sync = 3'b000;
  reg [2:0] out_empty_sync = 3'b000;

  // ck_sck domain signals synchronized into clk domain
  wire ck_ss_s = ck_ss_sync[2];
  wire ck_rst_s = ck_rst_sync[2];
  wire out_empty_s = out_empty_sync[2];

  // RESET
  reg rst_en = 1'b0;  // programatic reset
  wire reset = ~ck_rst || rst_en;
  wire reset_s = ~ck_rst_s || rst_en;

  // Error detection
  logic error_valid,
    fsm_error_valid,
    rx_error_valid,
    tx_error_valid,
    processor_error_valid;

  assign error_event.valid = error_critical  |
                             fsm_error_valid |
                             rx_error_valid  |
                             tx_error_valid  |
                             processor_error_valid;

  logic [7:0] fsm_error_valid,
    rx_error_valid,
    tx_error_valid,
    processor_error_valid;

  always_ff @(posedge clk) begin
    if (error_critical)
      error_event.code <= CRITICAL_ERROR;
    if (fsm_error_valid)
      error_event.code <= fsm_error_code;
    else if (rx_error_valid)
      error_event.code <= rx_error_code;
    else if (tx_error_valid)
      error_event.code <= rx_error_code;
    else if (processor_error_valid)
      error_event.code <= processor_error_code;
  end

  always_ff @(posedge clk) begin
    if (reset) begin
      ck_ss_sync <= 3'b111;
      ck_rst_sync <= 3'b000;
      out_empty_sync <= 3'b111;
    end else begin
      ck_ss_sync <= {ck_ss_sync[1:0], ck_ss};
      ck_rst_sync <= {ck_rst_sync[1:0], ck_rst};
      out_empty_sync <= {out_empty_sync[1:0], out_empty};
    end
  end

  // SUBMODULES

  // State handler
  spi_fsm fsm (
      .clk(clk),
      .opcode(opcode),
      .reset_s(reset_s),
      .boot_done(boot_done),
      .ck_ss_s(ck_ss_s),
      .in_empty(in_empty),
      .out_full(out_full),
      .state(state),
      .proc_done(proc_done),
      .error_valid(fsm_error_valid),
      .error_code(fsm_error_code)
  );

  // Data recieve module
  spi_rx rx (
      .clk(clk),
      .ck_sck(ck_sck),
      .ck_ss(ck_ss),
      .ck_ss_s(ck_ss_s),
      .reset(reset),
      .reset_s(reset_s),
      .octo_spi(octo_spi),
      .in_r_en(in_r_en),
      .rx_r_en(rx_r_en),
      .state(state),
      .acknack(acknack),
      .in_empty(in_empty),
      .in_ready(in_ready),
      .in_full(in_full),
      .in_fifo(in_fifo),
      .error_valid(rx_error_valid),
      .error_code(rx_error_code)
  );

  // Data send module
  spi_tx tx (
      .clk(clk),
      .ck_sck(ck_sck),
      .reset(reset),
      .reset_s(reset_s),
      .ck_ss(ck_ss),
      .ck_ss_s(ck_ss_s),
      .tx_r_en(tx_r_en),
      .in_empty(in_empty),
      .out_w_en(out_w_en),
      .state(state),
      .out_fifo(out_fifo),
      .octo_spi(octo_spi),
      .out_empty(out_empty),
      .out_full(out_full),
      .out_ready(out_ready),
      .out_empty_s(out_empty_s),
      .error_valid(tx_error_valid),
      .error_code(tx_error_code)
  );

  // Command processing module
  spi_processor processor (
      .clk(clk),
      .state(state),
      .in_fifo(in_fifo),
      .out_full(out_full),
      .in_empty(in_empty),
      .reset_s(reset_s),
      .proc_w_en(proc_w_en),
      .proc_r_en(proc_r_en),
      .out_fifo(out_fifo),
      .opcode(opcode),
      .proc_done(proc_done),
      .mem_in(mem_in),
      .mem_out(mem_out),
      .mem_len(mem_len),
      .mem_addr(mem_addr),
      .mem_transaction(mem_transaction),
      .mem_busy(mem_busy),
      .mem_data_on_wire(mem_data_on_wire),
      .mem_toggle(mem_toggle),
      .sprite_available_id(sprite_available_id),
      .sprite_id_found(sprite_id_found),
      .sprite_req_valid(sprite_req_valid),
      .sprite_req_body(sprite_req_body),
      .sprite_req_op(sprite_req_op),
      .write_sprite_valid(write_sprite_valid),
      .error_valid(processor_error_valid),
      .error_code(processor_error_code)

  );

  // DEBUG
  assign led[0] = ready;
  assign led[1] = ~in_empty;
  assign led[2] = ~out_empty;
  assign led[3] = (state == ST_FAILED);

  assign led0_r = state[2];
  assign led0_g = state[1];
  assign led0_b = state[0];

endmodule
