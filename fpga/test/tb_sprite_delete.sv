/* verilator lint_off PINMISSING */
/* verilator lint_off MULTITOP */

module tb_sprite_delete;
  import hud_pkg::*;

  logic clk = 1'b0;
  always #5 clk = ~clk;
  logic ck_rst = 1'b1;
  logic send_sprite_delete = 1'b0;
  logic [7:0] in_fifo;
  logic sprite_delete_done, sprite_delete_r_en, sprite_req_valid;
  sprite_inst_entry_t sprite_req_body;
  logic [hud_pkg::SpriteLutDepth-1:0] sprite_id_bitmap = '0;

  logic [7:0] stream [2];
  integer fifo_ptr = 0;
  integer request_count = 0;
  integer cycles = 0;

  always_comb in_fifo = (fifo_ptr < 2) ? stream[fifo_ptr] : 8'hEE;
  always @(posedge clk) begin
    if (ck_rst) fifo_ptr <= 0;
    else if (sprite_delete_r_en && fifo_ptr < 2)
      fifo_ptr <= fifo_ptr + 1;
  end

  sprite_delete dut (
    .clk(clk), .ck_rst(ck_rst), .send_sprite_delete(send_sprite_delete),
    .in_fifo(in_fifo), .sprite_delete_done(sprite_delete_done),
    .sprite_delete_r_en(sprite_delete_r_en), .sprite_req_valid(sprite_req_valid),
    .sprite_req_body(sprite_req_body), .sprite_id_bitmap(sprite_id_bitmap)
  );

  always @(posedge clk) begin
    #1;
    if (!ck_rst && sprite_req_valid) begin
      request_count = request_count + 1;
      if (sprite_req_body.id !== hud_pkg::sprite_lut_id'(10'h107))
        $fatal(1, "DELETE ID mismatch: got %0d expected 263", sprite_req_body.id);
    end
  end

  initial begin
    stream[0] = 8'h01;
    stream[1] = 8'h07;
    sprite_id_bitmap[10'h107] = 1'b1;
    repeat (3) @(negedge clk);
    ck_rst = 1'b0;
    send_sprite_delete = 1'b1;

    // Start a command, then interrupt it with reset before the ID is consumed.
    @(posedge clk); #1;
    if (sprite_delete_r_en !== 1'b1)
      $fatal(1, "SPRITE_DELETE did not assert read enable in START");
    @(negedge clk);
    send_sprite_delete = 1'b0;
    ck_rst = 1'b1;
    repeat (2) @(negedge clk);
    if (sprite_req_valid !== 1'b0 || sprite_delete_r_en !== 1'b0)
      $fatal(1, "SPRITE_DELETE request/read enable not cleared by reset");

    // The interrupted command must be re-armed from START after reset.
    ck_rst = 1'b0;
    send_sprite_delete = 1'b1;
    @(posedge clk); #1;
    if (sprite_delete_r_en !== 1'b1)
      $fatal(1, "SPRITE_DELETE did not restart from START after reset");

    cycles = 0;
    while (!sprite_delete_done && cycles < 30) begin
      @(negedge clk); cycles = cycles + 1;
    end
    if (!sprite_delete_done) $fatal(1, "SPRITE_DELETE timed out");
    send_sprite_delete = 1'b0;
    repeat (1) @(negedge clk);

    if (request_count != 1) $fatal(1, "SPRITE_DELETE request count %0d expected 1", request_count);
    $display("PASS tb_sprite_delete");
    $finish;
  end
endmodule
