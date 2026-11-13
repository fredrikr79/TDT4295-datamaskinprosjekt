/* verilator lint_off PINMISSING */
/* verilator lint_off MULTITOP */

module tb_read_cons #(
  // Derived from the burst length so the CHUNKING path stays covered -- a flat
  // 25 was 2 chunks at MaxMessageLength = 20 but a single transaction at 64.
  // See the note in tb_write_cons.
  parameter int READ_LEN = 2 * memory_pkg::MaxMessageLength + 5
);
  import config_pkg::*;
  import mem_driver_pkg::*;
  import memory_pkg::*;

  localparam int HEADER_LEN = 6;
  localparam int STREAM_LEN = HEADER_LEN;
  localparam int CHUNK_COUNT = (READ_LEN + memory_pkg::MaxMessageLength - 1)
                             / memory_pkg::MaxMessageLength;

  logic clk = 1'b0;
  always #5 clk = ~clk;
  logic ck_rst = 1'b0;
  logic mem_data_on_wire = 1'b0;
  logic send_read_cons = 1'b0;
  logic mem_busy = 1'b1;
  logic [7:0] in_fifo, mem_out, error_code;
  logic [7:0] read_cons_fifo;
  logic [15:0] mem_len;
  logic read_cons_done, read_cons_w_en, read_cons_r_en, mem_req, in_empty, error_valid;
  logic [31:0] mem_addr;

  logic [7:0] stream [0:STREAM_LEN-1];
  logic [7:0] response_bytes [0:READ_LEN-1];
  logic [7:0] received [0:READ_LEN-1];
  logic [31:0] request_addresses [0:CHUNK_COUNT-1];
  logic [15:0] request_lengths [0:CHUNK_COUNT-1];
  integer fifo_ptr = 0;
  integer received_count = 0;
  integer req_count = 0;
  integer cycles = 0;

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
    .read_cons_r_en(read_cons_r_en), .mem_req(mem_req), .mem_addr(mem_addr), .in_empty(in_empty),
    .error_code(error_code), .error_valid(error_valid)
  );

  always @(posedge clk) begin
    logic sampled_write_enable;
    sampled_write_enable = read_cons_w_en;
    #1;
    if (!ck_rst) begin
      if (mem_req) begin
        if (req_count >= CHUNK_COUNT) $fatal(1, "READ_CONS issued too many requests");
        request_addresses[req_count] = mem_addr;
        request_lengths[req_count] = mem_len;
        req_count = req_count + 1;
      end
      if (sampled_write_enable) begin
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
    stream[4] = 8'h00;
    stream[5] = 8'(READ_LEN);
    for (int i = 0; i < READ_LEN; i++) response_bytes[i] = 8'(8'hA0 + i);
    mem_out = 8'h00;

    ck_rst = 1'b1;
    repeat (3) @(negedge clk);

    ck_rst = 1'b0;  // Release reset
    send_read_cons = 1'b1;
    repeat (10) @(negedge clk);
    mem_busy = 1'b0;

    while (req_count < 1 && cycles < 30) begin
      @(negedge clk); cycles = cycles + 1;
    end
    if (req_count < 1) $fatal(1, "READ_CONS timed out waiting for request 1");

    for (int i = 0; i < READ_LEN; i++) begin
      if (i > 0 && i % memory_pkg::MaxMessageLength == 0) begin
        cycles = 0;
        while (req_count < (i / memory_pkg::MaxMessageLength) + 1 && cycles < 30) begin
          @(negedge clk); cycles = cycles + 1;
        end
        if (req_count < (i / memory_pkg::MaxMessageLength) + 1)
          $fatal(1, "READ_CONS timed out waiting for next chunk request");
      end
      pulse_memory_byte(response_bytes[i]);
    end

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

    if (req_count != CHUNK_COUNT)
      $fatal(1, "READ_CONS request count %0d expected %0d", req_count, CHUNK_COUNT);
    for (int chunk = 0; chunk < CHUNK_COUNT; chunk++) begin
      int chunk_offset;
      int expected_chunk_len;
      chunk_offset = chunk * memory_pkg::MaxMessageLength;
      expected_chunk_len = (READ_LEN - chunk_offset > memory_pkg::MaxMessageLength)
                         ? memory_pkg::MaxMessageLength
                         : READ_LEN - chunk_offset;
      if (request_addresses[chunk] !==
          GridPosToMemAddr(16'h000A + 16'(chunk_offset), 16'h0014))
        $fatal(1, "READ_CONS chunk %0d address %08h", chunk, request_addresses[chunk]);
      if (request_lengths[chunk] !== 16'(expected_chunk_len))
        $fatal(1, "READ_CONS chunk %0d length %0d expected %0d",
               chunk, request_lengths[chunk], expected_chunk_len);
    end
    if (received_count != READ_LEN)
      $fatal(1, "READ_CONS received %0d bytes, expected %0d", received_count, READ_LEN);
    for (int i = 0; i < READ_LEN; i++) begin
      if (received[i] !== response_bytes[i])
        $fatal(1, "READ_CONS byte %0d got %02h expected %02h",
               i, received[i], response_bytes[i]);
    end

    $display("PASS tb_read_cons");
    $finish;
  end
endmodule
