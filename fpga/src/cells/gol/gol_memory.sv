// https://github.com/hrvach/Life_MiSTer/blob/master/memory.v

module gol_memory #(
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
