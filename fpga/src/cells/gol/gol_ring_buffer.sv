module gol_ring_buffer #(
    parameter int unsigned Depth = gol_pkg::RingDepth
) (
    input  wire  clk,
    input  wire  en,
    input  wire  we,
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
      if (we) mem[addr] <= data_in;  // Then overwrite with new data
    end
  end

  always_ff @(posedge clk) begin
    if (en) data_out <= target_gol_cell;
  end
endmodule
