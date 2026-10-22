`timescale 1ns/1ps

module tb_read_box;
  import config_pkg::*;
  import mem_driver_pkg::*;
  import memory_pkg::*;

  // cols and rows are treated as inclusive maximum offsets by this test:
  // COL_MAX=2 => 3 bytes per row; ROW_MAX=1 => 2 rows.
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
  logic send_read_box = 1'b0;
  logic mem_busy = 1'b1;
  logic [7:0] in_fifo, mem_out = 8'h00, read_box_fifo, mem_len;
  logic read_box_done, read_box_w_en, read_box_r_en, mem_req;
  logic [31:0] mem_addr;

  logic [7:0] stream [0:HEADER_LEN-1];
  logic [7:0] response_bytes [0:TOTAL_BYTES-1];
  logic [7:0] received [0:TOTAL_BYTES-1];
  logic [31:0] req_addr [0:BOX_HEIGHT-1];
  logic [7:0] req_len [0:BOX_HEIGHT-1];
  integer fifo_ptr = 0;
  integer received_count = 0;
  integer req_count = 0;
  integer cycles = 0;
  logic [31:0] expected_addr;

  // FWFT FIFO model: in_fifo shows the current head. A registered read-enable
  // consumes that head on the rising edge where the old enable is high.
  always_comb in_fifo = (fifo_ptr < HEADER_LEN) ? stream[fifo_ptr] : 8'hEE;

  always @(posedge clk) begin
    if (ck_rst)
      fifo_ptr <= 0;
    else if (read_box_r_en && fifo_ptr < HEADER_LEN)
      fifo_ptr <= fifo_ptr + 1;
  end

  read_box dut (
    .clk(clk), .ck_rst(ck_rst), .mem_data_on_wire(mem_data_on_wire),
    .send_read_box(send_read_box), .mem_busy(mem_busy), .mem_out(mem_out),
    .in_fifo(in_fifo), .read_box_fifo(read_box_fifo), .mem_len(mem_len),
    .read_box_done(read_box_done), .read_box_w_en(read_box_w_en),
    .read_box_r_en(read_box_r_en), .mem_req(mem_req), .mem_addr(mem_addr)
  );

  // Sample registered DUT outputs after their nonblocking assignments settle.
  always @(posedge clk) begin
    #1;
    if (!ck_rst) begin
      if (mem_req) begin
        if (req_count >= BOX_HEIGHT)
          $fatal(1, "READ_BOX generated too many row requests");
        req_addr[req_count] = mem_addr;
        req_len[req_count] = mem_len;
        req_count = req_count + 1;
      end
      if (read_box_w_en) begin
        if (received_count >= TOTAL_BYTES)
          $fatal(1, "READ_BOX produced too many bytes");
        received[received_count] = read_box_fifo;
        received_count = received_count + 1;
      end
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
        $fatal(1, "READ_BOX timed out waiting for request %0d", target);
    end
  endtask

  // Keep data_on_wire high continuously while this row's response bytes are
  // available. Change mem_out once per byte; lower valid only after the row.
  task automatic drive_response_row(input integer first_byte);
    integer i;
    begin
      for (i = 0; i < ROW_BYTES; i = i + 1) begin
        @(negedge clk);
        mem_out = response_bytes[first_byte + i];
        mem_data_on_wire = 1'b1;
      end
      // The last byte is sampled on the next rising edge.
      @(negedge clk);
      mem_data_on_wire = 1'b0;
    end
  endtask

  initial begin
    stream[0] = 8'h00; stream[1] = 8'h01; // X = 1
    stream[2] = 8'h00; stream[3] = 8'h02; // Y = 2
    stream[4] = 8'h00; stream[5] = BOX_WIDTH[7:0];
    stream[6] = 8'h00; stream[7] = BOX_HEIGHT[7:0];

    response_bytes[0] = 8'hA0;
    response_bytes[1] = 8'hA1;
    response_bytes[2] = 8'hA2;
    response_bytes[3] = 8'hB0;

    // Assert active-high reset, then deassert it for normal operation.
    ck_rst = 1'b1;
    repeat (3) @(negedge clk);
    ck_rst = 1'b0;
    send_read_box = 1'b1;

    // Hold the memory busy until the header has been consumed and the handler
    // is waiting to issue its first row request.
    repeat (10) @(negedge clk);
    mem_busy = 1'b0;

    wait_requests(1);
    drive_response_row(0);

    wait_requests(2);
    drive_response_row(ROW_BYTES);

    cycles = 0;
    while (!read_box_done && cycles < 60) begin
      @(negedge clk);
      cycles = cycles + 1;
    end
    if (!read_box_done)
      $fatal(1, "READ_BOX timed out waiting for done");

    send_read_box = 1'b0;
    mem_data_on_wire = 1'b0;
    repeat (1) @(negedge clk);

    if (req_count != BOX_HEIGHT)
      $fatal(1, "READ_BOX expected %0d row requests, got %0d", BOX_HEIGHT, req_count);

    if (req_addr[0] !== expected_addr)
      $fatal(1, "READ_BOX row 0 address %08h expected %08h", req_addr[0], expected_addr);
    expected_addr = GridPosToMemAddr(16'h0001, 16'h0003);
    if (req_addr[1] !== expected_addr)
      $fatal(1, "READ_BOX row 1 address %08h expected %08h", req_addr[1], expected_addr);

    if (req_len[0] !== ROW_BYTES[7:0] || req_len[1] !== ROW_BYTES[7:0])
      $fatal(1, "READ_BOX row length should be %0d for inclusive cols=%0d; saw %0d and %0d",
             ROW_BYTES, BOX_WIDTH, req_len[0], req_len[1]);

    if (received_count != TOTAL_BYTES)
      $fatal(1, "READ_BOX response count %0d expected %0d", received_count, TOTAL_BYTES);
    for (int j = 0; j < TOTAL_BYTES; j++) begin
      if (received[j] !== response_bytes[j])
        $fatal(1, "READ_BOX byte %0d got %02h expected %02h", j, received[j], response_bytes[j]);
    end

    $display("PASS tb_read_box");
    $finish;
  end
endmodule
