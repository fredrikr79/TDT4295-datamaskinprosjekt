package vga_sync_params;
  // 640x480@60 (VESA) raster, with our 16:9 640x360 image letterboxed
  // vertically inside it.
  //
  // The lines we don't draw are split evenly between the vertical porches
  // (Letterbox), which centres the image and leaves equal bars top and bottom.
  //
  // IF CHANGING THESE, REMEMBER TO CHANGE THE PARAMETERS IN simulator.cpp AS WELL
  /* verilator lint_off UNUSEDPARAM */
  localparam int unsigned ActiveWidth = 640;
  localparam int unsigned ActiveHeight = 360;

  localparam int unsigned Letterbox = (480 - ActiveHeight) / 2;

  localparam int unsigned HorizontalFrontPorch = 16;  // Right Porch
  localparam int unsigned HorizontalBackPorch = 48;  // Left Porch

  localparam int unsigned VerticalFrontPorch = 10 + Letterbox;  // Bottom Porch
  localparam int unsigned VerticalBackPorch = 33 + Letterbox;  // Top Porch

  localparam int unsigned HorizontalSync = 96;
  localparam int unsigned VerticalSync = 2;

  localparam int unsigned TotalWidth = ActiveWidth + HorizontalFrontPorch + HorizontalSync + HorizontalBackPorch;
  localparam int unsigned TotalHeight = ActiveHeight + VerticalFrontPorch + VerticalSync + VerticalBackPorch;

  /* verilator lint_on UNUSEDPARAM */
endpackage
