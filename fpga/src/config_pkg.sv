package config_pkg;
localparam int unsigned GlobalClockFrequency = 100_000_000;
localparam int unsigned LeftPorch = 48;
localparam int unsigned ActiveWidth = 640;
localparam int unsigned RightPorch = 16;
localparam int unsigned HorizontalSync = 96;
localparam int unsigned TopPorch = 93;
localparam int unsigned ActiveHeight = 360;
localparam int unsigned BottomPorch = 70;
localparam int unsigned VerticalSync = 2;
localparam int unsigned FPS = 60;
endpackage
