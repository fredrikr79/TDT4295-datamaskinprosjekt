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

  // Seed the cell grid in PSRAM with a starting world, the same way
  // tb_sim_memory does. This is the cell layer's *input*, not a framebuffer
  // paint-over: sim_buffers reads these rows, simulator advances them, and
  // fb_compositor writes the result to the inactive framebuffer. So unlike a
  // test pattern poked into Fb0, this exercises the whole pipeline and the
  // world actually falls, flows and burns on screen.
  //
  // Layout is mem_map_pkg's: one byte per cell at CellGridMemPointer + y*W + x,
  // each byte {material[7:4], age[3:0]}. Scene is ported from tb_sim's
  // initialize_world() so sim-vga and tb_sim start from the same state.
  localparam int unsigned CellW = sim_pkg::CellGridWidth;  // 640
  localparam int unsigned CellH = sim_pkg::CellGridHeight;  // 360

  task automatic seed_world();
    logic [7:0] cell_val;
    for (int unsigned y = 0; y < CellH; y++) begin
      for (int unsigned x = 0; x < CellW; x++) begin
        cell_val = {sim_pkg::MatAir, 4'h0};

        // Stone floor and two floating platforms.
        if (y >= CellH - 12 || (y == CellH - 65 && x > 70 && x < 220) ||
            (y == CellH - 105 && x > 390 && x < 550))
          cell_val = {sim_pkg::MatStone, 4'h0};

        // Sand pile, resting on the floor.
        if (y > CellH - 95 && y < CellH - 25 && x > 115 && x < 185 &&
            (int'(x) - 150) * (int'(x) - 150) +
            (int'(y) - (int'(CellH) - 50)) * (int'(y) - (int'(CellH) - 50)) < 1100)
          cell_val = {sim_pkg::MatSand, 4'h0};

        // Water pool.
        if (y > CellH - 55 && y < CellH - 20 && x > 300 && x < 390)
          cell_val = {sim_pkg::MatWater, 4'h0};

        // Lava and acid samples, so the reactive materials are on screen too.
        if (y > CellH - 45 && y < CellH - 25 && x > 465 && x < 485)
          cell_val = {sim_pkg::MatLava, 4'h0};
        if (y > CellH - 35 && y < CellH - 25 && x > 520 && x < 540)
          cell_val = {sim_pkg::MatAcid, 4'h0};

        psram.mem[mem_map_pkg::CellGridMemPointer+y*CellW+x] = cell_val;
      end
    end

    // Game-of-Life seed (a glider) in open air above the sand.
    psram.mem[mem_map_pkg::CellGridMemPointer+(CellH-40)*CellW+150] = {sim_pkg::MatGOL, 4'b1000};
    psram.mem[mem_map_pkg::CellGridMemPointer+(CellH-40)*CellW+152] = {sim_pkg::MatGOL, 4'b1000};
    psram.mem[mem_map_pkg::CellGridMemPointer+(CellH-39)*CellW+151] = {sim_pkg::MatGOL, 4'b1000};
    psram.mem[mem_map_pkg::CellGridMemPointer+(CellH-39)*CellW+152] = {sim_pkg::MatGOL, 4'b1000};
    psram.mem[mem_map_pkg::CellGridMemPointer+(CellH-38)*CellW+151] = {sim_pkg::MatGOL, 4'b1000};

    // Explosion seed at maximum force, in open space.
    psram.mem[mem_map_pkg::CellGridMemPointer+90*CellW+350] = {sim_pkg::MatExplo, 4'hF};
  endtask

  initial begin
    if (DUMPFILEPATH != "") begin
      $dumpfile(DUMPFILEPATH);
      $dumpvars(0, tb_main);
    end

    // Time 0, before reset is released: psram_model never clears its backing
    // store, and memory_initializer only writes the device's config registers.
    seed_world();
  end


  assign h_sync = ~vga_hsync;
  assign v_sync = ~vga_vsync;
endmodule

