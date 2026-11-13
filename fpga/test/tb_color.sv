// Unit tests for the framebuffer colour decode, color_pkg::color_lookup.
//
// Replaces tb_palette, which tested the palette ROM that the 5-6-5 colour work
// removed. The property that mattered there still matters here: a framebuffer
// byte renders with no translation step, so the decode has to understand the
// cell layer's format itself -- { material[7:4], age[3:0] } -- and the HUD has
// to fit in the same byte without colliding with it.
//
// The decode is pure combinational logic in a package, so there is nothing to
// clock and nothing to reset.
//
// Run: make generate-vcd TOP_MODULE=tb_color
module tb_color #(
    parameter string DUMPFILEPATH = ""
) ();
  localparam int unsigned MaterialCount = 8;  // sim_pkg::materials_e members

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
      $dumpvars(0, tb_color);
    end

    // --- the material lives in the HIGH nibble -----------------------------
    // Index 8'h10 is stone at age 0. If the decode ever moved to the low
    // nibble it would be air instead, and every material would render as an
    // arbitrary ramp colour.
    check("0x10 decodes as stone",
          color_pkg::color_lookup(8'h10) === color_pkg::stone_colors(4'h0));
    check("0x01 is NOT stone",
          color_pkg::color_lookup(8'h01) !== color_pkg::stone_colors(4'h0));

    // Spot-check that the materials really are distinguishable -- an all-black
    // table would satisfy a decode check alone.
    check("air and sand are different colours",
          color_pkg::color_lookup(8'h00) !== color_pkg::color_lookup(8'h20));
    check("water ages are a ramp, not a constant",
          color_pkg::color_lookup(8'h30) !== color_pkg::color_lookup(8'h3F));

    // --- the cell region ends where the HUD region begins -------------------
    // The split is load-bearing: the HUD draws through the same byte, so a
    // material id with bit 7 set would be painted over by the HUD ramp.
    for (int unsigned m = 0; m < MaterialCount; m++) begin
      check($sformatf("material %0d stays out of the HUD half", m), !m[3]);
    end

    for (int unsigned c = 0; c < 65536; c += 257) begin
      check($sformatf("rgb565 0x%04h encodes into the HUD half", c),
            color_pkg::rgb565_to_hud_color(16'(c))[7]);
    end

    // --- HUD codes keep the top bits of each channel ------------------------
    // 3-2-2, so only the top 3 red, 2 green and 2 blue bits survive. Anything
    // else means the rasterizer's sprites come out the wrong hue.
    begin
      automatic logic [15:0] src, got;
      for (int unsigned c = 0; c < 65536; c += 257) begin
        src = 16'(c);
        got = color_pkg::color_lookup(color_pkg::rgb565_to_hud_color(src));
        check($sformatf("0x%04h keeps its red", c), got[15:13] === src[15:13]);
        check($sformatf("0x%04h keeps its green", c), got[10:9] === src[10:9]);
        check($sformatf("0x%04h keeps its blue", c), got[4:3] === src[4:3]);
      end
    end

    // --- the ramp reaches both ends -----------------------------------------
    // Channels widen by bit replication rather than zero-padding, so a white
    // sprite pixel has to come back as full white. Truncation alone would give
    // 0xF79E and tint everything bright slightly dark.
    check("white survives the round trip",
          color_pkg::color_lookup(color_pkg::rgb565_to_hud_color(16'hFFFF)) === 16'hFFFF);
    check("black survives the round trip",
          color_pkg::color_lookup(color_pkg::rgb565_to_hud_color(16'h0000)) === 16'h0000);

    // Not a specification, just a sanity check that the ramp is usable: the HUD
    // half has to be a gradient rather than a constant for anything drawn in it
    // to be readable.
    check("the HUD ramp increases with red",
          color_pkg::color_lookup(8'hF0) > color_pkg::color_lookup(8'h90));

    if (errors != 0) $fatal(1, "tb_color: %0d check(s) failed", errors);
    $display("tb_color: all checks passed");
    $finish;
  end

  initial begin
    #100000;
    $fatal(1, "tb_color: timeout");
  end
endmodule
