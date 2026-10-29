// Milestone 4 gate: the fixed-priority arbiter inside `memory`.
//
// Five properties, each of which fails in a way that is hard to spot in a
// waveform and easy to spot here:
//
//   (a) Ownership NEVER changes while the controller is busy. The toggle
//       handshake has no abort, so a revoked grant means two clients' bytes
//       interleaved on the bus -- corruption, not a stall. This is checked
//       continuously by a monitor rather than at the end.
//   (b) Nothing is granted before memory_initializer reports finished. Touching
//       the device before both dice are configured is a hardware-only failure.
//   (c) Priority holds: with several clients requesting at once, the
//       lowest-numbered eligible one wins.
//   (d) A client that is requesting but NOT allowed by the policy mask is never
//       granted -- and is not dropped either; it runs as soon as it is allowed.
//   (e) Every client eventually completes. A priority arbiter can starve the
//       bottom of the order, so this is the check that the design yields.
//
// Run: make generate-vcd TOP_MODULE=tb_mem_arbiter
module tb_mem_arbiter #(
    parameter string DUMPFILEPATH = ""  // overwritten when compiling with make
) ();
  localparam int unsigned N = memory_pkg::NumMemClients;
  localparam int unsigned Base = mem_map_pkg::Fb0MemPointer;
  localparam int unsigned Len = 4;

  logic clk = 0;
  always #1 clk = ~clk;
  logic reset = 1, enable = 0;

  logic [N-1:0] client_toggle = '0;
  memory_pkg::transaction_e client_transaction[N];
  memory_pkg::address_t client_address[N];
  memory_pkg::message_length_t client_length[N];
  logic [7:0] client_write_data[N];
  logic [N-1:0] client_allow = '1;
  logic [N-1:0] client_busy, client_data_on_cable, client_grant;
  logic [7:0] read_data;

  wire out_clk, out_clk_n, cs_n, reset_n, rwds;
  wire [7:0] dq;

  memory dut (
      .clk, .reset, .enable,
      .client_toggle, .client_next_transaction(client_transaction), .client_address, .client_length,
      .client_write_data, .client_allow,
      .client_busy, .client_data_on_cable, .client_grant, .read_data,
      .out_clk, .out_clk_n, .cs_n, .reset_n, .dq, .rwds
  );

  psram_model psram (
      .cs_n(cs_n), .ck(out_clk), .reset_n(reset_n), .dq(dq), .rwds(rwds)
  );

  int unsigned errors = 0;
  int unsigned completions[N];
  int unsigned grant_order[16];
  int unsigned grant_count = 0;

  task automatic check(input string name, input logic condition);
    if (condition !== 1'b1) begin
      $display("FAIL %s", name);
      errors = errors + 1;
    end
  endtask

  // --- (a) ownership is stable while busy, and (b) nothing before init -------
  // Continuous monitors: a one-cycle violation anywhere in the run is caught.
  logic [N-1:0] grant_q;
  logic busy_q;
  always @(posedge clk) begin
    if (!reset) begin
      if (dut.controller_busy && busy_q && (client_grant != grant_q)) begin
        $display("FAIL ownership changed mid-transaction: %b -> %b", grant_q, client_grant);
        errors = errors + 1;
      end
      if (!dut.initializer_finished && |client_grant) begin
        $display("FAIL grant %b asserted before initializer finished", client_grant);
        errors = errors + 1;
      end
    end
    grant_q <= client_grant;
    busy_q  <= dut.controller_busy;
  end

  // Record the order grants are handed out, and count completions per client.
  logic [N-1:0] grant_prev;
  always @(posedge clk) begin
    if (!reset) begin
      for (int unsigned i = 0; i < N; i++) begin
        if (client_grant[i] && !grant_prev[i] && grant_count < 16) begin
          grant_order[grant_count] = i;
          grant_count = grant_count + 1;
        end
        if (!client_grant[i] && grant_prev[i]) completions[i] = completions[i] + 1;
      end
    end
    grant_prev <= client_grant;
  end

  // Feed write data so a granted transaction actually moves bytes.
  always @(posedge clk) begin
    for (int unsigned i = 0; i < N; i++)
      if (client_data_on_cable[i]) client_write_data[i] <= client_write_data[i] + 8'd1;
  end

  // A client asks for the bus by inverting its toggle while its busy is low.
  task automatic request(input int unsigned i);
    client_transaction[i] = memory_pkg::WRITE;
    // Separate addresses so a mis-muxed transaction shows up as a wrong write.
    client_address[i] = memory_pkg::address_t'(Base + i * 256);
    client_length[i] = memory_pkg::message_length_t'(Len);
    client_write_data[i] = 8'(8'h10 * (i + 1));
    client_toggle[i] = ~client_toggle[i];
  endtask

  initial begin
    if (DUMPFILEPATH != "") begin
      $dumpfile(DUMPFILEPATH);
      $dumpvars(0, tb_mem_arbiter);
    end

    for (int unsigned i = 0; i < N; i++) begin
      client_transaction[i] = memory_pkg::NONE;
      client_address[i] = '0;
      client_length[i] = '0;
      client_write_data[i] = '0;
      completions[i] = 0;
    end

    // --- (b') a toggle held across reset is NOT an edge --------------------
    // The toggle protocol has no resting level: both 0 and 1 are idle. So the
    // arbiter's stored copy must TRACK the input while in reset, not clear to
    // zero -- otherwise a client sitting at 1 across a reset looks like it
    // inverted its bit the instant reset releases, and the arbiter fabricates
    // a transaction from whatever happens to be on the request bus.
    //
    // This is not hypothetical: spi_processor's mem_toggle has no reset at
    // all, so it genuinely does survive a reset at 1 after an odd number of
    // transactions. memory_controller.sv:152 handles the same hazard the same
    // way.
    client_toggle[memory_pkg::MemClientSpiID] = 1'b1;
    client_transaction[memory_pkg::MemClientSpiID] = memory_pkg::WRITE;
    client_address[memory_pkg::MemClientSpiID] = memory_pkg::address_t'(Base);
    client_length[memory_pkg::MemClientSpiID] = memory_pkg::message_length_t'(Len);

    repeat (4) @(posedge clk);
    reset  = 0;
    enable = 1;

    wait (dut.initializer_finished);
    $display("tb_mem_arbiter: initializer finished, %0d grants so far (must be 0)", grant_count);
    check("no grant issued during initialisation", grant_count == 0);

    repeat (20) @(posedge clk);
    check("toggle held across reset did not fabricate a request", grant_count == 0);
    check("toggle held across reset left the client idle",
          client_busy[memory_pkg::MemClientSpiID] === 1'b0);

    // --- (c) priority order ------------------------------------------------
    // Request from the BOTTOM of the priority order upward, so a design that
    // simply served requests in arrival order would produce the reverse result.
    @(posedge clk);
    for (int unsigned i = 0; i < N; i++) request(N - 1 - i);
    grant_count = 0;

    wait (grant_count == N);
    $display("tb_mem_arbiter: grant order was %0d %0d %0d %0d (expected 0 1 2 3)",
             grant_order[0], grant_order[1], grant_order[2], grant_order[3]);
    for (int unsigned i = 0; i < N; i++)
      check($sformatf("grant %0d went to client %0d", i, i), grant_order[i] == i);

    // --- (e) everyone completed -------------------------------------------
    // Drain first, then check. The completion counter updates on the same edge
    // busy drops, so checking inside the wait loop races the monitor.
    for (int unsigned i = 0; i < N; i++) wait (!client_busy[i]);
    repeat (2) @(posedge clk);
    for (int unsigned i = 0; i < N; i++)
      check($sformatf("client %0d completed", i), completions[i] >= 1);

    // --- (d) a disallowed client waits, then runs --------------------------
    // Deny the top-priority client and request from it alone. It must not be
    // granted while denied, and must not be forgotten either.
    @(posedge clk);
    client_allow = '1;
    client_allow[memory_pkg::MemClientScanoutID] = 1'b0;
    request(memory_pkg::MemClientScanoutID);
    grant_count = 0;

    repeat (400) @(posedge clk);
    check("denied client was not granted", grant_count == 0);
    check("denied client is still pending", client_busy[memory_pkg::MemClientScanoutID] === 1'b1);

    client_allow[memory_pkg::MemClientScanoutID] = 1'b1;
    wait (grant_count == 1);
    check("client runs once allowed", grant_order[0] == memory_pkg::MemClientScanoutID);
    wait (!client_busy[memory_pkg::MemClientScanoutID]);

    // --- a denied high-priority client must not block a lower one ----------
    // The whole point of allow-vs-priority being separate: denying the top
    // client hands the bus to the next eligible one rather than stalling.
    @(posedge clk);
    client_allow = '1;
    client_allow[memory_pkg::MemClientScanoutID] = 1'b0;
    request(memory_pkg::MemClientScanoutID);
    request(memory_pkg::MemClientCellsID);
    grant_count = 0;

    wait (grant_count == 1);
    check("denied top client did not block the lower one",
          grant_order[0] == memory_pkg::MemClientCellsID);

    if (errors != 0) $fatal(1, "tb_mem_arbiter: %0d check(s) failed", errors);
    $display("tb_mem_arbiter: all checks passed");
    $finish;
  end

  initial begin
    #2000000;
    $fatal(1, "tb_mem_arbiter: timeout (init=%0b grants=%0d busy=%b)",
           dut.initializer_finished, grant_count, client_busy);
  end
endmodule
