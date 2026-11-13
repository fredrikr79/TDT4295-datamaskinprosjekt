// Milestone 3 gate: a MAXIMUM-LENGTH burst must round-trip through `memory`
// and stay inside tCSM at the stricter 125 C grade.
//
// This is the test that justifies memory_pkg::MaxMessageLength. tb_memory only
// confirms memory_initializer issues its six transactions; nothing else drives
// a full-rate array burst, and the two things that break at the maximum length
// are invisible at shorter ones:
//
//   1. message_length_t must be able to REPRESENT MaxMessageLength. The width
//      is $clog2(MaxMessageLength + 1) -- without the +1, a power-of-two length
//      wraps to 0. It still produced the right burst, because the byte counter
//      is mod-MaxMessageLength and transaction_is_finished compares against
//      `length - 1`, but the value was not representable and anything reading
//      it back saw 0. The $fatal below pins that down.
//
//   2. CS# low time scales with the burst, and tCSM is a HARD device limit --
//      4 us at 85 C but only 1 us at 125 C. Exceeding it means a missed refresh,
//      i.e. silent data loss rather than a clean failure. At L = 64 the bus is
//      low for 19 + L = 83 clocks = 830 ns, which is 83 % of the 125 C budget.
//      L = 128 would be 1470 ns and would NOT be safe at that grade.
//
// Run: make generate-vcd TOP_MODULE=tb_burst64
module tb_burst64 #(
    parameter string DUMPFILEPATH = "",  // overwritten when compiling with make
    // Default to the STRICTER grade. Passing at 85 C proves very little, since
    // the budget there is 4x larger than any burst this controller can issue.
    parameter real TcsmNs = 1000.0
) ();
  localparam int unsigned L = memory_pkg::MaxMessageLength;
  // Any region base inside psram_model's backing store works; Fb0 is used
  // because it is page aligned and well clear of the init register space.
  localparam int unsigned Base = mem_map_pkg::Fb0MemPointer;

  logic clk = 0;
  always #1 clk = ~clk;
  logic reset = 1, enable = 0;

  // Drive one arbiter slot; the others stay idle and are never granted.
  localparam int unsigned C = memory_pkg::MemClientSpiID;

  logic [memory_pkg::NumMemClients-1:0] client_toggle = '0;
  memory_pkg::transaction_e client_transaction[memory_pkg::NumMemClients];
  memory_pkg::address_t client_address[memory_pkg::NumMemClients];
  memory_pkg::message_length_t client_length[memory_pkg::NumMemClients];
  logic [7:0] client_write_data[memory_pkg::NumMemClients];
  logic [memory_pkg::NumMemClients-1:0] client_allow = '1;
  logic [memory_pkg::NumMemClients-1:0] client_busy, client_data_on_cable, client_grant;
  logic [7:0] read_data;

  // Aliases so the body below reads the same as a single-client bus.
  wire busy = client_busy[C];
  wire data_on_cable = client_data_on_cable[C];

  wire out_clk, out_clk_n, cs_n, reset_n, rwds;
  wire [7:0] dq;

  memory dut (
      .clk, .reset, .enable,
      .client_toggle, .client_next_transaction(client_transaction), .client_address, .client_length,
      .client_write_data, .client_allow,
      .client_busy, .client_data_on_cable, .client_grant, .read_data,
      .out_clk, .out_clk_n, .cs_n, .reset_n, .dq, .rwds
  );

  psram_model #(.TcsmNs(TcsmNs)) psram (
      .cs_n(cs_n), .ck(out_clk), .reset_n(reset_n), .dq(dq), .rwds(rwds)
  );

  logic [7:0] pattern[L];
  logic [7:0] got[L];
  int unsigned idx = 0;

  // One byte per data_on_cable, mirroring how memory_initializer feeds a write.
  always @(posedge clk) begin
    if (dut.initializer_finished && data_on_cable) begin
      if (client_transaction[C] == memory_pkg::WRITE) begin
        idx <= idx + 1;
        client_write_data[C] <= pattern[(idx + 1) % L];
      end else if (client_transaction[C] == memory_pkg::READ) begin
        got[idx] <= read_data;
        idx <= idx + 1;
      end
    end
  end

  // Longest CS# low, counted in GLOBAL CLOCKS rather than $realtime. The repo
  // has no timescale, so simulation time units are whatever the testbench clock
  // period makes them -- a figure in ns is meaningless without it, while clocks
  // convert directly at 10 ns each.
  int unsigned cs_clocks = 0, cs_clocks_max = 0;
  always @(posedge clk) begin
    if (!cs_n) begin
      cs_clocks <= cs_clocks + 1;
      if (cs_clocks + 1 > cs_clocks_max) cs_clocks_max <= cs_clocks + 1;
    end else cs_clocks <= 0;
  end

  task automatic run(input memory_pkg::transaction_e t);
    @(posedge clk);
    wait (!busy);
    @(posedge clk);
    idx = 0;
    client_transaction[C] = t;
    client_address[C] = memory_pkg::address_t'(Base);
    client_length[C] = memory_pkg::message_length_t'(L);
    client_write_data[C] = pattern[0];
    client_toggle[C] = ~client_toggle[C];  // toggle, not a pulse -- no abort
    @(posedge clk);
    wait (busy);
    wait (!busy);
    @(posedge clk);
  endtask

  initial begin
    if (DUMPFILEPATH != "") begin
      $dumpfile(DUMPFILEPATH);
      $dumpvars(0, tb_burst64);
    end

    for (int unsigned i = 0; i < L; i++) pattern[i] = 8'(8'hA0 + i);
    repeat (4) @(posedge clk);
    reset = 0;
    enable = 1;

    // The wrapper holds busy high and ignores the client until both dice are
    // configured, so there is nothing to do until the initializer is done.
    wait (dut.initializer_finished);

    run(memory_pkg::WRITE);
    $display("tb_burst64: message_length_t is %0d bits, holds %0d (requested %0d)",
             $bits(memory_pkg::message_length_t), client_length[C], L);
    if (32'(client_length[C]) != 32'(L))
      $fatal(1, "client_length[C] field cannot represent %0d -- reads back %0d. MessageLengthRegisterWidth needs $clog2(MaxMessageLength + 1)",
             L, client_length[C]);
    if (idx != L) $fatal(1, "write moved %0d bytes, expected %0d", idx, L);

    run(memory_pkg::READ);
    if (idx != L) $fatal(1, "read moved %0d bytes, expected %0d", idx, L);

    for (int unsigned i = 0; i < L; i++)
      if (got[i] !== pattern[i])
        $fatal(1, "byte %0d: got 0x%02h want 0x%02h", i, got[i], pattern[i]);

    $display("tb_burst64: CS# low max %0d clocks = %0d ns at 100 MHz (tCSM budget %0.0f ns)",
             cs_clocks_max, cs_clocks_max * 10, TcsmNs);
    if (real'(cs_clocks_max * 10) > TcsmNs)
      $fatal(1, "CS# low %0d ns exceeds tCSM %0.0f ns", cs_clocks_max * 10, TcsmNs);

    $display("tb_burst64: %0d-byte round-trip OK, inside tCSM at both grades", L);
    $finish;
  end

  initial begin
    #200000;
    $fatal(1, "tb_burst64: timeout (initializer_finished=%0b busy=%0b idx=%0d)",
           dut.initializer_finished, busy, idx);
  end
endmodule
