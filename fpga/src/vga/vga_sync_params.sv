package vga_sync_params;
  import config_pkg::*;
  localparam int unsigned TotalWidth = LeftPorch + ActiveWidth + RightPorch + HorizontalSync;
  localparam int unsigned TotalHeight = TopPorch + ActiveHeight + BottomPorch + VerticalSync;

  localparam int unsigned XCoordinateBits = (TotalWidth > 1) ? $clog2(TotalWidth) : 1;
  localparam int unsigned YCoordinateBits = (TotalHeight > 1) ? $clog2(TotalHeight) : 1;

  // Type used to represent the X and Y coordinates of a pixel on the screen. Used by every module working with pixel coordinates.
  typedef logic unsigned [XCoordinateBits-1:0] x_coordinate_t;
  typedef logic unsigned [YCoordinateBits-1:0] y_coordinate_t;

  // The pixel clock rate is the number of pixels that must be displayed per second. The Delay is the number of global clock cycles per pixel. The delay is rounded to the nearest integer.
  localparam int unsigned PixelClockRate  = TotalWidth * TotalHeight * FPS;

  // Basically GlobalClockFrequency/PixelClockRate, only that this round to nearest rather than always flooring.
  localparam int unsigned PixelClockDelay =
            (GlobalClockFrequency + PixelClockRate / 2) / PixelClockRate;
endpackage
