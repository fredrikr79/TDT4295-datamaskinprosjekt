package spawn_screen_pkg;
  import cell_pkg::*;

  function automatic material_t material_at(input int unsigned x, input int unsigned y);
    if (y < 100) return MatAir; // Hardcoded air over 100
    else return MatSand;
  endfunction

  // Collect all 8 materials within the word
  function automatic cell_word_t word_at(input logic [WordXBits-1:0] word_x, input grid_y_t y);
    cell_word_t word;
    for (int unsigned i = 0; i < CellsPerWord; i++) begin
      word[i] = material_at(word_x * CellsPerWord + i, 32'(y));
    end
    return word;
  endfunction
endpackage
