module spi #(
    parameter bit DEBUG = 1'd1
) (
    input wire clk,
    input wire ck_ss,
    input wire ck_sck,
    input wire ck_rst,

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
    output wire in_full
);

  import spi_pck::*;

  // STATE / CMD TRACKERS
  spi_pck::state_e state;
  spi_pck::opcode_e opcode;

  // RESET
  reg rst_en = 1'b0;  // programatic reset
  wire reset = ~ck_rst || rst_en;
  wire reset_s = ~ck_rst_s || rst_en;

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

  assign in_r_en = proc_r_en | rx_r_en | tx_r_en;
  assign out_w_en = proc_w_en;

  // CDC synchronizations CK_SCK -> CLK
  reg [2:0] ck_ss_sync = 3'b000;
  reg [2:0] ck_rst_sync = 3'b000;
  reg [2:0] out_empty_sync = 3'b000;

  wire ck_ss_s = ck_ss_sync[2];
  wire ck_rst_s = ck_rst_sync[2];
  wire out_empty_s = out_empty_sync[2];

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
    .state(state)
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
    .in_fifo(in_fifo)
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
    .out_empty_s(out_empty_s)
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
    .opcode(opcode)
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
