TOP_MODULE ?= tb_main

SOURCE_DIRECTORIES ?= src test

SIM_DIR := build
VGA_SIM_DIR := vga-build
SIM_BINARY := $(SIM_DIR)/V$(TOP_MODULE)
VGA_SIM_BINARY := $(VGA_SIM_DIR)/V$(TOP_MODULE)

DUMP_DIR := dump-data
VCD_DUMP := $(TOP_MODULE).vcd
DUMP_MOVIE_NAME := movie.mp4
DUMP_MOVIE_FPS := 10
DUMP_MOVIE_MAX_FRAME := 100
DUMP_FRAMES := $(foreach n,$(shell seq -f '%04g' 0 $(DUMP_MOVIE_MAX_FRAME)),$(DUMP_DIR)/frame_$(n).ppm)

DUMP_FLAGS :=
ifneq ($(filter $(TOP_MODULE),tb_main tb_sim),)
DUMP_FLAGS += -GDUMPFILENAME=\"$(VCD_DUMP)\"
DUMP_FLAGS += -GDUMPDIR=\"$(DUMP_DIR)\"
endif
ifneq ($(filter $(TOP_MODULE),tb_sim),)
DUMP_FLAGS += -GNUM_GENERATIONS=$(DUMP_MOVIE_MAX_FRAME)
endif

ALL_SOURCES := $(shell find $(SOURCE_DIRECTORIES) \-type f \( -name "*.v" -o -name "*.sv" \) ! -path "src/config_pkg.sv") src/config_pkg.sv
PKG_SOURCES := $(filter %_pkg.sv,$(ALL_SOURCES))
OTHER_SOURCES := $(filter-out %_pkg.sv,$(ALL_SOURCES))
SOURCES := $(PKG_SOURCES) $(OTHER_SOURCES)

.PHONY: dump vga lint clean vivado-all vivado-build vivado-program

# Simulation
lint: $(SOURCES) lint/spi_fifo_stubs.sv
	verilator \
		--lint-only --Wall \
		$(SOURCES) lint/spi_fifo_stubs.sv
$(DUMP_DIR)/$(DUMP_MOVIE_NAME) dump-movie &: $(DUMP_FRAMES)
	ffmpeg \
		-framerate $(DUMP_MOVIE_FPS) \
		-i $(DUMP_DIR)/frame_%04d.ppm \
		-c:v libopenh264 \
		-pix_fmt yuv420p \
		$(DUMP_DIR)/movie.mp4
$(DUMP_FRAMES) $(DUMP_DIR)/$(VCD_DUMP) dump &: $(SIM_BINARY) | $(DUMP_DIR)
	./$(SIM_BINARY)
vga: $(VGA_SIM_BINARY)
	./$(VGA_SIM_BINARY)
$(SIM_BINARY): $(SOURCES) | $(SIM_DIR)
	verilator \
		$(SOURCES) \
		--binary --timing --trace --Wall --Wno-fatal \
		--top-module $(TOP_MODULE) \
		--Mdir $(SIM_DIR) \
		$(DUMP_FLAGS)
$(VGA_SIM_BINARY): $(SOURCES) test/vga-simulation/simulator.cpp test/config.hpp | $(VGA_SIM_DIR)
	verilator \
		$(SOURCES) test/vga-simulation/simulator.cpp \
		--cc --exe --build --timing --trace --Wall --Wno-fatal \
		--top-module $(TOP_MODULE) \
		--Mdir $< \
		-LDFLAGS "-lglut -lGLU -lGL"
$(VGA_SIM_DIR):
	@mkdir -p $@
$(SIM_DIR):
	@mkdir -p $@
$(DUMP_DIR):
	@mkdir -p $@

# Vivado synthesizing
vivado-all: vivado-build vivado-program
vivado-program: scripts/vivado.sh scripts/program.tcl build/main.bit .vivado.env
	sh scripts/vivado.sh -mode batch -source $(CURDIR)/scripts/program.tcl
vivado-build: scripts/vivado.sh scripts/build.tcl $(SOURCES) .vivado.env
	sh scripts/vivado.sh -mode batch -source $(CURDIR)/scripts/build.tcl

# Generate verible file for lsp
verible.filelist: $(SOURCES)
	@printf '%s\n' $(SOURCES) | sort > $@
# Set up configuration files
include config.mk

clean:
	rm -rf $(VGA_SIM_DIR) $(SIM_DIR) $(DUMP_DIR) build verible.filelist src/config_pkg.sv test/config.hpp
