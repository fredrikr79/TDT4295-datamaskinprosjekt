/* verilator lint_off PINMISSING */
/* verilator lint_off MULTITOP */

// This is the memory-upload handler named write_sprite. It is distinct from
// sprite_write, which parses a sprite record for the sprite LUT manager.
module tb_write_sprite_upload #(
  parameter int PAYLOAD_LEN = 25
);
  import config_pkg::*;
  import mem_driver_pkg::*;
  import memory_pkg::*;

  localparam int HEADER_LEN = 2;
  localparam int STREAM_LEN = HEADER_LEN + PAYLOAD_LEN;
  localparam int CHUNK_COUNT = (PAYLOAD_LEN + memory_pkg::MaxMessageLength - 1)
                             / memory_pkg::MaxMessageLength;

  logic clk = 1'b0;
  always #5 clk = ~clk;
  logic ck_rst = 1'b0;
  logic mem_data_on_wire = 1'b0;
  logic send_write_sprite = 1'b0;
  logic mem_busy = 1'b1;
  logic [7:0] in_fifo;
  logic [7:0] write_sprite_fifo, mem_in;
  logic [15:0] mem_len;
  hud_pkg::sprite_mem_id write_sprite_mem_id;
  logic write_sprite_done, write_sprite_r_en, write_sprite_w_en;
  logic mem_req, write_sprite_valid;
  logic [31:0] mem_addr;

  logic [7:0] stream [0:STREAM_LEN-1];
  logic [7:0] payload_seen [0:PAYLOAD_LEN-1];
  logic [31:0] request_addresses [0:CHUNK_COUNT-1];
  logic [15:0] request_lengths [0:CHUNK_COUNT-1];
  integer fifo_ptr = 0;
  integer payload_count = 0;
  integer req_count = 0;
  integer write_count = 0;
  integer valid_count = 0;
  integer cycles = 0;

  always_comb in_fifo = (fifo_ptr < STREAM_LEN) ? stream[fifo_ptr] : 8'hEE;

  // FWFT input FIFO: advance at the edge where an already-asserted read enable is sampled.
  always @(posedge clk) begin
    if (ck_rst) fifo_ptr <= 0;
    else if (write_sprite_r_en && fifo_ptr < STREAM_LEN)
      fifo_ptr <= fifo_ptr + 1;
  end

  write_sprite dut (
    .clk(clk), .ck_rst(ck_rst), .mem_data_on_wire(mem_data_on_wire),
    .send_write_sprite(send_write_sprite), .mem_busy(mem_busy), .in_fifo(in_fifo),
    .write_sprite_fifo(write_sprite_fifo), .mem_len(mem_len), .mem_in(mem_in),
    .write_sprite_done(write_sprite_done), .write_sprite_r_en(write_sprite_r_en),
    .write_sprite_w_en(write_sprite_w_en), .mem_req(mem_req),
    .write_sprite_valid(write_sprite_valid), .write_sprite_mem_id(write_sprite_mem_id),
    .mem_addr(mem_addr)
  );

  always @(posedge clk) begin
    logic sampled_read_enable;
    integer sampled_fifo_ptr;
    sampled_read_enable = write_sprite_r_en;
    sampled_fifo_ptr = fifo_ptr;
    #1;
    if (!ck_rst) begin
      if (mem_req) begin
        if (req_count >= CHUNK_COUNT) $fatal(1, "WRITE_SPRITE issued too many requests");
        request_addresses[req_count] = mem_addr;
        request_lengths[req_count] = mem_len;
        req_count = req_count + 1;
      end
      if (write_sprite_w_en) begin
        if (write_count > 1) $fatal(1, "WRITE_SPRITE returned too many ID bytes");
        if (write_sprite_fifo !== 8'h00)
          $fatal(1, "WRITE_SPRITE returned ID byte %02h, expected 00", write_sprite_fifo);
        write_count = write_count + 1;
      end
      if (write_sprite_valid) valid_count = valid_count + 1;
        if (mem_data_on_wire && !mem_busy && sampled_read_enable &&
          sampled_fifo_ptr >= HEADER_LEN) begin
        if (payload_count >= PAYLOAD_LEN) $fatal(1, "WRITE_SPRITE transferred too many bytes");
        payload_seen[payload_count] = mem_in;
        payload_count = payload_count + 1;
      end
    end
  end

  task automatic pulse_data_ready;
    begin
      @(negedge clk); mem_data_on_wire = 1'b1;
      @(negedge clk); mem_data_on_wire = 1'b0;
      @(negedge clk);
    end
  endtask

  initial begin
    stream[0] = 8'h00;
    stream[1] = PAYLOAD_LEN[7:0];
    for (int i = 0; i < PAYLOAD_LEN; i++) stream[HEADER_LEN + i] = 8'(8'hA0 + i);

    repeat (3) @(negedge clk);
    ck_rst = 1'b0;
    send_write_sprite = 1'b1;
    repeat (7) @(negedge clk);
    mem_busy = 1'b0;

    cycles = 0;
    while (req_count == 0 && cycles < 40) begin
      @(negedge clk); cycles = cycles + 1;
    end
    if (req_count < 1) $fatal(1, "WRITE_SPRITE expected a memory request");

    // Payload is available continuously, so hold data_on_wire high.
    @(negedge clk);
    mem_data_on_wire = 1'b1;

    cycles = 0;
    while (!write_sprite_done && cycles < PAYLOAD_LEN * 3 + 40) begin
      @(negedge clk);
      cycles = cycles + 1;
    end

    if (!write_sprite_done)
      $fatal(1, "WRITE_CONS timed out");

    // No more payload data is available after completion.
    mem_data_on_wire = 1'b0;
    send_write_sprite = 1'b0;
    repeat (1) @(negedge clk);


    if (payload_count != PAYLOAD_LEN)
      $fatal(1, "WRITE_SPRITE payload count %0d expected %0d", payload_count, PAYLOAD_LEN);
    for (int i = 0; i < PAYLOAD_LEN; i++) begin
      if (payload_seen[i] !== stream[HEADER_LEN + i])
        $fatal(1, "WRITE_SPRITE payload byte %0d got %02h expected %02h",
               i, payload_seen[i], stream[HEADER_LEN + i]);
    end
    if (req_count != CHUNK_COUNT)
      $fatal(1, "WRITE_SPRITE request count %0d expected %0d", req_count, CHUNK_COUNT);
    for (int chunk = 0; chunk < CHUNK_COUNT; chunk++) begin
      int chunk_offset;
      int expected_chunk_len;
      chunk_offset = chunk * memory_pkg::MaxMessageLength;
      expected_chunk_len = (PAYLOAD_LEN - chunk_offset > memory_pkg::MaxMessageLength)
                         ? memory_pkg::MaxMessageLength
                         : PAYLOAD_LEN - chunk_offset;
      if (request_addresses[chunk] !== 32'(chunk_offset))
        $fatal(1, "WRITE_SPRITE chunk %0d address %08h expected %08h",
               chunk, request_addresses[chunk], 32'(chunk_offset));
      if (request_lengths[chunk] !== 16'(expected_chunk_len))
        $fatal(1, "WRITE_SPRITE chunk %0d length %0d expected %0d",
               chunk, request_lengths[chunk], expected_chunk_len);
    end
    if (write_count != 2) $fatal(1, "WRITE_SPRITE ID response count %0d expected 2", write_count);
    if (write_sprite_mem_id !== hud_pkg::sprite_mem_id'(0))
      $fatal(1, "WRITE_SPRITE memory ID %0d expected 0", write_sprite_mem_id);
    // Assumption: this interface flag reports that the sprite upload is now valid.
    if (valid_count != 1)
      $fatal(1, "WRITE_SPRITE expected one write_sprite_valid pulse, observed %0d", valid_count);

    $display("PASS tb_write_sprite_upload");
    $finish;
  end
endmodule
