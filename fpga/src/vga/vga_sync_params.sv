package vga_sync_params;
  // 640X480 VGA sync parameters
// IF CHANGING THESE, REMEMBER TO CHANGE THE PARAMETERS IN simulator.cpp AS WELL
  /* verilator lint_off UNUSEDPARAM */
  localparam int unsigned LeftPorch = 40;
  /* verilator lint_on UNUSEDPARAM */
  localparam int unsigned ActiveWidth = 854;
  localparam int unsigned RightPorch = 8;
  localparam int unsigned HorizontalSync = 32;
  localparam int unsigned TotalWidth = 934;

  localparam int unsigned TopPorch = 6;
  localparam int unsigned ActiveHeight = 480;
  /* verilator lint_off UNUSEDPARAM */
  localparam int unsigned BottomPorch = 15;
  /* verilator lint_on UNUSEDPARAM */
  localparam int unsigned VerticalSync = 8;
  localparam int unsigned TotalHeight = 509;

  localparam int unsigned PixelClockIncrement = 4;
endpackage
