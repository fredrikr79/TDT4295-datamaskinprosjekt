module world_loader (
    input wire clk,
    input wire reset,
    output logic                          we,
    output logic [cell_pkg::AddrBits-1:0] write_addr,
    output cell_pkg::cell_word_t          data
);
  import cell_pkg::*;
  import spawn_screen_pkg::word_at; // Decides what screen we are loading.

  logic [WordXBits-1:0] word_x;
  grid_y_t              row_y;
  logic                 loading;
  grid_x_t              word_full_address;
  assign word_full_address = {word_x, {CellIndexBits{1'b0}}}; // Concatenate indexing bits.

  always_ff @(posedge clk) begin
    if (reset) begin
      word_x  <= '0;
      row_y   <= '0;
      we      <= 1'b0;
      loading <= 1'b1;
    end else begin
      we   <= loading;
      write_addr <= get_word_addr(word_full_address, row_y);
      data <= word_at(word_x, row_y); // We write the data that was retrieved last cycle

      // Now get the data that will be written next cycle
      if (loading) begin
        if (word_x == WordXBits'(WordsPerRow - 1)) begin
          word_x <= '0;
          row_y  <= row_y + 1;
          if (row_y == grid_y_t'(GridHeight - 1)) loading <= 1'b0;
        end else begin
          word_x <= word_x + 1;
        end
      end
    end
  end
endmodule
