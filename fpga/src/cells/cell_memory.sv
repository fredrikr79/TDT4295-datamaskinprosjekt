module cell_memory (
    input wire                            clk,
    input wire                            we,
    input wire [sim_pkg::AddrBits-1:0]   write_addr,
    input sim_pkg::cell_word_t           write_data,
    input  wire  [sim_pkg::AddrBits-1:0] read_addr,
    output sim_pkg::cell_word_t          read_data
);
  import sim_pkg::*;

  // cell_word_t grid[TotalWords] is a nightmare, synthesizer-wise, uses flipflops instead of BRAM
  logic [WordBits-1:0] grid[TotalWords];

  always_ff @(posedge clk) begin
    if (we) grid[write_addr] <= write_data;
    read_data <= grid[read_addr];
  end
endmodule
