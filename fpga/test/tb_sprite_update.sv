/* verilator lint_off PINMISSING */
/* verilator lint_off MULTITOP */

module tb_sprite_update;
  import hud_pkg::*;

  localparam int STREAM_LEN = 9;
  logic clk = 1'b0;
  always #5 clk = ~clk;
  logic ck_rst = 1'b1;
  logic send_sprite_update = 1'b0;
  logic [7:0] in_fifo;
  logic sprite_update_done, sprite_update_r_en, sprite_req_valid;
  logic [hud_pkg::SpriteLutDepth-1:0] sprite_id_bitmap = '0;
  sprite_inst_entry_t sprite_req_body;

  logic [7:0] stream [0:STREAM_LEN-1];
  integer fifo_ptr = 0;
  integer request_count = 0;
  integer cycles = 0;

  always_comb in_fifo = (fifo_ptr < STREAM_LEN) ? stream[fifo_ptr] : 8'hEE;
  always @(posedge clk) begin
    if (ck_rst) fifo_ptr <= 0;
    else if (sprite_update_r_en && fifo_ptr < STREAM_LEN)
      fifo_ptr <= fifo_ptr + 1;
  end

  sprite_update dut (
    .clk(clk), .ck_rst(ck_rst), .send_sprite_update(send_sprite_update),
    .in_fifo(in_fifo), .sprite_update_done(sprite_update_done),
    .sprite_update_r_en(sprite_update_r_en), .sprite_req_valid(sprite_req_valid),
    .sprite_req_body(sprite_req_body), .sprite_id_bitmap(sprite_id_bitmap)
  );

  always @(posedge clk) begin
    #1;
    if (!ck_rst && sprite_req_valid) begin
      request_count = request_count + 1;
      if (sprite_req_body.id !== hud_pkg::sprite_lut_id'(10'h205))
        $fatal(1, "UPDATE ID mismatch: %0d", sprite_req_body.id);
      if (sprite_req_body.mem_id !== hud_pkg::sprite_mem_id'(10'h12A))
        $fatal(1, "UPDATE memory ID mismatch: %0d", sprite_req_body.mem_id);
      if (sprite_req_body.active !== 1'b1) $fatal(1, "UPDATE active mismatch");
      if (sprite_req_body.x !== 16'h1234) $fatal(1, "UPDATE X mismatch: %h", sprite_req_body.x);
      if (sprite_req_body.y !== 16'hABCD) $fatal(1, "UPDATE Y mismatch: %h", sprite_req_body.y);
    end
  end

  initial begin
    stream[0]=8'h02; stream[1]=8'h05;
    stream[2]=8'h01; stream[3]=8'h2A;
    stream[4]=8'h01;
    stream[5]=8'h12; stream[6]=8'h34;
    stream[7]=8'hAB; stream[8]=8'hCD;
    sprite_id_bitmap[10'h205] = 1'b1;

    repeat (3) @(negedge clk);
    ck_rst = 1'b0;
    send_sprite_update = 1'b1;
    cycles = 0;
    while (!sprite_update_done && cycles < 40) begin
      @(negedge clk); cycles = cycles + 1;
    end
    if (!sprite_update_done) $fatal(1, "SPRITE_UPDATE timed out");
    send_sprite_update = 1'b0;
    repeat (1) @(negedge clk);

    if (request_count != 1) $fatal(1, "SPRITE_UPDATE request count %0d expected 1", request_count);
    $display("PASS tb_sprite_update");
    $finish;
  end
endmodule
