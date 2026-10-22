`timescale 1ns/1ps

module tb_read_cons;
  import config_pkg::*;
  import mem_driver_pkg::*;
  import memory_pkg::*;

  localparam int HEADER_LEN = 5;
  localparam int READ_LEN = 2;
  localparam int STREAM_LEN = HEADER_LEN;

  logic clk = 1'b0;
  always #5 clk = ~clk;
  logic ck_rst = 1'b0;
  logic mem_data_on_wire = 1'b0;
  logic send_read_cons = 1'b0;
  logic mem_busy = 1'b1;
  logic [7:0] in_fifo, mem_out;
  logic [7:0] read_cons_fifo, mem_len;
  logic read_cons_done, read_cons_w_en, read_cons_r_en, mem_req;
  logic [31:0] mem_addr;

  logic [7:0] stream [0:STREAM_LEN-1];
  logic [7:0] response_bytes [0:READ_LEN-1];
  logic [7:0] received [0:READ_LEN-1];
  integer fifo_ptr = 0;
  integer received_count = 0;
  integer req_count = 0;
  integer cycles = 0;
  logic [31:0] expected_addr;

  always_comb in_fifo = (fifo_ptr < STREAM_LEN) ? stream[fifo_ptr] : 8'hEE;

  always @(posedge clk) begin
    if (ck_rst) fifo_ptr <= 0;
    else if (read_cons_r_en && fifo_ptr < STREAM_LEN)
      fifo_ptr <= fifo_ptr + 1;
  end

  read_cons dut (
    .clk(clk), .ck_rst(ck_rst), .mem_data_on_wire(mem_data_on_wire),
    .send_read_cons(send_read_cons), .mem_busy(mem_busy), .mem_out(mem_out),
    .in_fifo(in_fifo), .read_cons_fifo(read_cons_fifo), .mem_len(mem_len),
    .read_cons_done(read_cons_done), .read_cons_w_en(read_cons_w_en),
    .read_cons_r_en(read_cons_r_en), .mem_req(mem_req), .mem_addr(mem_addr)
  );

  always @(posedge clk) begin
    #1;
    if (!ck_rst) begin
      if (mem_req) req_count = req_count + 1;
      if (read_cons_w_en) begin
        if (received_count >= READ_LEN) $fatal(1, "READ_CONS produced too many bytes");
        received[received_count] = read_cons_fifo;
        received_count = received_count + 1;
      end
    end
  end

  task automatic pulse_memory_byte(input logic [7:0] value);
    begin
      @(negedge clk); mem_out = value; mem_data_on_wire = 1'b1;
      @(negedge clk); mem_data_on_wire = 1'b0;
      @(negedge clk);
    end
  endtask

  initial begin
    stream[0] = 8'h00; stream[1] = 8'h0A;
    stream[2] = 8'h00; stream[3] = 8'h14;
    stream[4] = READ_LEN[7:0];
    response_bytes[0] = 8'hA1; response_bytes[1] = 8'hB2;
    mem_out = 8'h00;

    ck_rst = 1'b1;
    repeat (3) @(negedge clk);

    ck_rst = 1'b0;  // Release reset
    send_read_cons = 1'b1;
    repeat (10) @(negedge clk);
    mem_busy = 1'b0;

    while (req_count == 0 && cycles < 30) begin
      @(negedge clk); cycles = cycles + 1;
    end
    if (req_count != 1) $fatal(1, "READ_CONS expected one mem_req pulse, got %0d", req_count);

    // Present the first response byte and indicate valid data.
    @(negedge clk);
    mem_out = response_bytes[0];  // A1
    mem_data_on_wire = 1'b1;

    // Present the second byte on the next cycle.
    // Keep data_on_wire high because data remains available.
    @(negedge clk);
    mem_out = response_bytes[1];  // B2

    // Both bytes have now been presented to the DUT.
    @(negedge clk);
    mem_data_on_wire = 1'b0;

    // Allow the handler to finish.
    cycles = 0;
    while (!read_cons_done && cycles < 30) begin
      @(negedge clk);
      cycles = cycles + 1;
    end

    if (!read_cons_done)
      $fatal(1, "READ_CONS timed out");

    send_read_cons = 1'b0;
    repeat (1) @(negedge clk);

    expected_addr = GridPosToMemAddr(16'h000A, 16'h0014);
    if (mem_addr !== expected_addr)
      $fatal(1, "READ_CONS address %08h, expected %08h", mem_addr, expected_addr);
    if (mem_len !== READ_LEN[7:0]) $fatal(1, "READ_CONS length mismatch");
    if (received_count != READ_LEN)
      $fatal(1, "READ_CONS received %0d bytes, expected %0d", received_count, READ_LEN);
    if (received[0] !== 8'hA1 || received[1] !== 8'hB2)
      $fatal(1, "READ_CONS response mismatch: %02h, %02h", received[0], received[1]);
    if (req_count != 1) $fatal(1, "READ_CONS mem_req was not one pulse");

    $display("PASS tb_read_cons");
    $finish;
  end
endmodule
