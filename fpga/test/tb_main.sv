module tb_main #(
    parameter string DUMPFILEPATH = "" // this parameter is overwritten when compiling with make
);

  reg clk;
  wire [3:0] led;
  reg [3:0] sw;
  main #(
    .INCREMENT_LIMIT(1)
  ) dut (
      .clk(clk),
      .led(led),
      .sw (sw)
  );
  always #1 clk = ~clk;
  initial begin
    $dumpfile(DUMPFILEPATH);
    $dumpvars(0, tb_main);
  end
  initial begin
    sw = 1;
    clk = 0;
    repeat (2000) @(posedge clk);
    $finish;
  end
endmodule

