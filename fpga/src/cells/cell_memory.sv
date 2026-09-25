module cell_memory (
    input wire                            clk,
    input wire                            we,
    input wire [cell_pkg::AddrBits-1:0]   write_addr,
    input cell_pkg::cell_word_t           write_data,
    input  wire  [cell_pkg::AddrBits-1:0] read_addr,
    output cell_pkg::cell_word_t          read_data
);
  import cell_pkg::*;

  cell_word_t grid[TotalWords];

  always_ff @(posedge clk) begin
    if (we) grid[write_addr] <= write_data;
    read_data <= grid[read_addr];
  end
endmodule
