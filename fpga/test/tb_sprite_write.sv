/* verilator lint_off PINMISSING */
/* verilator lint_off MULTITOP */

module tb_sprite_write;
  import hud_pkg::*;

  localparam int ID_WIDTH = ($clog2(hud_pkg::SpriteLutDepth) > 0)
                          ? $clog2(hud_pkg::SpriteLutDepth) : 1;
  localparam int STREAM_LEN = 10;

  logic clk = 1'b0;
  always #5 clk = ~clk;
  logic ck_rst = 1'b1;
  logic send_sprite_write = 1'b0;
  logic in_empty = 0, error_valid = 0;
  logic [7:0] in_fifo;
  logic [ID_WIDTH-1:0] sprite_available_id = '0;
  logic sprite_id_found = 1'b0;
  logic sprite_write_r_en, sprite_write_w_en, sprite_write_done, sprite_req_valid;
  logic [7:0] sprite_write_fifo, error_code;
  sprite_inst_entry_t sprite_req_body;

  logic [7:0] stream [0:STREAM_LEN-1];
  integer fifo_ptr = 0;
  integer request_count = 0;
  integer response_count = 0;
  integer cycles = 0;
  sprite_inst_entry_t expected_body;

  always_comb in_fifo = (fifo_ptr < STREAM_LEN) ? stream[fifo_ptr] : 8'hEE;

  always @(posedge clk) begin
    if (ck_rst) fifo_ptr <= 0;
    else if (sprite_write_r_en && fifo_ptr < STREAM_LEN)
      fifo_ptr <= fifo_ptr + 1;
  end

  sprite_write dut (
    .clk(clk), .ck_rst(ck_rst), .send_sprite_write(send_sprite_write),
    .in_fifo(in_fifo), .sprite_available_id(sprite_available_id),
    .sprite_id_found(sprite_id_found), .sprite_write_r_en(sprite_write_r_en),
    .sprite_write_w_en(sprite_write_w_en), .sprite_write_done(sprite_write_done),
    .sprite_req_valid(sprite_req_valid), .sprite_write_fifo(sprite_write_fifo),
    .sprite_req_body(sprite_req_body), .error_code(error_code), .in_empty(in_empty),
    .error_valid(error_valid)
  );

  always @(posedge clk) begin
    #1;
    if (!ck_rst) begin
      if (sprite_req_valid) begin
        request_count = request_count + 1;
        if (sprite_req_body !== expected_body)
          $fatal(1, "SPRITE_WRITE body mismatch: got %h expected %h", sprite_req_body, expected_body);
      end
      if (sprite_write_w_en) begin
        response_count = response_count + 1;
        if (sprite_write_fifo !== 8'd5)
          $fatal(1, "SPRITE_WRITE returned ID %0d expected 5", sprite_write_fifo);
      end
    end
  end

  initial begin
    stream[0]=8'h2A;       // memory ID
    stream[1]=8'h01;       // active
    stream[2]=8'h12; stream[3]=8'h34; // X
    stream[4]=8'h56; stream[5]=8'h78; // Y
    stream[6]=8'h00; stream[7]=8'h20; // width
    stream[8]=8'h00; stream[9]=8'h10; // height

    expected_body = {1'b1, 10'd5, 10'h2A, 16'h1234, 16'h5678, 16'h0020, 16'h0010};

    repeat (3) @(negedge clk);
    ck_rst = 1'b0;
    sprite_available_id = 5;
    sprite_id_found = 1'b1;
    send_sprite_write = 1'b1;

    cycles = 0;
    while (!sprite_write_done && cycles < 50) begin
      @(negedge clk); cycles = cycles + 1;
    end
    if (!sprite_write_done) $fatal(1, "SPRITE_WRITE timed out waiting for parsed entry");
    send_sprite_write = 1'b0;
    repeat (1) @(negedge clk);

    if (request_count != 1) $fatal(1, "SPRITE_WRITE request count %0d expected 1", request_count);
    if (response_count != 1) $fatal(1, "SPRITE_WRITE ID response count %0d expected 1", response_count);

    $display("PASS tb_sprite_write");
    $finish;
  end
endmodule
