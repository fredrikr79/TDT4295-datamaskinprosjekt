`timescale 1ns/1ps

// This is the memory-upload handler named write_sprite. It is distinct from
// sprite_write, which parses a sprite record for the sprite LUT manager.
module tb_write_sprite_upload;
  import config_pkg::*;
  import mem_driver_pkg::*;
  import memory_pkg::*;

  localparam int PAYLOAD_LEN = 2;
  localparam int STREAM_LEN = 1 + PAYLOAD_LEN;

  logic clk = 1'b0;
  always #5 clk = ~clk;
  logic ck_rst = 1'b0;
  logic mem_data_on_wire = 1'b0;
  logic send_write_sprite = 1'b0;
  logic mem_busy = 1'b1;
  logic [7:0] in_fifo;
  logic [7:0] write_sprite_fifo, mem_len, mem_in;
  logic write_sprite_done, write_sprite_r_en, write_sprite_w_en;
  logic mem_req, write_sprite_valid;
  logic [31:0] mem_addr;

  logic [7:0] stream [0:STREAM_LEN-1];
  logic [7:0] payload_seen [0:PAYLOAD_LEN-1];
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
    .write_sprite_valid(write_sprite_valid), .mem_addr(mem_addr)
  );

  always @(posedge clk) begin
    #1;
    if (!ck_rst) begin
      if (mem_req) req_count = req_count + 1;
      if (write_sprite_w_en) begin
        write_count = write_count + 1;
        if (write_sprite_fifo !== 8'd0)
          $fatal(1, "WRITE_SPRITE returned ID %0d, expected first ID 0", write_sprite_fifo);
      end
      if (write_sprite_valid) valid_count = valid_count + 1;
      if (mem_data_on_wire && !mem_busy && write_sprite_r_en && fifo_ptr >= 1) begin
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
    stream[0] = PAYLOAD_LEN[7:0];
    stream[1] = 8'hCA;
    stream[2] = 8'hFE;

    repeat (3) @(negedge clk);
    ck_rst = 1'b0;
    send_write_sprite = 1'b1;
    repeat (7) @(negedge clk);
    mem_busy = 1'b0;

    cycles = 0;
    while (req_count == 0 && cycles < 40) begin
      @(negedge clk); cycles = cycles + 1;
    end
    if (req_count != 1)
      $fatal(1, "WRITE_SPRITE expected one mem_req pulse, observed %0d", req_count);

    // Payload is available continuously, so hold data_on_wire high.
    @(negedge clk);
    mem_data_on_wire = 1'b1;

    cycles = 0;
    while (!write_sprite_done && cycles < 30) begin
      @(negedge clk);
      cycles = cycles + 1;
    end

    if (!write_sprite_done)
      $fatal(1, "WRITE_CONS timed out");

    // No more payload data is available after completion.
    mem_data_on_wire = 1'b0;
    send_write_sprite = 1'b0;
    repeat (1) @(negedge clk);


    if (mem_len !== PAYLOAD_LEN[7:0]) $fatal(1, "WRITE_SPRITE length mismatch");
    if (payload_count != PAYLOAD_LEN)
      $fatal(1, "WRITE_SPRITE payload count %0d expected %0d", payload_count, PAYLOAD_LEN);
    if (payload_seen[0] !== 8'hCA || payload_seen[1] !== 8'hFE)
      $fatal(1, "WRITE_SPRITE payload mismatch: %02h, %02h", payload_seen[0], payload_seen[1]);
    if (write_count != 1) $fatal(1, "WRITE_SPRITE ID response count %0d expected 1", write_count);
    // Assumption: this interface flag reports that the sprite upload is now valid.
    if (valid_count != 1)
      $fatal(1, "WRITE_SPRITE expected one write_sprite_valid pulse, observed %0d", valid_count);

    $display("PASS tb_write_sprite_upload");
    $finish;
  end
endmodule
