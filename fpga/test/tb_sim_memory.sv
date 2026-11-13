// The cell layer against real memory: sim_buffers + simulator + memory +
// psram_model, with nothing hand-driven in between.
//
// tb_sim proves the simulator's rules by feeding it a window out of a testbench
// array. What it cannot prove is that the window ever arrives: the orchestration
// -- which row goes in which buffer, when the window slides, which res_buffer
// slot belongs to which grid row, and where in the SRAM a finished row lands --
// lives entirely in sim_buffers. This runs two generations and checks both ends
// of that:
//
//   1. At every pulse_new_row the three buffers hold rows [y-1, y, y+1] of the
//      grid AS IT WAS when the generation started. Off-grid rows read as air.
//      This is what catches a window that slides early, a prefetch that lags,
//      or a row written back on top of one still to be read.
//   2. After each generation the SRAM holds exactly the rows the simulator
//      produced -- captured from res_buffer at pulse_transaction_pair_done,
//      slotted using tb_sim's own convention rather than sim_buffers' internal
//      state. This catches a wrong row address, a swapped pair, and the byte
//      order inside a row.
//
// Generation 2 is the one that matters most: it runs the odd alignment, where
// rows 0 and H-1 close a transaction on their own.
//
// Run: make generate-vcd TOP_MODULE=tb_sim_memory
module tb_sim_memory #(
    // Declared so `make generate-vcd` can pass -G, but deliberately UNUSED: this
    // runs for over a million clocks and Verilator ignores $dumpoff, so dumping
    // would produce an enormous trace. Add $dumpvars by hand over a narrowed run
    // if a waveform is actually needed.
    parameter string DUMPFILEPATH = ""
) ();
  localparam int unsigned W = sim_pkg::CellGridWidth;  // 640
  localparam int unsigned H = sim_pkg::CellGridHeight;  // 360
  localparam int unsigned Px = sim_pkg::BitsPerPixel;
  localparam int unsigned Base = mem_map_pkg::CellGridMemPointer;
  localparam int unsigned Generations = 2;

  // The grid ends exactly on a page boundary, so Fb0 starts at the first byte
  // past it. Filling that page with a sentinel turns a burst that runs one byte
  // long into a failure instead of a corrupt framebuffer three milestones later.
  localparam logic [7:0] Sentinel = 8'hA5;
  localparam int unsigned GuardBytes = 1024;

  logic clk = 0;
  always #1 clk = ~clk;
  logic reset = 1, enable = 0, sim_running = 0;

  // --- memory ---------------------------------------------------------------
  localparam int unsigned N = memory_pkg::NumMemClients;
  localparam int unsigned Sim = memory_pkg::MemClientSimID;

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

  // --- device under test ----------------------------------------------------
  logic pulse_new_row, pulse_row_done, pulse_pair_done;
  sim_pkg::row_buffer_t window[3];
  sim_pkg::row_buffer_t res[2];

  logic sim_mem_toggle;
  memory_pkg::transaction_e sim_mem_transaction;
  memory_pkg::address_t sim_mem_address;
  memory_pkg::message_length_t sim_mem_length;
  logic [7:0] sim_mem_write_data;

  logic commit_valid, sim_commit_res_buffer, frame_restart;
  mem_map_pkg::fb_y_t commit_y;

  sim_buffers dut (
      .clk(clk), .reset(reset), .sim_running(sim_running),
      // No renderer here, so the generation gate is always open. Pacing against
      // the display is tb_fb_compositor's subject, not this one's.
      .fb_ready(1'b1), .fb_row_ready(1'b1),
      .pulse_new_row(pulse_new_row),
      .buffers(window),
      .pulse_transaction_pair_done(pulse_pair_done),
      .pulse_row_done(pulse_row_done),
      .res_buffer(res),
      // Row-commit handshake: exercised by tb_fb_compositor, observed here only
      // so the instantiation stays complete.
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
  end

  // --- reference state ------------------------------------------------------
  // gen_start: the grid the generation started from, i.e. what the window must
  // contain. expect_grid: what the write-back must have left in the SRAM,
  // accumulated from res_buffer as each transaction pair closes.
  logic [7:0] gen_start[H][W];
  logic [7:0] expect_grid[H][W];

  int unsigned row_count = 0;  // pulse_new_row count within the generation
  int unsigned gen = 0;
  int unsigned phase = 0;  // must start equal to simulator's odd_major_evaluation
  int unsigned window_errors = 0, memory_errors = 0, pair_count = 0;
  logic checking = 0, done = 0;

  // tb_sim's convention: which res_buffer slot a row's own result ends up in.
  function automatic int unsigned row_output_slot(input int unsigned y, input int unsigned p);
    return ((y % 2) == p) ? 1 : 0;
  endfunction

  function automatic logic unpaired_edge(input int unsigned y, input int unsigned p);
    return (p != 0) && ((y == 0) || (y == H - 1));
  endfunction

  function automatic sim_pkg::row_buffer_t pack_start_row(input int signed y);
    sim_pkg::row_buffer_t r;
    r = '0;
    if (y >= 0 && y < int'(H))
      for (int unsigned x = 0; x < W; x++) r[x*Px+:Px] = gen_start[y][x];
    return r;  // off-grid rows are air, same padding tb_sim uses
  endfunction

  task automatic check_window(input int unsigned y);
    sim_pkg::row_buffer_t want;
    for (int unsigned b = 0; b < 3; b++) begin
      want = pack_start_row(int'(y) + int'(b) - 1);
      if (window[b] !== want) begin
        if (window_errors < 8)
          $display("FAIL gen %0d row %0d: buffers[%0d] does not hold grid row %0d",
                   gen, y, b, int'(y) + int'(b) - 1);
        window_errors = window_errors + 1;
      end
    end
  endtask

  // res_buffer is final on the cycle pulse_transaction_pair_done is observed:
  // the simulator stopped touching it on the edge that raised the pulse.
  task automatic record_result(input int unsigned y);
    int unsigned slot;
    pair_count = pair_count + 1;
    if (unpaired_edge(y, phase)) begin
      slot = row_output_slot(y, phase);
      for (int unsigned x = 0; x < W; x++) expect_grid[y][x] = res[slot][x*Px+:Px];
    end else begin
      if (y == 0) begin
        $display("FAIL gen %0d: row 0 closed a pair in the even alignment", gen);
        memory_errors = memory_errors + 1;
      end else begin
        for (int unsigned x = 0; x < W; x++) begin
          expect_grid[y-1][x] = res[1][x*Px+:Px];  // upper row of the pair
          expect_grid[y][x]   = res[0][x*Px+:Px];  // lower row of the pair
        end
      end
    end
  endtask

  task automatic verify_memory();
    logic [7:0] got;
    for (int unsigned y = 0; y < H; y++) begin
      for (int unsigned x = 0; x < W; x++) begin
        got = psram.mem[Base+y*W+x];
        if (got !== expect_grid[y][x]) begin
          if (memory_errors < 8)
            $display("FAIL gen %0d cell (%0d,%0d) at 0x%08h: got 0x%02h want 0x%02h",
                     gen, x, y, Base + y * W + x, got, expect_grid[y][x]);
          memory_errors = memory_errors + 1;
        end
      end
    end
    for (int unsigned i = 0; i < GuardBytes; i++) begin
      if (psram.mem[mem_map_pkg::Fb0MemPointer+i] !== Sentinel) begin
        if (memory_errors < 8)
          $display("FAIL gen %0d: wrote 0x%02h past the cell grid at 0x%08h",
                   gen, psram.mem[mem_map_pkg::Fb0MemPointer+i], mem_map_pkg::Fb0MemPointer + i);
        memory_errors = memory_errors + 1;
      end
    end
  endtask

  always @(posedge clk) begin
    if (checking && !done) begin
      if (pulse_new_row) begin
        if (row_count == H) begin
          // Every write-back of the generation has landed by now: the sequencer
          // only reprimes the window after the last row's write finishes.
          verify_memory();
          $display("tb_sim_memory: generation %0d verified (%0d transaction pairs)",
                   gen, pair_count);
          gen_start   = expect_grid;
          row_count   = 0;
          pair_count  = 0;
          gen         = gen + 1;
          phase       = phase ^ 1;
          if (gen == Generations) done = 1;
        end
        if (!done) begin
          check_window(row_count);
          row_count = row_count + 1;
        end
      end else if (pulse_pair_done) begin
        if (row_count == 0) begin
          $display("FAIL: transaction pair closed before any row started");
          memory_errors = memory_errors + 1;
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
        gen_start[y][x] = seed;
        expect_grid[y][x] = seed;
        psram.mem[Base+y*W+x] = seed;
      end
    end
    for (int unsigned i = 0; i < GuardBytes; i++)
      psram.mem[mem_map_pkg::Fb0MemPointer+i] = Sentinel;

    repeat (4) @(posedge clk);
    reset  = 0;
    enable = 1;

    wait (memory_subsystem.initializer_finished);
    @(posedge clk);
    checking = 1;
    sim_running = 1;
    $display("tb_sim_memory: initializer done, running %0d generations of %0d rows",
             Generations, H);

    wait (done);

    if (window_errors != 0 || memory_errors != 0)
      $fatal(1, "tb_sim_memory: %0d window error(s), %0d memory error(s)",
             window_errors, memory_errors);

    $display("tb_sim_memory: %0d generations, window and write-back correct", Generations);
    $finish;
  end

  initial begin
    #40000000;
    $fatal(1, "tb_sim_memory: timeout (gen=%0d row=%0d state=%0d)", gen, row_count, dut.state);
  end
endmodule
