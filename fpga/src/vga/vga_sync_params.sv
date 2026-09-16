package vga_sync_params;
  // 640X480 VGA sync parameters
// IF CHANGING THESE, REMEMBER TO CHANGE THE PARAMETERS IN simulator.cpp AS WELL
  /* verilator lint_off UNUSEDPARAM */
/* verilator lint_on UNUSEDPARAM */
  localparam int unsigned ActiveWidth = 640;
  localparam int unsigned ActiveHeight = 360;
  localparam int unsigned HorizontalFrontPorch = 32; // Right Porch
  localparam int unsigned HorizontalBackPorch = 48; // Left Porch
  
  localparam int unsigned VerticalFrontPorch = 63; // Bottom Porch
  localparam int unsigned VerticalBackPorch = 71; // Top Porch
  
  localparam int unsigned HorizontalSync = 64;
  localparam int unsigned VerticalSync = 3;
  
  localparam int unsigned TotalWidth = ActiveWidth + HorizontalFrontPorch + HorizontalSync + HorizontalBackPorch;
  localparam int unsigned TotalHeight = ActiveHeight + VerticalFrontPorch + VerticalSync + VerticalBackPorch;

  /* verilator lint_off UNUSEDPARAM */
  /* verilator lint_on UNUSEDPARAM */

  localparam int unsigned PixelClockIncrement = 4;
endpackage
