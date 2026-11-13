// The render stage against real memory: sim_buffers + simulator + fb_compositor
// + memory + psram_model, with the real vga block supplying frame_start.
//
// tb_sim_memory proves the simulation's rows reach the CELL GRID. This proves
// the other destination: that the same rows, composited with the HUD, reach the
// framebuffer the scan-out is not reading, and that the two buffers swap.
//
// Left to itself the simulation runs FASTER than the display -- a generation
// takes well under a VGA frame -- and with only two framebuffers that means
// producing rows the renderer has nowhere to put. So sim_buffers is gated: a
// generation may only begin when the renderer is between frames AND the
// previous frame has actually been swapped onto the screen. The result is a
// strict 1:1 lockstep, and most of what is checked here is that lockstep
// holding rather than the pixels alone.
//
// What is checked, per frame:
//
//   1. The written buffer holds every grid row exactly once, at the address
//      mem_map_pkg::fb_byte_addr predicts, in the right byte order. The expected
//      bytes come from res_buffer at pulse_transaction_pair_done using tb_sim's
//      own slot convention -- NOT from the new commit handshake -- so a wrong
//      commit_y or commit_res_buffer fails rather than cancelling out.
//   2. The HUD lands on top, from REAL sprite pixels read out of the psram
//      model by hud_line_buffer: four sprites covering see-through holes (the
//      simulation must show through), overlap (a later ID wins where it paints
//      but does not erase an earlier one through its holes), right-edge
//      clipping, and an inactive sprite that must not appear. A seam optimised
//      away fails, and so does one that leaks.
//   3. The buffer being DISPLAYED still holds the previous frame, byte for
//      byte. This is the tearing check: writing the wrong buffer, or starting a
//      generation before the swap landed, corrupts it.
//   4. No generation ever starts while a swap is still pending. That is the
//      gate itself; if it ever fails, check 3 is what corrupts.
//   5. Every finished frame is matched by exactly one swap, and consecutive
//      frames alternate buffers.
//   6. overrun never asserts -- no commit is lost mid-frame.
//
// Run: make generate-vcd TOP_MODULE=tb_fb_compositor
module tb_fb_compositor #(
    // Declared so `make generate-vcd` can pass -G, but deliberately UNUSED:
    // this runs for millions of clocks and Verilator ignores $dumpoff, so
    // dumping would produce an enormous trace. Add $dumpvars by hand over a
    // narrowed run if a waveform is actually needed.
    parameter string DUMPFILEPATH = ""
) ();
  localparam int unsigned W = sim_pkg::CellGridWidth;  // 640
  localparam int unsigned H = sim_pkg::CellGridHeight;  // 360
  localparam int unsigned Px = sim_pkg::BitsPerPixel;
  localparam int unsigned CellBase = mem_map_pkg::CellGridMemPointer;
  localparam int unsigned KeptFrames = 2;

  // A sentinel page past the framebuffer region turns a burst that runs off the
  // end of a buffer into a failure here rather than a corrupt picture later.
  // Fb1 is immediately followed by the sprite region, so the sentinel page sits
  // at the very start of it and the sprites are placed a page further on. A
  // compositor burst that runs past the end of Fb1 lands in the sentinel.
  localparam logic [7:0] Sentinel = 8'hA5;
  localparam int unsigned GuardBytes = 1024;
  localparam int unsigned GuardBase = mem_map_pkg::Fb1MemPointer + mem_map_pkg::FbBytes;
  // Stride must exceed the largest sprite: 64 x 40 x 2 = 5120 bytes.
  localparam int unsigned SpriteStride = 8192;
  localparam int unsigned SpriteBase = mem_map_pkg::SpritesMemPointer + GuardBytes;

  // --- sprites --------------------------------------------------------------
  // Four sprites, each exercising something different:
  //   0  plainly on screen, with see-through holes
  //   1  overlaps sprite 0 -- must win where it paints, and must NOT erase
  //      sprite 0 where it is see-through
  //   2  runs off the right edge, so the row has to be clipped
  //   3  inactive, and must therefore not appear at all
  localparam int unsigned NumSprites = 4;
  localparam int unsigned SprX[NumSprites] = '{200, 232, 620, 100};
  localparam int unsigned SprY[NumSprites] = '{100, 108, 200, 300};
  localparam int unsigned SprW[NumSprites] = '{64, 48, 40, 32};
  localparam int unsigned SprH[NumSprites] = '{40, 24, 16, 16};
  localparam logic SprActive[NumSprites] = '{1'b1, 1'b1, 1'b1, 1'b0};

  localparam logic [15:0] SeeThroughColour = 16'hF81F;

  // Sprite pixel content. Distinct per sprite and per position so a swapped row,
  // a swapped sprite or an off-by-one in the source address all show up. Every
  // fourth pixel on the diagonal is the see-through colour.
  function automatic logic [15:0] spr_pixel(input int unsigned s, input int unsigned sx,
                                            input int unsigned sy);
    logic [15:0] c;
    if (((sx + sy) % 4) == 0) return SeeThroughColour;
    c = 16'((s << 13) | ((sy & 7) << 9) | ((sx & 31) << 3) | 3'h5);
    // Must not collide with the see-through colour by accident.
    if (c == SeeThroughColour) c = 16'h0001;
    return c;
  endfunction

  // Modelled, not called from color_pkg, so a change to the HUD code layout has
  // to be made in both places deliberately: bit 7 marks a HUD code, the rest is
  // a 3-2-2 RGB cube taken from the top bits of each RGB565 channel.
  function automatic logic [7:0] rgb565_to_hud_color(input logic [15:0] c);
    return {1'b1, c[15:13], c[10:9], c[4:3]};
  endfunction

  logic clk = 0;
  always #1 clk = ~clk;
  logic reset = 1, enable = 0, sim_running = 0;

  // --- raster ---------------------------------------------------------------
  // The real vga block, so the swap lands on the frame boundary that ships.
  logic active_area, pixel_pulse, v_blank, h_blank, frame_start, near_frame_end, line_end;
  vga_sync_params::x_coordinate_t coord_x;
  vga_sync_params::y_coordinate_t coord_y;
  color_pkg::color_t vga_color;
  wire [3:0] vga_r, vga_g, vga_b;
  wire h_sync, v_sync;

  assign vga_color = color_pkg::Black;

  vga raster (
      .clk(clk), .reset(reset), .enable(enable), .color(vga_color),
      .vga_r(vga_r), .vga_g(vga_g), .vga_b(vga_b),
      .coord_x(coord_x), .coord_y(coord_y),
      .active_area(active_area), .pixel_pulse(pixel_pulse),
      .h_sync(h_sync), .v_sync(v_sync),
      .v_blank(v_blank), .h_blank(h_blank),
      .frame_start(frame_start), .near_frame_end(near_frame_end), .line_end(line_end)
  );

  // --- memory ---------------------------------------------------------------
  localparam int unsigned N = memory_pkg::NumMemClients;
  localparam int unsigned Sim = memory_pkg::MemClientSimID;
  localparam int unsigned Fb = memory_pkg::MemClientCompositorID;
  localparam int unsigned Hud = memory_pkg::MemClientHudID;

  logic [N-1:0] client_toggle;
  memory_pkg::transaction_e client_transaction[N];
  memory_pkg::address_t client_address[N];
  memory_pkg::message_length_t client_length[N];
  logic [7:0] client_write_data[N];
  logic [N-1:0] client_allow = '1;
  logic [N-1:0] client_busy, client_data_on_cable, client_grant;
  logic [7:0] mem_read_data;

  wire out_clk, out_clk_n, cs_n, reset_n, rwds;
  wire [7:0] dq;

  memory memory_subsystem (
      .clk(clk), .reset(reset), .enable(enable),
      .client_toggle, .client_next_transaction(client_transaction),
      .client_address, .client_length, .client_write_data, .client_allow,
      .client_busy, .client_data_on_cable, .client_grant, .read_data(mem_read_data),
      .out_clk, .out_clk_n, .cs_n, .reset_n, .dq, .rwds
  );

  psram_model psram (
      .cs_n(cs_n), .ck(out_clk), .reset_n(reset_n), .dq(dq), .rwds(rwds)
  );

  // --- simulation layer -----------------------------------------------------
  logic pulse_new_row, pulse_row_done, pulse_pair_done;
  sim_pkg::row_buffer_t window[3];
  sim_pkg::row_buffer_t res[2];

  logic commit_valid, sim_commit_res_buffer, frame_restart;
  mem_map_pkg::fb_y_t commit_y;

  logic sim_mem_toggle;
  memory_pkg::transaction_e sim_mem_transaction;
  memory_pkg::address_t sim_mem_address;
  memory_pkg::message_length_t sim_mem_length;
  logic [7:0] sim_mem_write_data;

  sim_buffers sim_memory (
      .clk(clk), .reset(reset), .sim_running(sim_running),
      .fb_ready(fb_ready_for_frame), .fb_row_ready(fb_row_idle),
      .pulse_new_row(pulse_new_row),
      .buffers(window),
      .pulse_transaction_pair_done(pulse_pair_done),
      .pulse_row_done(pulse_row_done),
      .res_buffer(res),
      .pulse_row_commit(commit_valid), .commit_y(commit_y), .commit_res_buffer(sim_commit_res_buffer),
      .pulse_frame_restart(frame_restart),
      .mem_toggle(sim_mem_toggle), .mem_transaction(sim_mem_transaction),
      .mem_address(sim_mem_address), .mem_length(sim_mem_length),
      .mem_write_data(sim_mem_write_data),
      .mem_busy(client_busy[Sim]), .mem_data_on_cable(client_data_on_cable[Sim]),
      .mem_read_data(mem_read_data)
  );

  simulator sim_core (
      .clk(clk), .pulse_new_row(pulse_new_row), .sim_running(sim_running),
      .buffers(window),
      .pulse_transaction_pair_done(pulse_pair_done), .pulse_row_done(pulse_row_done),
      .res_buffer(res)
  );

  // --- HUD layer ------------------------------------------------------------
  // The real rasterizer, reading real sprite pixels out of the psram model. The
  // sprite LUT is driven straight from the testbench rather than through hud.sv
  // so the subject stays the rasterizer, not the SPI path that populates it.
  mem_map_pkg::fb_x_t hud_x;
  mem_map_pkg::fb_y_t hud_y;
  logic hud_opaque, hud_row_ready, hud_overflow;
  color_pkg::hud_color_t hud_color;

  hud_pkg::sprite_inst_entry_t spriteLUT[hud_pkg::SpriteLutDepth];
  hud_pkg::sprite_mem_id hud_lookup_mem_id;
  logic [31:0] hud_lookup_mem_addr;
  logic [31:0] sprite_base_model[NumSprites];

  assign hud_lookup_mem_addr = (32'(hud_lookup_mem_id) < NumSprites)
      ? sprite_base_model[hud_lookup_mem_id] : 32'd0;

  logic hud_mem_toggle;
  memory_pkg::transaction_e hud_mem_transaction;
  memory_pkg::address_t hud_mem_address;
  memory_pkg::message_length_t hud_mem_length;

  hud_line_buffer hud_painter (
      .clk(clk), .reset(reset),
      .spriteLUT(spriteLUT),
      .lookup_mem_id(hud_lookup_mem_id), .lookup_mem_addr(hud_lookup_mem_addr),
      .pulse_frame_restart(frame_restart),
      .pulse_row_commit(commit_valid), .commit_y(commit_y),
      .row_ready(hud_row_ready),
      .px_x(hud_x), .px_opaque(hud_opaque), .px_color(hud_color),
      .mem_toggle(hud_mem_toggle), .mem_transaction(hud_mem_transaction),
      .mem_address(hud_mem_address), .mem_length(hud_mem_length),
      .mem_busy(client_busy[Hud]), .mem_data_on_cable(client_data_on_cable[Hud]),
      .mem_read_data(mem_read_data),
      .shortlist_overflow(hud_overflow)
  );

  logic fb_sel, fb_swap_pending, fb_frame_done, fb_frame_idle, fb_row_idle, fb_overrun;

  // Mirrors main.sv: the simulation may only start a generation when the
  // renderer is between frames and the previous frame is already on screen.
  logic fb_ready_for_frame;
  assign fb_ready_for_frame = fb_frame_idle && !fb_swap_pending && !fb_frame_done;

  logic fb_mem_toggle;
  memory_pkg::transaction_e fb_mem_transaction;
  memory_pkg::address_t fb_mem_address;
  memory_pkg::message_length_t fb_mem_length;
  logic [7:0] fb_mem_write_data;

  fb_compositor dut (
      .clk(clk), .reset(reset),
      .pulse_row_commit(commit_valid), .commit_y(commit_y), .commit_res_buffer(sim_commit_res_buffer),
      .pulse_frame_restart(frame_restart),
      .res_buffer(res),
      .hud_x(hud_x), .hud_y(hud_y), .hud_opaque(hud_opaque), .hud_color(hud_color),
      .hud_row_ready(hud_row_ready),
      .fb_sel(fb_sel), .frame_written(fb_frame_done), .frame_idle(fb_frame_idle), .row_idle(fb_row_idle),
      .mem_toggle(fb_mem_toggle), .mem_transaction(fb_mem_transaction),
      .mem_address(fb_mem_address), .mem_length(fb_mem_length),
      .mem_write_data(fb_mem_write_data),
      .mem_busy(client_busy[Fb]), .mem_data_on_cable(client_data_on_cable[Fb]),
      .overrun(fb_overrun)
  );

  // The swap register, mirroring main.sv.
  always_ff @(posedge clk) begin
    if (reset) begin
      fb_sel <= 1'b0;
      fb_swap_pending <= 1'b0;
    end else begin
      if (fb_frame_done) fb_swap_pending <= 1'b1;
      if (frame_start && fb_swap_pending) begin
        fb_sel <= ~fb_sel;
        fb_swap_pending <= 1'b0;
      end
    end
  end

  always_comb begin
    client_toggle = '0;
    for (int unsigned i = 0; i < N; i++) begin
      client_transaction[i] = memory_pkg::NONE;
      client_address[i] = '0;
      client_length[i] = '0;
      client_write_data[i] = '0;
    end

    client_toggle[Sim]      = sim_mem_toggle;
    client_transaction[Sim] = sim_mem_transaction;
    client_address[Sim]     = sim_mem_address;
    client_length[Sim]      = sim_mem_length;
    client_write_data[Sim]  = sim_mem_write_data;

    client_toggle[Hud]      = hud_mem_toggle;
    client_transaction[Hud] = hud_mem_transaction;
    client_address[Hud]     = hud_mem_address;
    client_length[Hud]      = hud_mem_length;

    client_toggle[Fb]      = fb_mem_toggle;
    client_transaction[Fb] = fb_mem_transaction;
    client_address[Fb]     = fb_mem_address;
    client_length[Fb]      = fb_mem_length;
    client_write_data[Fb]  = fb_mem_write_data;
  end

  // --- reference state ------------------------------------------------------
  // expect_grid: what the simulator has produced so far, accumulated from
  // res_buffer as each transaction pair closes -- the same independent route
  // tb_sim_memory uses, so it does not depend on the handshake under test.
  // expect_frame: that image frozen at the last commit of a kept frame, which
  // is what the framebuffer must hold once the frame finishes.
  logic [7:0] expect_grid[H][W];
  logic [7:0] expect_frame[H][W];
  logic [7:0] prev_frame[H][W];

  int unsigned row_count = 0;  // pulse_new_row count within the generation
  int unsigned phase = 0;  // must start equal to simulator's odd_major_evaluation
  int unsigned commits_seen = 0;  // commit count within the compositor's frame
  int unsigned errors = 0, kept = 0, swaps = 0, frames_done = 0;
  int unsigned swaps_at_prev = 0, settle = 0;
  logic checking = 0, done = 0;

  // Mirrors of the compositor's own choices, derived from its inputs only.
  logic wr_buf = 1;
  logic prev_buf = 0, have_prev = 0;

  // tb_sim's convention: which res_buffer slot a row's own result ends up in.
  function automatic int unsigned row_output_slot(input int unsigned y, input int unsigned p);
    return ((y % 2) == p) ? 1 : 0;
  endfunction

  function automatic logic unpaired_edge(input int unsigned y, input int unsigned p);
    return (p != 0) && ((y == 0) || (y == H - 1));
  endfunction

  // What the HUD layer should contribute at (x,y): sprites in ascending ID
  // order, a later one winning only where it actually paints. A see-through
  // pixel does not erase what an earlier sprite put there.
  task automatic hud_model(input int unsigned x, input int unsigned y, output logic v,
                           output logic [7:0] idx);
    logic [15:0] c;
    v = 1'b0;
    idx = 8'h00;
    for (int unsigned s = 0; s < NumSprites; s++) begin
      if (SprActive[s] && x >= SprX[s] && x < SprX[s] + SprW[s]
          && y >= SprY[s] && y < SprY[s] + SprH[s] && x < W) begin
        c = spr_pixel(s, x - SprX[s], y - SprY[s]);
        if (c != SeeThroughColour) begin
          v   = 1'b1;
          idx = rgb565_to_hud_color(c);
        end
      end
    end
  endtask

  task automatic record_result(input int unsigned y);
    int unsigned slot;
    if (unpaired_edge(y, phase)) begin
      slot = row_output_slot(y, phase);
      for (int unsigned x = 0; x < W; x++) expect_grid[y][x] = res[slot][x*Px+:Px];
    end else begin
      if (y == 0) begin
        $display("FAIL: row 0 closed a pair in the even alignment");
        errors = errors + 1;
      end else begin
        for (int unsigned x = 0; x < W; x++) begin
          expect_grid[y-1][x] = res[1][x*Px+:Px];  // upper row of the pair
          expect_grid[y][x]   = res[0][x*Px+:Px];  // lower row of the pair
        end
      end
    end
  endtask

  // The composite the framebuffer must hold: the simulation byte, replaced by
  // the HUD code wherever the HUD was opaque.
  task automatic verify_frame(input logic buf_sel);
    int unsigned addr;
    logic [7:0] got, want, hud_idx;
    logic hud_v;
    for (int unsigned y = 0; y < H; y++) begin
      for (int unsigned x = 0; x < W; x++) begin
        addr = 32'(mem_map_pkg::fb_byte_addr(buf_sel, mem_map_pkg::fb_x_t'(x),
                                             mem_map_pkg::fb_y_t'(y)));
        got  = psram.mem[addr];
        hud_model(x, y, hud_v, hud_idx);
        want = hud_v ? hud_idx : expect_frame[y][x];
        if (got !== want) begin
          if (errors < 8)
            $display("FAIL frame %0d fb%0d px (%0d,%0d) at 0x%08h: got 0x%02h want 0x%02h",
                     kept, buf_sel, x, y, addr, got, want);
          errors = errors + 1;
        end
      end
    end
  endtask

  // The buffer on screen must still hold the frame it was swapped in with.
  task automatic verify_displayed_intact(input logic buf_sel);
    int unsigned addr;
    logic [7:0] got, want, hud_idx;
    logic hud_v;
    for (int unsigned y = 0; y < H; y++) begin
      for (int unsigned x = 0; x < W; x++) begin
        addr = 32'(mem_map_pkg::fb_byte_addr(buf_sel, mem_map_pkg::fb_x_t'(x),
                                             mem_map_pkg::fb_y_t'(y)));
        got  = psram.mem[addr];
        hud_model(x, y, hud_v, hud_idx);
        want = hud_v ? hud_idx : prev_frame[y][x];
        if (got !== want) begin
          $display("FAIL frame %0d: displayed fb%0d corrupted at (%0d,%0d) 0x%08h: %02h/%02h",
                   kept, buf_sel, x, y, addr, got, want);
          errors = errors + 1;
          return;  // one report is enough; the whole buffer is suspect
        end
      end
    end
  endtask

  task automatic verify_guard();
    for (int unsigned i = 0; i < GuardBytes; i++) begin
      if (psram.mem[GuardBase+i] !== Sentinel) begin
        $display("FAIL frame %0d: wrote 0x%02h past the framebuffer region at 0x%08h",
                 kept, psram.mem[GuardBase+i], GuardBase + i);
        errors = errors + 1;
        return;
      end
    end
  endtask

  // Kept frames alternate buffers, so when a frame finishes the OTHER buffer
  // still holds the previous one -- the next frame has not started yet. That is
  // the tearing check: writing the wrong buffer, or starting before the swap
  // landed, shows up here.
  task automatic verify_finished_frame();
    verify_frame(wr_buf);
    verify_guard();

    if (have_prev) begin
      verify_displayed_intact(prev_buf);
      // Exactly one swap since the previous kept frame -- the previous frame's.
      // This frame's own swap has not happened yet; swap_pending was raised a
      // few cycles ago and lands at the next frame_start.
      if (swaps != swaps_at_prev + 1) begin
        $display("FAIL frame %0d: %0d swaps since the previous frame, expected 1",
                 kept, swaps - swaps_at_prev);
        errors = errors + 1;
      end
      if (wr_buf == prev_buf) begin
        $display("FAIL frame %0d: wrote fb%0d again instead of alternating", kept, wr_buf);
        errors = errors + 1;
      end
      if (fb_sel !== prev_buf) begin
        $display("FAIL frame %0d: displaying fb%0d, expected the previous frame's fb%0d",
                 kept, fb_sel, prev_buf);
        errors = errors + 1;
      end
    end else if (wr_buf == fb_sel) begin
      $display("FAIL frame 0: wrote the buffer already being displayed (fb%0d)", wr_buf);
      errors = errors + 1;
    end

    $display("tb_fb_compositor: frame %0d verified into fb%0d", kept, wr_buf);

    prev_frame    = expect_frame;
    prev_buf      = wr_buf;
    swaps_at_prev = swaps;
    have_prev     = 1;
    kept          = kept + 1;
    if (kept == KeptFrames) done = 1;
  endtask

  // --- bookkeeping ----------------------------------------------------------
  // Everything here is derived from the compositor's INPUTS, never its state,
  // so the checks stay independent of the logic under test.
  //
  // Frames are verified HERE rather than from the initial block: the compositor
  // produces a frame roughly every simulation generation and never waits, so a
  // checker that blocks between frames silently falls behind and ends up
  // comparing the wrong buffer against the wrong generation.
  always @(posedge clk) begin
    if (checking) begin
      if (frame_start && fb_swap_pending) swaps = swaps + 1;
    end

    if (checking && !done) begin
      if (fb_overrun) begin
        $display("FAIL frame %0d: compositor lost a committed row mid-frame", kept);
        errors = errors + 1;
      end
      if (hud_overflow) begin
        $display("FAIL frame %0d: hud rasterizer ran out of shortlist room", kept);
        errors = errors + 1;
      end

      if (fb_frame_done) begin
        frames_done = frames_done + 1;
        settle = 4;  // let the last burst land in the model before reading it
      end else if (settle != 0) begin
        settle = settle - 1;
        if (settle == 0) verify_finished_frame();
      end

      if (commit_valid) begin
        if (commits_seen == 0) begin
          // Every generation now becomes a frame, so there is no keep/drop
          // decision left -- if the gate ever lets a generation start while a
          // frame is still queued, the buffer checks below catch it.
          if (fb_swap_pending) begin
            $display("FAIL frame %0d: generation started while a swap was pending", kept);
            errors = errors + 1;
          end
          wr_buf = ~fb_sel;
        end

        // Freeze the reference image on the frame's last commit: the generation
        // that fed this frame is complete, the next has not touched it yet.
        if (commits_seen == H - 1) expect_frame = expect_grid;

        commits_seen = commits_seen + 1;
        if (commits_seen == H) commits_seen = 0;
      end

      // res_buffer is final on the cycle pulse_transaction_pair_done is seen.
      if (pulse_new_row) begin
        if (row_count == H) begin
          row_count = 0;
          phase = phase ^ 1;
        end
        row_count = row_count + 1;
      end else if (pulse_pair_done) begin
        if (row_count == 0) begin
          $display("FAIL: transaction pair closed before any row started");
          errors = errors + 1;
        end else record_result(row_count - 1);
      end
    end
  end

  initial begin
    // A grid with structure in it: a stone floor, a sand pile, water, and a
    // lattice of lava, so that something actually moves and the result differs
    // row to row. A uniform grid would pass even with the rows swapped.
    for (int unsigned y = 0; y < H; y++) begin
      for (int unsigned x = 0; x < W; x++) begin
        logic [7:0] seed;
        seed = {sim_pkg::MatAir, 4'h0};
        if (y >= H - 10) seed = {sim_pkg::MatStone, 4'h0};
        else if (y > H - 70 && y < H - 20 && x > 100 && x < 200) seed = {sim_pkg::MatSand, 4'h0};
        else if (y > H - 50 && y < H - 20 && x > 300 && x < 380) seed = {sim_pkg::MatWater, 4'h0};
        else if ((x % 37) == 11 && (y % 29) == 7) seed = {sim_pkg::MatLava, 4'h0};
        expect_grid[y][x] = seed;
        psram.mem[CellBase+y*W+x] = seed;
      end
    end
    // Both buffers and the page past them start at the sentinel, so any byte
    // that is not the sentinel afterwards was put there by the compositor.
    for (int unsigned i = 0; i < mem_map_pkg::FbBytes; i++) begin
      psram.mem[mem_map_pkg::Fb0MemPointer+i] = Sentinel;
      psram.mem[mem_map_pkg::Fb1MemPointer+i] = Sentinel;
    end
    for (int unsigned i = 0; i < GuardBytes; i++) psram.mem[GuardBase+i] = Sentinel;

    // Sprite pixels, laid out exactly as the MCU uploads them: RGB565, two
    // bytes per pixel, MSB first, rows back to back. Each sprite gets its own
    // page-aligned base so a wrong base address is obvious rather than subtle.
    for (int unsigned sp = 0; sp < NumSprites; sp++) begin
      logic [15:0] c;
      sprite_base_model[sp] = 32'(SpriteBase + sp * SpriteStride);
      for (int unsigned sy = 0; sy < SprH[sp]; sy++) begin
        for (int unsigned sx = 0; sx < SprW[sp]; sx++) begin
          c = spr_pixel(sp, sx, sy);
          psram.mem[sprite_base_model[sp]+(sy*SprW[sp]+sx)*2]   = c[15:8];
          psram.mem[sprite_base_model[sp]+(sy*SprW[sp]+sx)*2+1] = c[7:0];
        end
      end
    end

    // The sprite LUT the rasterizer reads. Driven straight from here rather
    // than through hud.sv so this stays a test of the rasterizer.
    for (int unsigned i = 0; i < hud_pkg::SpriteLutDepth; i++) begin
      spriteLUT[i] = '0;
    end
    for (int unsigned sp = 0; sp < NumSprites; sp++) begin
      spriteLUT[sp].active = SprActive[sp];
      spriteLUT[sp].id     = hud_pkg::sprite_lut_id'(sp);
      spriteLUT[sp].mem_id = hud_pkg::sprite_mem_id'(sp);
      spriteLUT[sp].x      = 16'(SprX[sp]);
      spriteLUT[sp].y      = 16'(SprY[sp]);
      spriteLUT[sp].w      = 16'(SprW[sp]);
      spriteLUT[sp].h      = 16'(SprH[sp]);
    end

    repeat (4) @(posedge clk);
    reset  = 0;
    enable = 1;

    wait (memory_subsystem.initializer_finished);
    @(posedge clk);
    checking = 1;
    sim_running = 1;
    $display("tb_fb_compositor: initializer done, waiting for %0d kept frames of %0d rows",
             KeptFrames, H);

    // Verification runs in the bookkeeping block above, frame by frame as they
    // finish -- the compositor never waits, so neither can the checker. Swap
    // accounting is per-frame there for the same reason: the design keeps
    // producing frames after the test is satisfied, so a global count would
    // only measure how long this block took to notice.
    wait (done);

    if (frames_done != kept)
      $fatal(1, "tb_fb_compositor: %0d frames announced but %0d verified", frames_done, kept);
    if (errors != 0) $fatal(1, "tb_fb_compositor: %0d error(s)", errors);

    $display("tb_fb_compositor: %0d frames composited and swapped correctly, none dropped",
             kept);
    $finish;
  end

  initial begin
    #120000000;
    $fatal(1, "tb_fb_compositor: timeout (kept=%0d row=%0d commits=%0d)",
           kept, row_count, commits_seen);
  end
endmodule
