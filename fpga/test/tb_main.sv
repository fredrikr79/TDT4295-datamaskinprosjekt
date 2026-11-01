/* verilator lint_off PINMISSING */
/* verilator lint_off MULTITOP */

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

  // PSRAM pins. Without a memory on the far end of these, every read returns X
  // and the failure looks like a logic bug rather than a missing model.
  wire mem_out_clk, mem_out_clk_n, mem_cs_n, mem_reset_n;
  wire [7:0] mem_dq;
  wire mem_rwds;

  main dut (
      .clk(clk),
      .sw({reset, enable}),
      .btn({right, left, down, up}),
      .vga_r(color.red),
      .vga_g(color.green),
      .vga_b(color.blue),
      .vga_hsync(vga_hsync),
      .vga_vsync(vga_vsync),

      .mem_out_clk(mem_out_clk),
      .mem_out_clk_n(mem_out_clk_n),
      .mem_cs_n(mem_cs_n),
      .mem_reset_n(mem_reset_n),
      .mem_dq(mem_dq),
      .mem_rwds(mem_rwds)
  );

  psram_model psram (
      .cs_n(mem_cs_n),
      .ck(mem_out_clk),
      .reset_n(mem_reset_n),
      .dq(mem_dq),
      .rwds(mem_rwds)
  );

  // pixel_pulse is internal to main (no board pin), tap it for the simulator
  assign pixel_pulse = dut.pixel_pulse;

  initial begin
    if (DUMPFILEPATH != "") begin
      $dumpfile(DUMPFILEPATH);
      $dumpvars(0, tb_main);
    end

    // Paint a test pattern into Fb0, straight into the model's backing store.
    //
    // The cell layer now scans out of the framebuffer rather than BRAM, and
    // nothing writes that framebuffer until milestone 6 gives the cell
    // streamer a render pass. Without this, sim-vga would show whatever
    // uninitialised PSRAM contains -- which in Verilator is a black screen,
    // indistinguishable from the display path being broken.
    //
    // Colour comes from the palette's power-on defaults: indices 0-15 are
    // cell_pkg material colours, above that a ramp. The gradient plus the
    // bands make tearing, a line fetched from the wrong address, or a stuck
    // double-buffer swap obvious by eye.
    for (int unsigned y = 0; y < mem_map_pkg::GridHeight; y++) begin
      for (int unsigned x = 0; x < mem_map_pkg::GridWidth; x++) begin
        psram.mem[mem_map_pkg::Fb0MemPointer + y * mem_map_pkg::GridWidth + x] =
            // Left eighth: vertical bands of the 16 material colours, so the
            // palette is visible. Elsewhere: a two-axis ramp.
            (x < mem_map_pkg::GridWidth / 8) ? 8'((y / 16) % 16)
                                             : 8'(((x * 3) + (y * 7)) & 32'hFF);
      end
    end
  end


  assign h_sync = ~vga_hsync;
  assign v_sync = ~vga_vsync;
endmodule

