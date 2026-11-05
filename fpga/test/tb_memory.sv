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
    transaction_toggle = 0;
    next_transaction = memory_pkg::NONE;
    address = 0;
    length = 0;
    write_data = 0;

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

  logic reset, enable, transaction_toggle;
  memory_pkg::transaction_e next_transaction;
  logic [31:0] address;
  memory_pkg::message_length_t length;
  logic [7:0] write_data, read_data;
  logic busy, data_on_cable;
  memory memory_module (
      .clk,
      .reset,
      .enable,
      .transaction_toggle,

      .next_transaction,
      .address,
      .length,
      .write_data,

      // Info to fpga
      .busy,
      .data_on_cable,
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

