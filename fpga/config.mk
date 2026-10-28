GlobalClockFrequency ?= 100_000_000
LeftPorch ?= 48
ActiveWidth ?= 640
RightPorch ?= 16
HorizontalSync ?= 96
TopPorch ?= 93
ActiveHeight ?= 360
BottomPorch ?= 70
VerticalSync ?= 2
FPS ?= 60

.ONESHELL:

src/config_pkg.sv: src
	@cat > $@ <<EOF
	package config_pkg;
	    parameter int unsigned GlobalClockFrequency = $(GlobalClockFrequency);
	    parameter int unsigned LeftPorch = $(LeftPorch);
	    parameter int unsigned ActiveWidth = $(ActiveWidth);
	    parameter int unsigned RightPorch = $(RightPorch);
	    parameter int unsigned HorizontalSync = $(HorizontalSync);
	    parameter int unsigned TopPorch = $(TopPorch);
	    parameter int unsigned ActiveHeight = $(ActiveHeight);
	    parameter int unsigned BottomPorch = $(BottomPorch);
	    parameter int unsigned VerticalSync = $(VerticalSync);
	    parameter int unsigned FPS = $(FPS);
	endpackage
	EOF
test/config.hpp: test/vga-simulation
	@cat > $@ <<EOF
	constexpr unsigned int LeftPorch = $(LeftPorch);
	constexpr unsigned int ActiveWidth = $(ActiveWidth);
	constexpr unsigned int RightPorch = $(RightPorch);
	constexpr unsigned int HorizontalSync = $(HorizontalSync);
	constexpr unsigned int TopPorch = $(TopPorch);
	constexpr unsigned int ActiveHeight = $(ActiveHeight);
	constexpr unsigned int BottomPorch = $(BottomPorch);
	constexpr unsigned int VerticalSync = $(VerticalSync);
	constexpr unsigned int FPS = $(FPS);
	EOF
