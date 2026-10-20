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

