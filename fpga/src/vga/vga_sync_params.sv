package vga_sync_params;
  import config_pkg::*;
  localparam int unsigned TotalWidth = LeftPorch + ActiveWidth + RightPorch + HorizontalSync;

  localparam int unsigned TotalHeight = TopPorch + ActiveHeight + BottomPorch + VerticalSync;

  localparam int unsigned XCoordinateBits = (TotalWidth > 1) ? $clog2(TotalWidth) : 1;
  localparam int unsigned YCoordinateBits = (TotalHeight > 1) ? $clog2(TotalHeight) : 1;

  typedef logic unsigned [XCoordinateBits-1:0] x_coordinate_t;
  typedef logic unsigned [YCoordinateBits-1:0] y_coordinate_t;

  localparam int unsigned PixelClockRate  = TotalWidth * TotalHeight * FPS;
  localparam int unsigned PixelClockDelay = GlobalClockFrequency / PixelClockRate;
endpackage
