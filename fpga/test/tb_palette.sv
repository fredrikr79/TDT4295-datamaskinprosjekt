// Unit tests for the framebuffer colour table.
//
// The property that matters is that entries 0-15 are the cell_pkg material
// colours. That is what lets a framebuffer byte holding a material id render
// correctly with no translation step, which is the whole plan for milestone 6 --
// if these drift apart, the cell layer comes out the wrong colour and the
// cause is two files away from the symptom.
//
// The table is a ROM, so there is nothing to clock and nothing to write.
//
// Run: make generate-vcd TOP_MODULE=tb_palette
module tb_palette #(
    parameter string DUMPFILEPATH = ""
) ();
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
    index = 8'd1;
    #0 check("read is combinational (no clock needed)", color === cell_pkg::material_color(4'd1));

    // --- material colours occupy the low 16 entries -----------------------
    for (int unsigned i = 0; i < 16; i++) begin
      index = palette_pkg::palette_index_t'(i);
      #0
      check($sformatf("entry %0d is material_color(%0d)", i, i),
            color === cell_pkg::material_color(cell_pkg::material_t'(i)));
    end

    // Spot-check that the materials really are distinguishable -- an all-zero
    // table would pass the loop above if material_color itself were broken.
    index = 8'd0;
    #0 begin
      automatic color_pkg::color_t air = color;
      index = 8'd1;
      #0 check("air and sand are different colours", air !== color);
    end

    // --- the ramp above 16 is monotonic in red ----------------------------
    // Not a specification, just a sanity check that the defaults are a usable
    // gradient rather than a constant: a test pattern has to be readable.
    begin
      automatic color_pkg::color_t low, high;
      index = 8'd32;
      #0 low = color;
      index = 8'd224;
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
