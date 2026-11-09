// Unit tests for the framebuffer colour table.
//
// The property that matters is that a cell byte renders as its material's
// colour with no translation step -- fb_compositor writes cell bytes straight
// into the framebuffer, so the table has to decode the cell layer's format
// itself: { material[7:4], age[3:0] }.
//
// This used to assert that entries 0-15 were the material colours, which
// assumed the material was in the LOW nibble. It isn't, and the mismatch made
// every material render as an arbitrary ramp colour. The age sweep below is
// what pins the nibble down: it fails if anyone moves the decode back.
//
// The table is a ROM, so there is nothing to clock and nothing to write.
//
// Run: make generate-vcd TOP_MODULE=tb_palette
module tb_palette #(
    parameter string DUMPFILEPATH = ""
) ();
  localparam int unsigned MaterialCount = 8;  // sim_pkg::materials_e members
  localparam int unsigned CellIndexLimit = MaterialCount * 16;  // 128

  palette_pkg::palette_index_t index = 0;
  color_pkg::color_t color;

  palette dut (.index, .color);

  int unsigned errors = 0;

  task automatic check(input string name, input logic condition);
    if (condition !== 1'b1) begin
      $display("FAIL %s", name);
      errors = errors + 1;
    end
  endtask

  initial begin
    if (DUMPFILEPATH != "") begin
      $dumpfile(DUMPFILEPATH);
      $dumpvars(0, tb_palette);
    end

    // --- read is combinational --------------------------------------------
    // No clock anywhere in this testbench: if the lookup ever became
    // registered, every check below would read the previous index and fail.
    // The scan-out path already spends a cycle in the line buffer's registered
    // read, and a second would push the pixel out of its slot.
    index = 8'h10;  // {MatStone, age 0}
    #0
    check("read is combinational (no clock needed)",
          color === sim_pkg::material_color(sim_pkg::MatStone));

    // --- material lives in the HIGH nibble, age is ignored ------------------
    // Every age of a material must give that material's colour. This is the
    // check that fails if the decode moves to the low nibble: index 8'h10 would
    // then be material 0 (air) instead of material 1 (stone).
    for (int unsigned m = 0; m < MaterialCount; m++) begin
      for (int unsigned age = 0; age < 16; age++) begin
        index = palette_pkg::palette_index_t'((m << 4) | age);
        #0
        check($sformatf("index 0x%02h is material_color(%0d)", (m << 4) | age, m),
              color === sim_pkg::material_color(sim_pkg::material_t'(m)));
      end
    end

    // Spot-check that the materials really are distinguishable -- an all-zero
    // table would pass the loop above if material_color itself were broken.
    begin
      automatic color_pkg::color_t air;
      index = 8'h00;  // {MatAir, age 0}
      #0 air = color;
      index = 8'h20;  // {MatSand, age 0}
      #0 check("air and sand are different colours", air !== color);
    end

    // --- the cell region ends where the HUD region begins -------------------
    // The split is load-bearing: the HUD draws through the same index space, so
    // if the cell decode ever ran past 0x7F the two layers would collide.
    begin
      automatic color_pkg::color_t last_cell, first_hud;
      index = palette_pkg::palette_index_t'(CellIndexLimit - 1);
      #0 last_cell = color;
      index = palette_pkg::palette_index_t'(CellIndexLimit);
      #0 first_hud = color;
      check("cell region ends at CellIndexLimit",
            last_cell === sim_pkg::material_color(sim_pkg::material_t'(MaterialCount - 1)));
      check("HUD region is not a material colour", first_hud !== last_cell);
    end

    // --- the HUD ramp is monotonic in red ----------------------------------
    // Not a specification, just a sanity check that the upper half is a usable
    // gradient rather than a constant: anything drawn there has to be readable.
    begin
      automatic color_pkg::color_t low, high;
      index = 8'd160;
      #0 low = color;
      index = 8'd240;
      #0 high = color;
      check("ramp increases with index", high.red > low.red);
    end

    if (errors != 0) $fatal(1, "tb_palette: %0d check(s) failed", errors);
    $display("tb_palette: all checks passed");
    $finish;
  end

  initial begin
    #100000;
    $fatal(1, "tb_palette: timeout");
  end
endmodule
