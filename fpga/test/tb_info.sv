`timescale 1ns/1ps

module tb_info;
  import config_pkg::*;

  logic clk = 1'b0;
  always #5 clk = ~clk;

  logic out_full = 1'b0;
  logic send_info = 1'b0;
  logic info_done, info_w_en;
  logic [7:0] info_fifo;

  logic [79:0] expected_data;
  integer bytes_seen = 0;
  integer cycles = 0;

  info dut (
    .clk(clk), .out_full(out_full), .send_info(send_info),
    .info_done(info_done), .info_w_en(info_w_en), .info_fifo(info_fifo)
  );

  always @(posedge clk) begin
    #1;
    if (send_info && info_w_en) begin
      if (bytes_seen >= 10)
        $fatal(1, "INFO emitted more than 10 bytes");
      if (info_fifo !== expected_data[79 - bytes_seen*8 -: 8])
        $fatal(1, "INFO byte %0d mismatch: got %02h expected %02h",
               bytes_seen, info_fifo, expected_data[79 - bytes_seen*8 -: 8]);
      bytes_seen = bytes_seen + 1;
    end
  end

  initial begin
    repeat (4) @(negedge clk);
    // The LFSR runs before the first INFO request and locks on send_info.
    expected_data = {ActiveWidth, ActiveHeight, 16'd1024, dut.sessionID};
    out_full = 1'b1;
    send_info = 1'b1;
    repeat (5) begin
      @(negedge clk);
      if (info_w_en !== 1'b0)
        $fatal(1, "INFO asserted write enable while output FIFO was full");
    end

    out_full = 1'b0;
    while (!info_done && cycles < 80) begin
      @(negedge clk);
      cycles = cycles + 1;
    end
    #2;

    if (!info_done) $fatal(1, "INFO timed out");
    if (bytes_seen != 10) $fatal(1, "INFO emitted %0d bytes, expected 10", bytes_seen);
    if (info_w_en !== 1'b0) $fatal(1, "INFO write enable should be low at completion");

    $display("PASS tb_info");
    $finish;
  end
endmodule
