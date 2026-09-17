module tb_main #(
    parameter string DUMPFILEPATH = ""  // this parameter is overwritten when compiling with make
) (
    input logic clk,
    input logic reset,
    input logic enable,
    input logic up,
    input logic down,
    input logic left,
    input logic right,

    output logic              h_sync,
    output logic              v_sync,
    output logic pixel_pulse,
    output color_pkg::color_t color
);
  logic vga_hsync, vga_vsync;

  main dut (
      .clk(clk),
      .sw({reset, enable}),
      .btn({right, left, down, up}),
      .vga_r(color.red),
      .vga_g(color.green),
      .vga_b(color.blue),
      .vga_hsync(vga_hsync),
      .vga_vsync(vga_vsync)
  );

  // pixel_pulse is internal to main (no board pin), tap it for the simulator
  assign pixel_pulse = dut.pixel_pulse;

  initial begin
    if (DUMPFILEPATH != "") begin
      $dumpfile(DUMPFILEPATH);
      $dumpvars(0, tb);
    end
  end


  assign h_sync = ~vga_hsync;
  assign v_sync = ~vga_vsync;
endmodule

