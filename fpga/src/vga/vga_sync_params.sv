`timescale 1ns / 1ps
package vga_sync_params;
  // 640X480 VGA sync parameters
  /* verilator lint_off UNUSEDPARAM */
  localparam int unsigned LeftPorch = 48;
  /* verilator lint_on UNUSEDPARAM */
  localparam int unsigned ActiveWidth = 640;
  localparam int unsigned RightPorch = 16;
  localparam int unsigned HorizontalSync = 96;
  localparam int unsigned TotalWidth = 800;

  localparam int unsigned TopPorch = 33;
  localparam int unsigned ActiveHeight = 480;
  /* verilator lint_off UNUSEDPARAM */
  localparam int unsigned BottomPorch = 10;
  /* verilator lint_on UNUSEDPARAM */
  localparam int unsigned VerticalSync = 2;
  localparam int unsigned TotalHeight = 525;

  localparam int unsigned PixelClockIncrement = 4;
endpackage
