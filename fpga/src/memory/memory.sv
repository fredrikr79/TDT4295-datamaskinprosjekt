module memory (
    // input from fpga
    input logic clk,
    input logic reset,
    input logic enable,
    input logic transaction_toggle,

    input memory_pkg::transaction_e next_transaction,
    input memory_pkg::address_t address,
    input memory_pkg::message_length_t length,
    input logic [7:0] write_data,

    // Info to fpga
    output logic busy,
    data_on_cable,
    output logic [7:0] read_data,

    // Memory pins
    output logic out_clk,
    output logic out_clk_n,
    output logic cs_n,
    output logic reset_n,
    inout wire [7:0] dq,
    inout wire rwds
);

  memory_controller controller (
      .clk,
      .reset,
      .enable,
      .transaction_toggle,
      .next_transaction(controller_next_transaction),
      .address(controller_address),
      .length(controller_length),
      .write_data(controller_write_data),
      .busy(controller_busy),
      .data_on_cable(controller_data_on_cable),
      .read_data(controller_read_data),
      .out_clk,
      .out_clk_n,
      .reset_n,
      .dq,
      .rwds
  );

  assign busy = initializer_finished ? controller_busy : 1;
  assign data_on_cable = initializer_finished ? controller_data_on_cable : 0;
  assign read_data = initializer_finished ? controller_read_data : 'h00;

  assign controller_next_transaction = initializer_finished ? next_transaction
                                                            : initializer_next_transaction;
  assign controller_address = initializer_finished ? address : initializer_address;
  assign controller_length = initializer_finished ? length : initializer_length;
  assign controller_write_data = initializer_finished ? write_data : initializer_write_data;

  memory_initializer initilizer (
      .clk,
      .reset,
      .enable,
      .controller_busy,
      .controller_data_on_wire,
      .finished(intializer_finished),
      .controller_toggle,
      .next_transaction(initializer_next_transaction),
      .next_address,
      .next_length
  );

endmodule
