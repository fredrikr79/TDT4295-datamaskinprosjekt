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
  logic enable = 1, reset = 0;
  logic busy, data_on_cable;
  memory_pkg::transaction_e next_transaction;
  memory_pkg::address_t address;
  memory_pkg::message_length_t length;
  logic [7:0] write_data, read_data;
  logic transaction_toggle = 0;

  memory_controller dut (
      .clk,
      .reset,
      .enable,
      .out_clk,
      .out_clk_n,
      .cs_n,
      .reset_n,
      .dq,
      .rwds,
      .next_transaction,
      .transaction_toggle,
      .address,
      .length,
      .write_data,
      .read_data,
      .data_on_cable,
      .busy
  );
  initial begin
    for (int i = 0; i < 500; i = i + 1) begin
      clk = 0;
      #1;
      clk = 1;
      #1;
    end
    $finish;
  end

  logic read_active = 0;
  logic [7:0] dq_in;
  logic [15:0] register_data;
  assign dq = read_active ? dq_in : 8'hZZ;
  always_ff @(posedge clk) begin
    if (dut.current_state == memory_pkg::READING)
      register_data <= {register_data[7:0], read_data};
  end
  initial begin
    if (DUMPFILEPATH != "") begin
      $dumpfile(DUMPFILEPATH);
      $dumpvars(0);
    end

    // First write
    wait (dut.current_state == memory_pkg::IDLE);
    next_transaction = memory_pkg::WRITE;
    transaction_toggle = ~transaction_toggle;
    address = 'h12345678;
    length = 2;
    write_data = 8'hAA;
    wait (dut.current_state == memory_pkg::WRITING);
    write_data = 8'hBB;
    wait (dut.current_state == memory_pkg::IDLE);

    // First read
    next_transaction = memory_pkg::READ;
    transaction_toggle = ~transaction_toggle;
    wait (dut.current_state == memory_pkg::READING);
    dq_in = 'hCC;
    read_active = 1;
    @(negedge clk);
    dq_in = 'hDD;
    wait (dut.current_state == memory_pkg::IDLE);
    read_active = 0;
    if (register_data !== 16'hCCDD) $fatal(1, "First register read: %h", register_data);

    // Second read
    next_transaction = memory_pkg::READ;
    transaction_toggle = ~transaction_toggle;
    wait (dut.current_state == memory_pkg::READING);
    dq_in = 'hEE;
    read_active = 1;
    @(negedge clk);
    dq_in = 'hFF;
    wait (dut.current_state == memory_pkg::IDLE);
    read_active = 0;
    if (register_data !== 16'hEEFF) $fatal(1, "Second register read: %h", register_data);
    $finish;
  end
endmodule

