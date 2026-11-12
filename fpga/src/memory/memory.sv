module memory #(
    parameter int unsigned StartupDelayCycles = (config_pkg::GlobalClockFrequency / 1000) * 300
) (
    // input from fpga
    input logic clk,
    input logic reset,
    input logic enable,
    input logic start_phase,
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
    output logic run_clock,
    output logic cs_n,
    output logic reset_n,
    input wire [7:0] dq_in,
    output logic [7:0] dq_out,
    output logic dq_oe,
    input wire rwds_in,
    output logic rwds_out,
    output logic rwds_oe
);

  logic initializer_finished, controller_busy, controller_data_on_cable;
  logic [7:0] controller_read_data, initializer_write_data, controller_write_data;
  memory_pkg::transaction_e controller_next_transaction, initializer_next_transaction;
  logic [31:0] controller_address, initializer_address;
  memory_pkg::message_length_t initializer_length, controller_length;
  assign busy = initializer_finished ? controller_busy : 1;
  // xSPI DDR transfers one byte on each edge of the memory clock.
  assign data_on_cable = initializer_finished ? controller_data_on_cable : 0;
  assign read_data = initializer_finished ? controller_read_data : 'h00;

  assign controller_next_transaction = initializer_finished ? next_transaction
                                                            : initializer_next_transaction;
  assign controller_address = initializer_finished ? address : initializer_address;
  assign controller_length = initializer_finished ? length : initializer_length;
  assign controller_write_data = initializer_finished ? write_data : initializer_write_data;

  logic initializer_toggle, controller_toggle;
  logic toggle_offset;
  always_ff @(posedge clk) begin
    if (reset)                     toggle_offset <= 1'b0;
    else if (!initializer_finished) toggle_offset <= initializer_toggle ^ transaction_toggle;
  end

  assign controller_toggle = initializer_finished ? (transaction_toggle ^ toggle_offset)
                                                  : initializer_toggle;

  memory_controller controller (
      .clk,
      .reset,
      .enable,
      .start_phase,
      .transaction_toggle(controller_toggle),
      .next_transaction(controller_next_transaction),
      .address(controller_address),
      .length(controller_length),
      .write_data(controller_write_data),
      .busy(controller_busy),
      .run_clock(run_clock),
      .data_on_cable(controller_data_on_cable),
      .read_data(controller_read_data),
      .reset_n,
      .dq_in,
      .dq_out,
      .dq_oe,
      .cs_n,
      .rwds_in,
      .rwds_out,
      .rwds_oe
  );


  memory_initializer #(
      .StartupDelayCycles(StartupDelayCycles)
  ) initilizer (
      .clk,
      .reset,
      .enable,
      .controller_busy,
      .controller_data_on_cable,
      .finished(initializer_finished),
      .controller_toggle(initializer_toggle),
      .next_transaction(initializer_next_transaction),
      .next_address(initializer_address),
      .next_length(initializer_length),
      .write(initializer_write_data)
  );

endmodule
