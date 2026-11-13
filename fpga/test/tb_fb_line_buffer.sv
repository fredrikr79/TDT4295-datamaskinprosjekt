// Unit tests for the double line buffer.
//
// tb_fb_scanout covers this in situ, but only along the paths a working
// fetcher happens to take. The cases below are the ones that bit during
// bring-up and would otherwise only show as a wrong picture:
//
//   - a FULL line must report LineBytes valid bytes, not LineBytes-1. The fill
//     counter has to be one bit wider than an address.
//   - half 1 must be addressed arithmetically. LineBytes is not a power of two,
//     so {half, offset} puts half 1 past the end of the array and every pixel
//     beyond x = 2**AddrBits - LineBytes reads garbage.
//   - a short line must report the shortfall as underrun rather than silently
//     serving stale bytes from the previous frame.
//
// Run: make generate-vcd TOP_MODULE=tb_fb_line_buffer
module tb_fb_line_buffer #(
    parameter string DUMPFILEPATH = ""
) ();
  localparam int unsigned LineBytes = mem_map_pkg::GridWidth;  // 640
  localparam int unsigned AddrBits = $clog2(LineBytes);

  logic clk = 0;
  always #1 clk = ~clk;
  logic reset = 1;

  logic fill_we = 0;
  logic [7:0] fill_data = 0;
  logic swap = 0;
  logic [AddrBits-1:0] read_x = 0;
  logic read_en = 0;
  logic [7:0] read_data;
  logic underrun;

  fb_line_buffer #(.LineBytes(LineBytes)) dut (
      .clk, .reset, .fill_we, .fill_data, .swap, .read_x, .read_en, .read_data, .underrun
  );

  int unsigned errors = 0;

  task automatic check(input string name, input logic condition);
    if (condition !== 1'b1) begin
      $display("FAIL %s", name);
      errors = errors + 1;
    end
  endtask

  // Fill the back half with `count` bytes of a recognisable pattern.
  task automatic fill_line(input int unsigned count, input logic [7:0] seed);
    for (int unsigned i = 0; i < count; i++) begin
      @(negedge clk);
      fill_we   = 1'b1;
      fill_data = 8'(seed + 8'(i));
    end
    @(negedge clk);
    fill_we = 1'b0;
  endtask

  task automatic do_swap();
    @(negedge clk);
    swap = 1'b1;
    @(negedge clk);
    swap = 1'b0;
  endtask

  // Read one byte out of the front half. Read is registered, so the data for
  // read_x appears on the following edge.
  task automatic read_at(input int unsigned x, output logic [7:0] data,
                         output logic saw_underrun);
    @(negedge clk);
    read_x  = AddrBits'(x);
    read_en = 1'b1;
    #0 saw_underrun = underrun;
    @(negedge clk);
    read_en = 1'b0;
    data = read_data;
  endtask

  logic [7:0] got;
  logic u;

  initial begin
    if (DUMPFILEPATH != "") begin
      $dumpfile(DUMPFILEPATH);
      $dumpvars(0, tb_fb_line_buffer);
    end

    repeat (4) @(negedge clk);
    reset = 0;
    @(negedge clk);

    // --- a full line, then both halves ------------------------------------
    // Two lines in a row, because the second lands in the OTHER half -- which
    // is the half that arithmetic-vs-concatenation addressing gets wrong.
    for (int unsigned pass = 0; pass < 2; pass++) begin
      automatic logic [7:0] seed = 8'(8'h10 * 8'(pass + 1));
      fill_line(LineBytes, seed);
      do_swap();

      read_at(0, got, u);
      check($sformatf("half %0d: first byte", pass), got == seed);
      check($sformatf("half %0d: no underrun at 0", pass), u === 1'b0);

      // The byte that concatenation addressing loses first.
      read_at(300, got, u);
      check($sformatf("half %0d: byte 300", pass), got == 8'(32'(seed) + 32'd300));

      // The byte that an address-width-wide fill counter loses.
      read_at(LineBytes - 1, got, u);
      check($sformatf("half %0d: last byte", pass), got == 8'(seed + 8'(LineBytes - 1)));
      check($sformatf("half %0d: no underrun at last byte", pass), u === 1'b0);
    end

    // --- a short line must report underrun --------------------------------
    fill_line(100, 8'hA0);
    do_swap();

    read_at(99, got, u);
    check("short line: last delivered byte is valid", u === 1'b0);
    check("short line: last delivered byte is correct", got == 8'(8'hA0 + 8'd99));

    read_at(100, got, u);
    check("short line: first undelivered byte underruns", u === 1'b1);

    read_at(LineBytes - 1, got, u);
    check("short line: end of line underruns", u === 1'b1);

    // --- underrun only applies while actually reading ----------------------
    @(negedge clk);
    read_x  = AddrBits'(LineBytes - 1);
    read_en = 1'b0;
    #0 check("underrun is gated on read_en", underrun === 1'b0);

    if (errors != 0) $fatal(1, "tb_fb_line_buffer: %0d check(s) failed", errors);
    $display("tb_fb_line_buffer: all checks passed");
    $finish;
  end

  initial begin
    #100000;
    $fatal(1, "tb_fb_line_buffer: timeout");
  end
endmodule
