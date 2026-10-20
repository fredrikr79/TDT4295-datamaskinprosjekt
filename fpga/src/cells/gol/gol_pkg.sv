package gol_pkg;
  localparam int unsigned RowLen   = int'(config_pkg::ActiveWidth);   // 640
  localparam int unsigned NumRows  = int'(config_pkg::ActiveHeight);  // 360
  localparam int unsigned BitsPerFrame = RowLen * NumRows;            // 230,400

  localparam int unsigned RowDelay = RowLen - 2;  // 638
  localparam int unsigned RingDelay = BitsPerFrame - RowLen - 2;  // 229,758

  // gol_ring_buffer spends 2 cycles in its own output registers.
  localparam int unsigned RingDepth = RingDelay - 2;  // 229,756

  localparam int unsigned GridXBits = $clog2(RowLen);  // 10
  localparam int unsigned GridYBits = $clog2(NumRows);  // 9
  typedef logic unsigned [GridXBits-1:0] grid_x_t;
  typedef logic unsigned [GridYBits-1:0] grid_y_t;

  localparam int unsigned CellIndexBits = $clog2(BitsPerFrame);  // 18
  typedef logic unsigned [CellIndexBits-1:0] cell_index_t;
  localparam int unsigned SeedCountBits = CellIndexBits;  // For initial seeding of gol-layer

  function automatic cell_index_t cell_index(input grid_x_t x, input grid_y_t y);
    return cell_index_t'(int'(y) * RowLen + int'(x));
  endfunction
endpackage
