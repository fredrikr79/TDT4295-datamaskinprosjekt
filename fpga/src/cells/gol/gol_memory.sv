// https://github.com/hrvach/Life_MiSTer/blob/master/memory.v

module gol_row_delay #(
    parameter int unsigned Depth = gol_pkg::RowDelay
) (
    input  wire clk,
    input  wire en,
    input  wire data_in,
    output wire data_out
);

	// Declares to create a SRLC32E shift-register out of sr (srl_style=srl)
  (* srl_style = "srl" *) logic [Depth-1:0] sr = '0;

  always_ff @(posedge clk) begin
    if (en) begin
      sr <= {sr[Depth-2:0], data_in}; // Shift in the new data
    end
  end

  assign data_out = sr[Depth-1]; // Output the last bit of the shift register
endmodule

module gol_ring_buffer #(
    parameter int unsigned Depth = gol::pkg::RingDepth
) (
    input  wire  clk,
    input  wire  en,
    input  wire  data_in,
    output logic data_out
);
  localparam int unsigned AddrBits = $clog2(Depth);

  // Explicitly tell vivado to use BRAM
  (* ram_style = "block" *) logic mem[Depth];

  logic [AddrBits-1:0] addr = '0;
  logic target_gol_cell = 1'b0;

  always_ff @(posedge clk) begin
    if (en) begin 
      addr <= (addr == AddrBits'(Depth - 1)) ? '0 : addr + 1'b1; // Looparound if at end of memory
    end
  end

  always_ff @(posedge clk) begin
    if (en) begin
      target_gol_cell <= mem[addr];  // Read first
      mem[addr] <= data_in;  // Then overwrite with new data
    end
  end

  always_ff @(posedge clk) begin
    if (en) data_out <= target_gol_cell;
  end
endmodule
