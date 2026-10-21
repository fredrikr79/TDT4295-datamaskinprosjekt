`timescale 1ns/1ps

module tb_write_box;
  import config_pkg::*;
  import mem_driver_pkg::*;
  import memory_pkg::*;

  // cols and rows are inclusive maximum offsets:
  // COL_MAX=2 => 3 payload bytes per row; ROW_MAX=1 => 2 rows.
  localparam int HEADER_LEN = 8;
  localparam int BOX_WIDTH  = 2;
  localparam int BOX_HEIGHT = 2;

  localparam int ROW_BYTES  = BOX_WIDTH;
  localparam int TOTAL_BYTES = BOX_WIDTH * BOX_HEIGHT;
  localparam int STREAM_LEN = HEADER_LEN + TOTAL_BYTES;
  logic clk = 1'b0;
  always #5 clk = ~clk;

  logic ck_rst = 1'b1; // Active-high reset: 1 asserts reset.
  logic mem_data_on_wire = 1'b0;
  logic send_write_box = 1'b0;
  logic mem_busy = 1'b0;
  logic [7:0] in_fifo, mem_in, mem_len;
  logic write_box_done, write_box_r_en, mem_req;
  logic [31:0] mem_addr;

  logic [7:0] stream [0:STREAM_LEN-1];
  logic [7:0] payload_seen [0:TOTAL_BYTES-1];
  logic [31:0] req_addr [0:BOX_HEIGHT-1];
  logic [7:0] req_len [0:BOX_HEIGHT-1];
  integer fifo_ptr = 0;
  integer payload_count = 0;
  integer req_count = 0;
  integer cycles = 0;
  logic [31:0] expected_addr;

  // FWFT FIFO model: data at in_fifo is the current head; the previously
  // registered write_box_r_en pops it on this rising edge.
  always_comb in_fifo = (fifo_ptr < STREAM_LEN) ? stream[fifo_ptr] : 8'hEE;

  always @(posedge clk) begin
    if (ck_rst)
      fifo_ptr <= 0;
    else if (write_box_r_en && fifo_ptr < STREAM_LEN)
      fifo_ptr <= fifo_ptr + 1;
  end

  write_box dut (
    .clk(clk), .ck_rst(ck_rst), .mem_data_on_wire(mem_data_on_wire),
    .send_write_box(send_write_box), .mem_busy(mem_busy), .in_fifo(in_fifo),
    .mem_in(mem_in), .mem_len(mem_len), .write_box_done(write_box_done),
    .write_box_r_en(write_box_r_en), .mem_req(mem_req), .mem_addr(mem_addr)
  );

  // Record row requests after registered outputs have settled.
  always @(posedge clk) begin
    #1;
    if (!ck_rst && mem_req) begin
     if (req_count >= BOX_HEIGHT)
        $fatal(1, "WRITE_BOX generated too many row requests");

      req_addr[req_count] = mem_addr;
      req_len[req_count] = mem_len;
      req_count = req_count + 1;
    end
  end

  // A byte is consumed on an edge where the pre-edge registered read enable
  // is high. Sample mem_in after NBA updates so it corresponds to that pop.
  always @(posedge clk) begin
    if (!ck_rst && write_box_r_en && fifo_ptr >= HEADER_LEN) begin
      #1;
      if (payload_count >= TOTAL_BYTES)
        $fatal(1, "WRITE_BOX transferred too many payload bytes");
      payload_seen[payload_count] = mem_in;
      payload_count = payload_count + 1;
    end
  end

  task automatic wait_requests(input integer target);
    integer timeout_count;
    begin
      timeout_count = 0;
      while (req_count < target && timeout_count < 60) begin
        @(negedge clk);
        timeout_count = timeout_count + 1;
      end
      if (req_count < target)
        $fatal(1, "WRITE_BOX timed out waiting for request %0d", target);
    end
  endtask

  initial begin
    stream[0] = 8'h00; stream[1] = 8'h01; // X = 1
    stream[2] = 8'h00; stream[3] = 8'h02; // Y = 2

    stream[4] = 8'h00; stream[5] = BOX_WIDTH;
    stream[6] = 8'h00; stream[7] = BOX_HEIGHT;

    stream[8]  = 8'h10; stream[9]  = 8'h11;
    stream[10] = 8'h20; stream[11] = 8'h21;

    // Assert active-high reset, then deassert it for normal operation.
    ck_rst = 1'b1;
    repeat (3) @(negedge clk);
    ck_rst = 1'b0;
    send_write_box = 1'b1;

    wait_requests(1);

    // Data remains continuously available. Do not pulse this once per byte.
    @(negedge clk);
    mem_data_on_wire = 1'b1;

    // Wait until all payload pops have been observed. The same high valid
    // level spans byte transfers and the transition to the next row request.
    cycles = 0;
    while (payload_count < TOTAL_BYTES && cycles < 80) begin
      @(negedge clk);
      cycles = cycles + 1;
    end
    if (payload_count != TOTAL_BYTES)
      $fatal(1, "WRITE_BOX transferred %0d payload bytes, expected %0d",
             payload_count, TOTAL_BYTES);

    mem_data_on_wire = 1'b0;
    wait_requests(2);

    cycles = 0;
    while (!write_box_done && cycles < 40) begin
      @(negedge clk);
      cycles = cycles + 1;
    end
    if (!write_box_done)
      $fatal(1, "WRITE_BOX timed out waiting for done");

    send_write_box = 1'b0;
    repeat (1) @(negedge clk);

    if (req_count != BOX_HEIGHT)
      $fatal(1, "Expected %0d row requests, got %0d",
            BOX_HEIGHT, req_count);

    for (int r = 0; r < BOX_HEIGHT; r++) begin
      expected_addr = GridPosToMemAddr(16'h0001, 16'h0002 + r);

      if (req_addr[r] !== expected_addr)
        $fatal(1, "Row %0d address mismatch", r);

      if (req_len[r] !== BOX_WIDTH)
        $fatal(1, "Row %0d length %0d, expected %0d",
              r, req_len[r], BOX_WIDTH);
    end

    $display("PASS tb_write_box");
    $finish;
  end
endmodule
