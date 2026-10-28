// Simulation model of the `out_buffer` Vivado FIFO Generator IP.
//
// The real thing is src/spi/ip/out_buffer/out_buffer.xci
// (xilinx.com:ip:fifo_generator:13.2). See test/in_buffer.sv for why these
// models exist and why they live in test/ rather than src/.
//
// Configuration transcribed from the .xci:
//
//   Input/Output_Data_Width      8
//   Input/Output_Depth           1024
//   Performance_Options          Standard_FIFO             <-- note
//   Almost_Full_Flag             true, Full_Threshold_Assert_Value  = 1021
//   Almost_Empty_Flag            true, Empty_Threshold_Assert_Value = 2
//   Write_Acknowledge_Flag       false  (no wr_ack port)
//   Reset_Type                   Asynchronous_Reset
//   Full_Flags_Reset_Value       1
//   Use_Dout_Reset               true, Dout_Reset_Value = 0
//   Enable_Safety_Circuit        true  (hence wr_rst_busy / rd_rst_busy)
//
// STANDARD MODE, unlike its sibling: `dout` is registered and updates the cycle
// *after* `rd_en` is accepted. It does not show the head of the queue until
// asked. in_buffer is First-Word-Fall-Through and does the opposite. The two
// are not interchangeable and must not be collapsed into one parameterised
// module -- spi_tx.sv's preload/arming logic depends on this timing.
//
// SINGLE CLOCK ASSUMPTION: configured for independent clocks, but spi_tx.sv
// ties wr_clk and rd_clk to the same `clk`, so this model runs on wr_clk. The
// assertion at the bottom catches anyone who changes that.
module out_buffer #(
    parameter int unsigned Depth = 1024,
    parameter int unsigned Width = 8,
    // Entry counts at which the almost_* flags assert, from the .xci.
    parameter int unsigned AlmostFullAssert  = 1021,
    parameter int unsigned AlmostEmptyAssert = 2
) (
    input  wire  [Width-1:0] din,
    output logic [Width-1:0] dout,

    output logic almost_full,
    output logic full,
    output logic almost_empty,
    output logic empty,

    input wire wr_en,
    input wire rd_en,

    input wire rst,
    input wire wr_clk,
    input wire rd_clk,

    output logic wr_rst_busy,
    output logic rd_rst_busy
);

  logic [Width-1:0] mem[Depth];
  int unsigned count;
  int unsigned rd_ptr, wr_ptr;

  // Full_Flags_Reset_Value = 1: full asserts while in reset.
  assign full = (count >= Depth) || wr_rst_busy;
  assign empty = (count == 0) || rd_rst_busy;
  assign almost_full = (count >= AlmostFullAssert) || wr_rst_busy;
  assign almost_empty = (count <= AlmostEmptyAssert) || rd_rst_busy;

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
      dout   <= '0;  // Use_Dout_Reset = true, Dout_Reset_Value = 0
    end else begin
      if (do_write) begin
        mem[wr_ptr] <= din;
        wr_ptr <= (wr_ptr == Depth - 1) ? 0 : wr_ptr + 1;
      end

      // Standard mode: the read lands on dout one cycle after rd_en.
      if (do_read) begin
        dout   <= mem[rd_ptr];
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
      $fatal(1, "out_buffer: wr_clk and rd_clk differ; this model is single-clock");
  end
endmodule
