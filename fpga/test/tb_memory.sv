/* verilator lint_off PINMISSING */
/* verilator lint_off MULTITOP */

module tb_memory #(
    parameter string DUMPFILEPATH = ""  // this parameter is overwritten when compiling with make
) (
    output logic clk,
    output logic out_clk,
    output logic out_clk_n,
    output logic cs_n,
    output logic reset_n,
    inout wire [7:0] dq,
    inout wire rwds
);
  initial begin
    $dumpfile(DUMPFILEPATH);
    $dumpvars;
    clk = 0;
    reset = 1;
    enable = 1;
    client_toggle = '0;
    client_allow = '1;
    for (int unsigned i = 0; i < memory_pkg::NumMemClients; i++) begin
      client_transaction[i] = memory_pkg::NONE;
      client_address[i] = '0;
      client_length[i] = '0;
      client_write_data[i] = '0;
    end

    repeat (2) begin
      #1 clk = 1;
      #1 clk = 0;
    end
    reset = 0;
    for (int i = 0; i < 1000; i++) begin
      #1 clk = 1;
      #1 clk = 0;
    end
    if (!memory_module.initializer_finished || command_count != 6)
      $fatal(1, "Expected two initialization commands, saw %0d", command_count);
    $finish;
  end

  int command_count = 0;
  always @(negedge cs_n) if (!reset) command_count++;

  logic reset, enable;
  logic [7:0] read_data;

  // `memory` now fronts a fixed-priority arbiter rather than a single client
  // port. This testbench only exercises memory_initializer, which outranks the
  // arbiter entirely, so every client slot stays idle -- an idle slot never
  // inverts its toggle and is never granted.
  logic [memory_pkg::NumMemClients-1:0] client_toggle;
  memory_pkg::transaction_e client_transaction[memory_pkg::NumMemClients];
  memory_pkg::address_t client_address[memory_pkg::NumMemClients];
  memory_pkg::message_length_t client_length[memory_pkg::NumMemClients];
  logic [7:0] client_write_data[memory_pkg::NumMemClients];
  logic [memory_pkg::NumMemClients-1:0] client_allow;
  logic [memory_pkg::NumMemClients-1:0] client_busy, client_data_on_cable, client_grant;

  memory memory_module (
      .clk,
      .reset,
      .enable,

      .client_toggle,
      .client_next_transaction(client_transaction),
      .client_address,
      .client_length,
      .client_write_data,
      .client_allow,

      // Info to fpga
      .client_busy,
      .client_data_on_cable,
      .client_grant,
      .read_data,

      // Memory pins
      .out_clk,
      .out_clk_n,
      .cs_n,
      .reset_n,
      .dq,
      .rwds
  );

endmodule

