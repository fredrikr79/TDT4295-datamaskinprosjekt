// Behavioural stand-in for the Infineon S70KL1283 PSRAM.
//
// This is a real memory: writes land in `mem` and reads come back from it, so a
// testbench (or the whole design under sim-vga) can store and retrieve data with
// no hardware present. tb_memory's old hand-driven `dq` only checked that the
// controller walked the protocol; nothing proved a byte survived a round-trip.
//
// Lives in test/ on purpose -- scripts/build.tcl globs src/ only, so this can
// never reach synthesis and needs no `ifdef guards.
//
// Timing is matched to what memory_controller actually emits, captured from a
// bus trace rather than assumed:
//
//   ck edge:   0     1     2       3       4      5      | latency | data...
//   dq:      opcode opcode A31:24 A23:16 A15:8  A7:0     |         | D0 D1 ..
//
// The opcode occupying two edge slots is a quirk of memory_controller's
// SENDING_CMD state lasting two clocks. CmdSlots is a parameter so it can be
// retuned if the controller changes or the real part disagrees -- worth
// confirming against the datasheet CA timing diagram before hardware bring-up.
module psram_model #(
    // Enough for the whole memory map (~592 KiB today, ~1.4 MiB with
    // framebuffers) without allocating the full 16 MiB every run.
    parameter int unsigned SizeBytes = 2 * 1024 * 1024,
    parameter int unsigned CmdSlots = 2,
    parameter int unsigned AddrSlots = 4,
    // ck edges of initial latency. Dual-die parts always use the 2x path.
    parameter int unsigned LatencySlots = 14,
    // The controller samples dq on the same edge the memory would change it, so
    // read data has to already be on the bus when READING begins -- the model
    // starts driving this many slots early. Writes need no such lead: the host
    // drives dq itself and the model captures on the edge.
    parameter int unsigned ReadLead = 1,
    // "Linear Burst across die boundary is not supported" -- 8 MiB per die.
    parameter int unsigned DieBytes = 8 * 1024 * 1024,
    // CS# LOW maximum time: 4 us at 85 C, after which a refresh is missed.
    parameter realtime TcsmNs = 4000.0,
    // Print every register byte memory_initializer writes. Four CR writes at
    // startup, so it is cheap -- and it is the only way to see what actually
    // landed in CR0/CR1 versus what the init table intended.
    parameter bit TraceRegisters = 1'b1
) (
    input wire cs_n,
    input wire ck,
    input wire reset_n,
    inout wire [7:0] dq,
    inout wire rwds
);

  localparam logic [7:0] CmdRead = 8'hEE;
  localparam logic [7:0] CmdWrite = 8'hDE;

  // Register-space opcodes. memory_initializer issues these at startup, so a
  // model that only knows READ/WRITE fatals on the very first transaction of
  // any run that includes `memory` -- which is every run of tb_main.
  localparam logic [7:0] CmdWriteReg = 8'h71;  // WRITE_ANY_REGISTER
  localparam logic [7:0] CmdReadReg = 8'h65;  // READ_ANY_REGISTER
  localparam logic [7:0] CmdReadId = 8'h9F;  // READ_ID

  function automatic bit is_array_access(input logic [7:0] op);
    return op == CmdRead || op == CmdWrite;
  endfunction

  function automatic bit is_register_access(input logic [7:0] op);
    return op == CmdWriteReg || op == CmdReadReg || op == CmdReadId;
  endfunction

  // Opcodes with neither an address nor a data phase: CS# goes high again after
  // the command, so these never reach the address-complete slot below.
  function automatic bit is_command_only(input logic [7:0] op);
    case (op)
      8'h01,  // DEEP_POWER_UP
      8'h06,  // WRITE_ENABLE
      8'h04,  // WRITE_DISABLE
      8'h66,  // RESET_ENABLE
      8'h99,  // RESET
      8'hB9:  // DEEP_POWER_DOWN
      return 1'b1;
      default: return 1'b0;
    endcase
  endfunction

  localparam int unsigned AddrEnd = CmdSlots + AddrSlots;  // first latency slot
  localparam int unsigned WriteStart = AddrEnd + LatencySlots;
  localparam int unsigned ReadStart = WriteStart - ReadLead;

  // NB: Verilator is 2-state, so unwritten entries read back as 0 here, not x.
  // A read-before-write will NOT announce itself under `make generate-vcd` --
  // only under a 4-state simulator such as Vivado. Do not rely on x-propagation
  // to catch uninitialised reads; the address-range $fatal below is the check
  // that actually works in this toolchain.
  logic [7:0] mem[SizeBytes];

  // Register space. "Each die has its own register space" -- ID0 reads 0x0C81
  // from Die 0 and 0x4C81 from Die 1, with separate CR0 addresses, so an
  // initialisation sequence that configures only one die leaves the other at
  // defaults. Indexed [die][addr[3:0]]: ID0 at 0x0, ID1 at 0x2, CR0 at 0x4,
  // CR1 at 0x6. The die selector is address bit 23 (0x00800004 vs 0x00000004).
  localparam int unsigned RegDieBit = 23;
  logic [7:0] regs[2][16];

  int unsigned slot;
  logic [7:0] opcode;
  logic [31:0] addr;
  int unsigned data_idx;
  realtime cs_fell_at;

  logic [7:0] dq_drv;
  logic dq_oe;
  logic rwds_drv;
  logic rwds_oe;

  assign dq   = dq_oe ? dq_drv : 8'bzzzzzzzz;
  assign rwds = rwds_oe ? rwds_drv : 1'bz;

  initial begin
    slot = 0;
    dq_oe = 1'b0;
    rwds_oe = 1'b0;
    opcode = 8'h00;
    addr = 32'h0;
    data_idx = 0;
    for (int unsigned d = 0; d < 2; d++) for (int unsigned i = 0; i < 16; i++) regs[d][i] = 8'h00;
    // Device identification, per the datasheet.
    regs[0][0] = 8'h0C;
    regs[0][1] = 8'h81;  // Die 0 ID0 = 0x0C81
    regs[1][0] = 8'h4C;
    regs[1][1] = 8'h81;  // Die 1 ID0 = 0x4C81
  end

  function automatic int unsigned current_addr();
    return addr + data_idx;
  endfunction

  function automatic int unsigned reg_die();
    return int'(addr[RegDieBit]);
  endfunction

  // Register accesses are byte-sequential from the supplied address, same as
  // array bursts. Clamped so a stray long burst cannot run off the table.
  function automatic int unsigned reg_index();
    int unsigned i;
    i = int'(addr[3:0]) + data_idx;
    return (i > 15) ? 15 : i;
  endfunction

  // Start of transaction. cs_n falls before the first ck edge (confirmed from
  // the bus trace), so there is no race with the sampling process below.
  always @(negedge cs_n) begin
    slot = 0;
    data_idx = 0;
    addr = 32'h0;
    dq_oe = 1'b0;
    // During the command/address phase the memory drives RWDS to tell the host
    // whether extra latency is needed. This part always needs the 2x path.
    rwds_oe = 1'b1;
    rwds_drv = 1'b1;
    cs_fell_at = $realtime;
  end

  always @(posedge cs_n) begin
    dq_oe   = 1'b0;
    rwds_oe = 1'b0;
    if (($realtime - cs_fell_at) > TcsmNs)
      $fatal(1, "psram_model: CS# low %0t ns exceeds tCSM %0t ns -- refresh would be missed",
             $realtime - cs_fell_at, TcsmNs);
  end

  always @(posedge ck, negedge ck) begin
    if (!reset_n) begin
      slot  <= 0;
      dq_oe <= 1'b0;
    end else if (!cs_n) begin
      if (slot < CmdSlots) begin
        if (slot == 0) opcode = dq;
      end else if (slot < AddrEnd) begin
        addr = {addr[23:0], dq};
      end else if (slot == AddrEnd) begin
        // Address complete: validate it before any data moves.
        if (!is_array_access(opcode) && !is_register_access(opcode) && !is_command_only(opcode))
          $fatal(1, "psram_model: unsupported opcode 0x%02h", opcode);
        // Only array accesses are bounded by the backing store; register
        // addresses live in a separate space and are legitimately far above
        // SizeBytes (die 1's CR0 is at 0x00800004).
        if (is_array_access(opcode) && addr >= SizeBytes)
          $fatal(1, "psram_model: address 0x%08h past SizeBytes (0x%08h) -- addressing bug",
                 addr, SizeBytes);
      end

      // Register writes have no latency phase -- the controller goes straight
      // from SENDING_ADDRESS to WRITING (mem_txn_write_any_register.sv).
      // Register reads do wait out the latency, like array reads.
      if (opcode == CmdWriteReg && slot >= AddrEnd) begin
        regs[reg_die()][reg_index()] = dq;
        rwds_oe = 1'b0;  // host drives RWDS as the data mask during writes
        if (TraceRegisters)
          $display("psram_model: die %0d reg[0x%0h] <= 0x%02h", reg_die(), reg_index(), dq);
        data_idx = data_idx + 1;
      end else if ((opcode == CmdReadReg || opcode == CmdReadId) && slot >= ReadStart) begin
        dq_oe = 1'b1;
        dq_drv = regs[reg_die()][reg_index()];
        rwds_oe = 1'b1;
        rwds_drv = ~rwds_drv;
        data_idx = data_idx + 1;
      end else if ((opcode == CmdWrite && slot >= WriteStart) ||
          (opcode == CmdRead && slot >= ReadStart)) begin
        if (((addr + data_idx) / DieBytes) != (addr / DieBytes))
          $fatal(1, "psram_model: burst from 0x%08h crossed the die boundary at 0x%08h",
                 addr, DieBytes);
        if ((addr + data_idx) >= SizeBytes)
          $fatal(1, "psram_model: burst from 0x%08h ran past SizeBytes", addr);

        if (opcode == CmdWrite) begin
          mem[addr+data_idx] = dq;
          rwds_oe = 1'b0;  // host drives RWDS as the data mask during writes
        end else begin
          dq_oe = 1'b1;
          dq_drv = mem[addr+data_idx];
          rwds_oe = 1'b1;
          rwds_drv = ~rwds_drv;  // read data strobe
        end
        data_idx = data_idx + 1;
      end

      slot = slot + 1;
    end
  end

  // Debug helper: dump a region as a PGM so a packed 1-bit grid can be looked
  // at directly rather than inferred from the screen.
  task automatic dump_bitmap(input string path, input int unsigned base,
                             input int unsigned width, input int unsigned height);
    int fd, byte_addr, bit_pos;
    fd = $fopen(path, "w");
    $fwrite(fd, "P2\n%0d %0d\n1\n", width, height);
    for (int unsigned y = 0; y < height; y++) begin
      for (int unsigned x = 0; x < width; x++) begin
        byte_addr = base + (y * width + x) / 8;
        bit_pos   = 7 - ((y * width + x) % 8);
        $fwrite(fd, "%0d ", (mem[byte_addr] >> bit_pos) & 1'b1);
      end
      $fwrite(fd, "\n");
    end
    $fclose(fd);
  endtask
endmodule
