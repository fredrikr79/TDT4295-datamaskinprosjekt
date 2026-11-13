package palette_pkg;
  localparam int unsigned PaletteEntries = 256;
  localparam int unsigned PaletteIndexBits = $clog2(PaletteEntries);  // 8
  typedef logic unsigned [PaletteIndexBits-1:0] palette_index_t;
endpackage
