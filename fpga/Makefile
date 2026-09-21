TOP_MODULE ?= tb_main

SOURCE_DIRECTORIES ?= src test

VCD_SIM_DIR := vcd-build
VGA_SIM_DIR := vga-build
VCD_SIM_BINARY := $(VCD_SIM_DIR)/V$(TOP_MODULE)
VGA_SIM_BINARY := $(VGA_SIM_DIR)/V$(TOP_MODULE)

VCD_DIR := vcd
TOP_VCD := $(VCD_DIR)/$(TOP_MODULE).vcd

SOURCES := $(shell find $(SOURCE_DIRECTORIES) \-type f \( -name "*.v" -o -name "*.sv" \) ! -path "src/config_pkg.sv") src/config_pkg.sv

.PHONY: sim-vga sim-vcd generate-vcd clean vivado-all vivado-build vivado-program

# Simulation
sim-vga: $(VGA_SIM_BINARY)
	./$<
sim-vcd: $(TOP_VCD)
	gtkwave $<
generate-vcd: $(TOP_VCD)
$(TOP_VCD): $(VCD_SIM_BINARY) $(VCD_DIR)
	./$<
$(VCD_SIM_BINARY): $(VCD_SIM_DIR) $(SOURCES)
	verilator \
		$(SOURCES) \
		--binary --timing --trace --Wall --Wno-fatal \
		--top-module $(TOP_MODULE) \
		--Mdir $< \
		-GDUMPFILEPATH=\"$(TOP_VCD)\"
$(VGA_SIM_BINARY): $(VGA_SIM_DIR) $(SOURCES) test/vga-simulation/simulator.cpp test/config.hpp
	verilator \
		$(SOURCES) test/vga-simulation/simulator.cpp \
		--cc --exe --build --timing --trace --Wall --Wno-fatal \
		--top-module $(TOP_MODULE) \
		--Mdir $< \
		-LDFLAGS "-lglut -lGLU -lGL"
$(VGA_SIM_DIR):
	@mkdir -p $@
$(VCD_SIM_DIR):
	@mkdir -p $@
$(VCD_DIR):
	@mkdir -p $@

# Vivado synthesizing
vivado-all: vivado-build vivado-program
vivado-program: scripts/vivado.sh scripts/program.tcl build/main.bit
	sh scripts/vivado.sh -mode batch -source $(shell pwd)/scripts/program.tcl
vivado-build: scripts/vivado.sh scripts/build.tcl
	sh scripts/vivado.sh -mode batch -source $(shell pwd)/scripts/build.tcl

# Generate verible file for lsp
verible.filelist:
	@printf '%s\n' $(SOURCES) | sort > $@
# Set up configuration files
include config.mk

clean:
	rm -rf $(VGA_SIM_DIR) $(VCD_SIM_DIR) $(VCD_DIR) build verible.filelist src/config_pkg.sv test/config.hpp
