package gol_pkg;
  localparam int unsigned RowLen   = config_pkg::ActiveWidth;   // 640
  localparam int unsigned NumRows  = config_pkg::ActiveHeight;  // 360
  localparam int unsigned FrameLen = RowLen * NumRows;          // 230,400
  
  localparam int unsigned RowDelay = RowLen - 2;  // 638
  localparam int unsigned RingDelay = FrameLen - RowLen - 2;  // 229,758

  // gol_ring_buffer spends 2 cycles in its own output registers.
  localparam int unsigned RingDepth = RingDelay - 2;  // 229,756
  localparam int unsigned SeedCountBits = $clog2(FrameLen);  // For initial seeding of gol-layer
endpackage
