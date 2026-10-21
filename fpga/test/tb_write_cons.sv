`timescale 1ns/1ps

module tb_write_cons;
  import config_pkg::*;
  import mem_driver_pkg::*;
  import memory_pkg::*;

  localparam int HEADER_LEN = 5;
  localparam int PAYLOAD_LEN = 4;
  localparam int STREAM_LEN = HEADER_LEN + PAYLOAD_LEN;

  logic clk = 1'b0;
  always #5 clk = ~clk;
  logic ck_rst = 1'b0; // Handler reset is active-low in the supplied interface.
  logic mem_data_on_wire = 1'b0;
  logic send_write_cons = 1'b0;
  logic mem_busy = 1'b1;
  logic [7:0] in_fifo;
  logic [7:0] mem_len, mem_in;
  logic write_cons_done, write_cons_r_en, mem_req;
  logic [31:0] mem_addr;

  logic [7:0] stream [0:STREAM_LEN-1];
  logic [7:0] payload_seen [0:PAYLOAD_LEN-1];
  integer fifo_ptr = 0;
  integer payload_count = 0;
  integer req_count = 0;
  integer cycles = 0;
  logic [31:0] expected_addr;

  always_comb in_fifo = (fifo_ptr < STREAM_LEN) ? stream[fifo_ptr] : 8'hEE;

  // FWFT model: pop on the rising edge where the previously registered r_en is high.
  always @(posedge clk) begin
    if (ck_rst) fifo_ptr <= 0;
    else if (write_cons_r_en && fifo_ptr < STREAM_LEN)
      fifo_ptr <= fifo_ptr + 1;
  end

  write_cons dut (
    .clk(clk), .ck_rst(ck_rst), .mem_data_on_wire(mem_data_on_wire),
    .send_write_cons(send_write_cons), .mem_busy(mem_busy), .in_fifo(in_fifo),
    .mem_len(mem_len), .mem_in(mem_in), .write_cons_done(write_cons_done),
    .write_cons_r_en(write_cons_r_en), .mem_req(mem_req), .mem_addr(mem_addr)
  );

  always @(posedge clk) begin
    #1;
    if (!ck_rst) begin
      if (mem_req) req_count = req_count + 1;
      if (mem_data_on_wire && !mem_busy && write_cons_r_en && fifo_ptr >= HEADER_LEN) begin
        if (payload_count >= PAYLOAD_LEN) $fatal(1, "WRITE_CONS emitted too many payload samples");
        payload_seen[payload_count] = mem_in;
        payload_count = payload_count + 1;
      end
    end
  end

  task automatic pulse_data_ready;
    begin
      @(negedge clk); mem_data_on_wire = 1'b1;
      @(negedge clk); mem_data_on_wire = 1'b0;
      @(negedge clk); // allow the registered read enable to pop the FWFT head
    end
  endtask

  initial begin
    stream[0] = 8'h00; stream[1] = 8'h0A; // X = 10
    stream[2] = 8'h00; stream[3] = 8'h14; // Y = 20
    stream[4] = PAYLOAD_LEN[7:0];
    stream[5] = 8'hAB; stream[6] = 8'hCD;
    stream[7] = 8'hEE; stream[8] = 8'hFF;

    ck_rst = 1'b1;
    repeat (3) @(negedge clk);

    ck_rst = 1'b0;  // Release reset
    send_write_cons = 1'b1;
    // Keep memory busy while the request state is reached.
    repeat (10) @(negedge clk);
    mem_busy = 1'b0;

    while (req_count == 0 && cycles < 30) begin
      @(negedge clk); cycles = cycles + 1;
    end
    if (req_count != 1) $fatal(1, "WRITE_CONS expected one mem_req pulse, got %0d", req_count);

    // Payload is available continuously, so hold data_on_wire high.
    @(negedge clk);
    mem_data_on_wire = 1'b1;

    cycles = 0;
    while (!write_cons_done && cycles < 30) begin
      @(negedge clk);
      cycles = cycles + 1;
    end

    if (!write_cons_done)
      $fatal(1, "WRITE_CONS timed out");

    // No more payload data is available after completion.
    mem_data_on_wire = 1'b0;
    send_write_cons = 1'b0;
    repeat (1) @(negedge clk);


    expected_addr = GridPosToMemAddr(16'h000A, 16'h0014);
    if (mem_addr !== expected_addr)
      $fatal(1, "WRITE_CONS address %08h, expected %08h", mem_addr, expected_addr);
    if (mem_len !== PAYLOAD_LEN[7:0])
      $fatal(1, "WRITE_CONS length %0d, expected %0d", mem_len, PAYLOAD_LEN);
    if (payload_count != PAYLOAD_LEN)
      $fatal(1, "WRITE_CONS payload samples %0d, expected %0d", payload_count, PAYLOAD_LEN);
    if (payload_seen[0] !== 8'hDE || payload_seen[1] !== 8'hAD)
      $fatal(1, "WRITE_CONS payload mismatch: %02h, %02h", payload_seen[0], payload_seen[1]);
    if (req_count != 1) $fatal(1, "WRITE_CONS mem_req was not a single pulse");

    $display("PASS tb_write_cons");
    $finish;
  end
endmodule
