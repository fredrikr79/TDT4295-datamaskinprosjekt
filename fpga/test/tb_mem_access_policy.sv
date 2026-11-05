// Self-checking tests for mem_access_policy.
//
// The module is combinational, so this needs no clock: set the raster phase and
// sim state, then read the mask back.
//
// The row that matters is "SPI never allowed during active scan". That is the
// invariant protecting the streamers from being starved at the moment the
// raster starts, and the raster cannot be back-pressured -- so a violation is a
// wrong pixel, not a stall.
//
// Run: make generate-vcd TOP_MODULE=tb_mem_access_policy
module tb_mem_access_policy #(
    parameter string DUMPFILEPATH = ""  // overwritten when compiling with make
) ();
  import memory_pkg::*;

  logic v_blank, near_frame_end, sim_running;
  logic [NumMemClients-1:0] allow;

  int unsigned errors = 0;

  mem_access_policy dut (
      .v_blank(v_blank),
      .near_frame_end(near_frame_end),
      .sim_running(sim_running),
      .allow(allow)
  );

  task automatic check(input string name, input logic got, input logic want);
    if (got !== want) begin
      $display("FAIL %-46s got %b, want %b", name, got, want);
      errors = errors + 1;
    end
  endtask

  // Apply a raster phase and assert the whole mask at once.
  task automatic expect_mask(input string name, input logic vb, input logic nfe, input logic sr,
                             input logic want_cells, input logic want_spi);
    v_blank = vb;
    near_frame_end = nfe;
    sim_running = sr;
    #1;
    check({name, " / cells"}, allow[MemClientCellsID], want_cells);
    check({name, " / spi"}, allow[MemClientSpiID], want_spi);
    // The display path is unconditional in every phase, so it is asserted on
    // every case rather than given rows of its own.
    check({name, " / scanout"}, allow[MemClientScanoutID], 1'b1);
    check({name, " / hud"}, allow[MemClientHudID], 1'b1);
  endtask

  initial begin
    if (DUMPFILEPATH != "") begin
      $dumpfile(DUMPFILEPATH);
      $dumpvars(0);
    end

    //                                     vb nfe sr | cells spi
    // --- sim running: SPI confined to vertical blanking ----------------
    expect_mask("active scan", 0, 0, 1, 1, 0);
    expect_mask("vblank, before guard", 1, 0, 1, 1, 1);
    expect_mask("vblank, guard window", 1, 1, 1, 1, 0);

    // --- sim stopped: SPI gets the bus continuously --------------------
    expect_mask("stopped, active scan", 0, 0, 0, 1, 1);
    expect_mask("stopped, vblank", 1, 0, 0, 1, 1);
    expect_mask("stopped, guard window", 1, 1, 0, 1, 1);

    // near_frame_end should never be asserted outside blanking, but the policy
    // must not grant SPI the bus even if the VGA phase logic misbehaves.
    expect_mask("active scan with stray guard", 0, 1, 1, 1, 0);

    // Exhaustive sweep of the three inputs. The display path and the cell
    // streamer are never denied in any phase -- scan-out because it cannot be
    // back-pressured at all, the streamer because priority already makes it
    // yield and denying it as well would only add a way to stall it.
    for (int unsigned i = 0; i < 8; i++) begin
      v_blank = i[0];
      near_frame_end = i[1];
      sim_running = i[2];
      #1;
      check($sformatf("scanout always allowed (case %0d)", i), allow[MemClientScanoutID], 1'b1);
      check($sformatf("hud always allowed (case %0d)", i), allow[MemClientHudID], 1'b1);
      check($sformatf("cells always allowed (case %0d)", i), allow[MemClientCellsID], 1'b1);
      // SPI is the only conditional client; restate its rule independently of
      // the table above so a change to the policy has to break both.
      check($sformatf("spi matches its rule (case %0d)", i), allow[MemClientSpiID],
            !sim_running || (v_blank && !near_frame_end));
    end

    if (errors != 0) $fatal(1, "tb_mem_access_policy: %0d check(s) failed", errors);
    $display("tb_mem_access_policy: all checks passed");
    $finish;
  end
endmodule
