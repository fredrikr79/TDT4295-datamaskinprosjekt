
 
// Testbench for the cell simulator, driving it through sim_wrap.
//
// It plays the part of sim_buffers: it keeps the world in an array, feeds the simulator a
// three-row window, and writes result rows back using the same pairing rules as the real
// module. No memory, SPI or display is involved, so this tests the rules and nothing else.
//
// Works unchanged for behavioural simulation and for post-synthesis functional simulation
// with sim_wrap as the synthesis top.
module tb_sim;
  import sim_pkg::*;
 
  localparam int GW = CellGridWidth;   // 160
  localparam int GH = CellGridHeight;  // 90
  localparam int Bpp = BitsPerPixel;   // 8
  localparam int W = GW * Bpp;
 
  // ---------------------------------------------------------------- DUT
  logic clk = 1'b0;
  always #5 clk = ~clk;  // 100 MHz
 
  logic pulse_new_row = 1'b0;
  logic sim_running = 1'b0;
  logic [3*W-1:0] in_flat = '0;
  wire pulse_transaction_pair_done, pulse_row_done;
  wire [2*W-1:0] out_flat;
 
  sim_wrap dut (
      .clk(clk),
      .pulse_new_row(pulse_new_row),
      .sim_running(sim_running),
      .in_flat(in_flat),
      .pulse_transaction_pair_done(pulse_transaction_pair_done),
      .pulse_row_done(pulse_row_done),
      .out_flat(out_flat)
  );
 
  // ---------------------------------------------------------------- world model
  logic [W-1:0] world[GH];
  int errors = 0;
 
  function automatic logic [7:0] cell_at(input int x, input int y);
    return world[y][x*Bpp+:Bpp];
  endfunction
 
  function automatic logic [3:0] mat_at(input int x, input int y);
    return world[y][x*Bpp+4+:4];
  endfunction
 
  // A GOL cell with its alive bit set.
  function automatic bit alive_at(input int x, input int y);
    return (world[y][x*Bpp+4+:4] == 4'(MatGOL)) && (world[y][x*Bpp+3] === 1'b1);
  endfunction
 
  task automatic put(input int x, input int y, input logic [3:0] mat, input logic [3:0] state);
    world[y][x*Bpp+:Bpp] = {mat, state};
  endtask
 
  function automatic int count_mat(input logic [3:0] mat);
    int n = 0;
    for (int y = 0; y < GH; y++)
      for (int x = 0; x < GW; x++) if (world[y][x*Bpp+4+:4] == mat) n++;
    return n;
  endfunction
 
  // Row of the first cell of this material, searching top to bottom; -1 if none.
  function automatic int first_row_of(input logic [3:0] mat);
    for (int y = 0; y < GH; y++)
      for (int x = 0; x < GW; x++) if (world[y][x*Bpp+4+:4] == mat) return y;
    return -1;
  endfunction
 
  function automatic int first_col_of(input logic [3:0] mat);
    for (int y = 0; y < GH; y++)
      for (int x = 0; x < GW; x++) if (world[y][x*Bpp+4+:4] == mat) return x;
    return -1;
  endfunction
 
  function automatic string glyph(input logic [7:0] c);
    case (c[7:4])
      4'(MatAir):   return ".";
      4'(MatStone): return "#";
      4'(MatSand):  return "s";
      4'(MatWater): return "~";
      4'(MatLava):  return "L";
      4'(MatAcid):  return "a";
      4'(MatGOL):   return c[3] ? "O" : "o";
      4'(MatExplo): return "*";
      default:      return "?";
    endcase
  endfunction
 
  task automatic show(input string title, input int x0, input int y0, input int w, input int h);
    string line;
    $display("  %s  (x %0d..%0d, y %0d..%0d)", title, x0, x0 + w - 1, y0, y0 + h - 1);
    for (int y = y0; y < y0 + h; y++) begin
      line = "";
      for (int x = x0; x < x0 + w; x++) line = {line, glyph(cell_at(x, y))};
      $display("    %2d  %s", y, line);
    end
  endtask
 
  task automatic check(input bit ok, input string what);
    if (ok) $display("  ok    %s", what);
    else begin
      $display("  FAIL  %s", what);
      errors++;
    end
  endtask
 
  // ---------------------------------------------------------------- one simulation frame
  // Mirrors sim_buffers: window = {row above, current row, row below}, taken from the world
  // as it was when each row was fetched; results are written back when a pair completes.
  task automatic run_frame();
    logic [W-1:0] win[3];
    logic [W-1:0] out0, out1;
    bit single_row_edgecase;
    bit pair_done;
    int guard;
 
    win[0] = '0;
    win[1] = world[0];
    win[2] = (GH > 1) ? world[1] : '0;
    single_row_edgecase = 1'b1;
 
    for (int r = 0; r < GH; r++) begin
      in_flat = {win[2], win[1], win[0]};
 
      @(negedge clk);
      pulse_new_row = 1'b1;
      @(negedge clk);
      pulse_new_row = 1'b0;
 
      guard = 0;
      while (pulse_row_done !== 1'b1) begin
        @(negedge clk);
        guard++;
        if (guard > 2000) begin
          $display("  FAIL  row %0d: no pulse_row_done after 2000 cycles", r);
          errors++;
          $finish;
        end
      end
      pair_done = (pulse_transaction_pair_done === 1'b1);
      out0 = out_flat[0*W+:W];
      out1 = out_flat[1*W+:W];
 
      // shift the window exactly like sim_buffers does on pulse_row_done
      win[0] = win[1];
      win[1] = win[2];
      win[2] = '0;
 
      if (pair_done) begin
        if (!single_row_edgecase) begin
          world[r-1] = out1;  // upper row of the pair
          world[r]   = out0;  // lower row of the pair
        end else begin
          world[r] = (r % 2 == 1) ? out1 : out0;  // unmatched boundary row
        end
        single_row_edgecase = 1'b1;
      end else begin
        single_row_edgecase = 1'b0;
      end
 
      // prefetch row r+2 for the next step
      if (r + 2 < GH) win[2] = world[r+2];
 
      @(negedge clk);
    end
  endtask
 
  // ---------------------------------------------------------------- scenario
  localparam int SandX = 39, SandY0 = 70;   // on a core boundary on purpose
  localparam int WaterX = 100, WaterY0 = 80;
  localparam int GolX = 60, GolY = 20;      // centre of the GOL patch
  localparam int StoneX = 120, StoneY = 30;
 
  int sand_row, water_row, alive, frames_checked;
  bit horizontal, vertical;
 
  initial begin
    for (int y = 0; y < GH; y++) world[y] = '0;
 
    // stone floor along the bottom, and one stone cell floating in the air
    for (int x = 0; x < GW; x++) put(x, GH - 1, 4'(MatStone), 4'h0);
    put(StoneX, StoneY, 4'(MatStone), 4'h0);
 
    // one sand cell and one water cell in open air
    put(SandX, SandY0, 4'(MatSand), 4'h0);
    put(WaterX, WaterY0, 4'(MatWater), 4'h0);
 
    // GOL: a 7x7 patch of dead cells with a horizontal blinker in the middle.
    // (The rule only lets solid, non-spreading cells come alive, so it needs a dead patch.)
    for (int y = GolY - 3; y <= GolY + 3; y++)
      for (int x = GolX - 3; x <= GolX + 3; x++) put(x, y, 4'(MatGOL), 4'h0);
    for (int x = GolX - 1; x <= GolX + 1; x++) put(x, GolY, 4'(MatGOL), 4'h8);
 
    repeat (50) @(negedge clk);
    sim_running = 1'b1;
    repeat (2) @(negedge clk);
 
    $display("\n=== start ===");
    show("GOL patch", GolX - 3, GolY - 3, 7, 7);
 
    // ---- first 6 frames: watch GOL closely
    for (int f = 1; f <= 6; f++) begin
      run_frame();
      $display("\n--- after frame %0d ---", f);
      show("GOL patch", GolX - 3, GolY - 3, 7, 7);
 
      alive = 0;
      for (int y = GolY - 3; y <= GolY + 3; y++)
        for (int x = GolX - 3; x <= GolX + 3; x++)
          if (alive_at(x, y)) alive++;
      horizontal = alive_at(GolX - 1, GolY) && alive_at(GolX, GolY) && alive_at(GolX + 1, GolY);
      vertical   = alive_at(GolX, GolY - 1) && alive_at(GolX, GolY) && alive_at(GolX, GolY + 1);
 
      check(alive == 3, $sformatf("GOL: 3 cells alive (found %0d)", alive));
      check((f % 2 == 1) ? vertical : horizontal,
            $sformatf("GOL: blinker is %s", (f % 2 == 1) ? "vertical" : "horizontal"));
      check(count_mat(4'(MatGOL)) == 49, $sformatf("GOL: patch still 49 cells (found %0d)",
                                                    count_mat(4'(MatGOL))));
    end
 
    // ---- sand and water after those 6 frames
    $display("\n=== falling materials after 6 frames ===");
    sand_row  = first_row_of(4'(MatSand));
    water_row = first_row_of(4'(MatWater));
    check(count_mat(4'(MatSand)) == 1, $sformatf("sand: exactly one cell (found %0d)",
                                                  count_mat(4'(MatSand))));
    check(sand_row > SandY0, $sformatf("sand: moved down from row %0d (now row %0d, col %0d)",
                                       SandY0, sand_row, first_col_of(4'(MatSand))));
    check(count_mat(4'(MatWater)) == 1, $sformatf("water: exactly one cell (found %0d)",
                                                   count_mat(4'(MatWater))));
    check(water_row >= WaterY0, $sformatf("water: not above its start row %0d (now row %0d)",
                                          WaterY0, water_row));
 
    // ---- let everything settle
    for (int f = 7; f <= 40; f++) run_frame();
 
    $display("\n=== after 40 frames ===");
    sand_row  = first_row_of(4'(MatSand));
    water_row = first_row_of(4'(MatWater));
    show("around the sand", SandX - 4, GH - 6, 9, 6);
    show("around the water", WaterX - 6, GH - 6, 13, 6);
 
    check(count_mat(4'(MatSand)) == 1, $sformatf("sand: still exactly one cell (found %0d)",
                                                  count_mat(4'(MatSand))));
    check(sand_row == GH - 2, $sformatf("sand: resting on the floor, row %0d (now row %0d)",
                                        GH - 2, sand_row));
    check(count_mat(4'(MatWater)) == 1, $sformatf("water: still exactly one cell (found %0d)",
                                                   count_mat(4'(MatWater))));
    check(water_row == GH - 2, $sformatf("water: on the floor, row %0d (now row %0d)",
                                         GH - 2, water_row));
    check(mat_at(StoneX, StoneY) == 4'(MatStone), "stone: floating cell has not moved");
    check(count_mat(4'(MatStone)) == GW + 1, $sformatf("stone: %0d cells (found %0d)", GW + 1,
                                                        count_mat(4'(MatStone))));
    check(count_mat(4'(MatExplo)) == 0, "no explosion cells appeared");
 
    $display("\n=== %s: %0d check(s) failed ===\n", errors == 0 ? "PASS" : "FAIL", errors);
    $finish;
  end
 
  // overall watchdog
  initial begin
    #200ms;
    $display("FAIL  watchdog: simulation did not finish");
    $finish;
  end
endmodule
 