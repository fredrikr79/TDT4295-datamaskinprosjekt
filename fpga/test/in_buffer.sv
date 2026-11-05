// Simulation model of the `in_buffer` Vivado FIFO Generator IP.
//
// The real thing is src/spi/ip/in_buffer/in_buffer.xci
// (xilinx.com:ip:fifo_generator:13.2). Verilator cannot read .xci, so without
// this file `spi_rx.sv` elaborates to a missing module and every Verilator
// target that reaches the SPI subtree fails with MODMISSING -- including
// tb_main, which is why `make sim-vga` stopped building.
//
// Lives in test/ on purpose: scripts/build.tcl globs src/ only, so this can
// never reach synthesis and Vivado keeps using the real IP. Same arrangement as
// test/psram_model.sv.
//
// Configuration below is transcribed from the .xci, not guessed:
//
//   Input/Output_Data_Width      8
//   Input/Output_Depth           1024
//   Performance_Options          First_Word_Fall_Through   <-- note
//   Almost_Full_Flag             true, Full_Threshold_Assert_Value  = 1023
//   Almost_Empty_Flag            true, Empty_Threshold_Assert_Value = 4
//   Write_Acknowledge_Flag       true (active high)
//   Reset_Type                   Asynchronous_Reset
//   Full_Flags_Reset_Value       1
//   Use_Dout_Reset               false
//   Enable_Safety_Circuit        true  (hence wr_rst_busy / rd_rst_busy)
//
// FIRST-WORD-FALL-THROUGH is the detail that matters. `dout` already shows the
// head of the queue whenever `empty` is low -- `rd_en` *pops* the word it has
// been showing, it does not request one. The sibling out_buffer is a Standard
// FIFO and behaves the opposite way. Swapping the two shifts every SPI byte by
// one, which reads as a protocol bug rather than a FIFO bug.
//
// SINGLE CLOCK ASSUMPTION: the IP is configured for independent clocks, but
// spi_rx.sv ties wr_clk and rd_clk to the same `clk`. This model runs entirely
// on wr_clk. If anyone ever drives the two ports from different clocks, this
// model silently stops representing the IP -- the assertion below catches that.
module in_buffer #(
    parameter int unsigned Depth = 1024,
    parameter int unsigned Width = 8,
    // Entry counts at which the almost_* flags assert, from the .xci.
    parameter int unsigned AlmostFullAssert  = 1023,
    parameter int unsigned AlmostEmptyAssert = 4
) (
    input  wire  [Width-1:0] din,
    output logic [Width-1:0] dout,

    output logic almost_full,
    output logic full,
    output logic almost_empty,
    output logic empty,

    input wire wr_en,
    input wire rd_en,

    output logic wr_ack,

    input wire rst,
    input wire wr_clk,
    input wire rd_clk,

    output logic wr_rst_busy,
    output logic rd_rst_busy
);

  logic [Width-1:0] mem[Depth];
  int unsigned count;
  int unsigned rd_ptr, wr_ptr;

  // Full_Flags_Reset_Value = 1: the IP asserts full while in reset, so a client
  // gating on ~full cannot start writing before the FIFO is ready.
  assign full = (count >= Depth) || wr_rst_busy;
  assign empty = (count == 0) || rd_rst_busy;
  assign almost_full = (count >= AlmostFullAssert) || wr_rst_busy;
  assign almost_empty = (count <= AlmostEmptyAssert) || rd_rst_busy;

  // First-word-fall-through: the head is continuously presented.
  // Use_Dout_Reset = false, so reset leaves dout alone.
  always_comb dout = (count == 0) ? '0 : mem[rd_ptr];

  // Safety circuit: busy through reset and one recovery cycle.
  logic rst_d;
  always_ff @(posedge wr_clk or posedge rst) begin
    if (rst) rst_d <= 1'b1;
    else rst_d <= 1'b0;
  end
  assign wr_rst_busy = rst | rst_d;
  assign rd_rst_busy = rst | rst_d;

  wire do_write = wr_en && !full && !wr_rst_busy;
  wire do_read = rd_en && !empty && !rd_rst_busy;

  always_ff @(posedge wr_clk or posedge rst) begin
    if (rst) begin
      count  <= 0;
      rd_ptr <= 0;
      wr_ptr <= 0;
      wr_ack <= 1'b0;
    end else begin
      // wr_ack is a one-cycle pulse acknowledging an accepted write.
      wr_ack <= do_write;

      if (do_write) begin
        mem[wr_ptr] <= din;
        wr_ptr <= (wr_ptr == Depth - 1) ? 0 : wr_ptr + 1;
      end
      if (do_read) begin
        rd_ptr <= (rd_ptr == Depth - 1) ? 0 : rd_ptr + 1;
      end

      case ({do_write, do_read})
        2'b10:   count <= count + 1;
        2'b01:   count <= count - 1;
        default: count <= count;  // both or neither: unchanged
      endcase
    end
  end

  // The single-clock assumption above, made loud.
  always @(posedge rd_clk) begin
    if (!rst && (rd_clk !== wr_clk))
      $fatal(1, "in_buffer: wr_clk and rd_clk differ; this model is single-clock");
  end
endmodule
