TOP_MODULE ?= main

SOURCE_DIRECTORIES ?= src test

BUILD_DIRECTORY := sim-build

VCD_DIRECTORY := vcd
TOP_VCD := $(VCD_DIRECTORY)/$(TOP_MODULE).vcd

SOURCES := $(shell find $(SOURCE_DIRECTORIES) \-type f \( -name "*.v" -o -name "*.sv" -o -name "*.cpp" \))

.PHONY: default view run compile clean list vivado-all vivado-build vivado-program

# Simulation
default: run
view: run
	gtkwave $(TOP_VCD)
run: compile
	@mkdir -p $(VCD_DIRECTORY)
	./$(BUILD_DIRECTORY)/V$(TOP_MODULE)
compile:
	@mkdir -p $(BUILD_DIRECTORY)
	verilator \
		$(SOURCES) \
		--binary --timing --trace --Wall --Wno-fatal \
		--top-module $(TOP_MODULE) \
		--Mdir $(BUILD_DIRECTORY) \
		-LDFLAGS "-lglut -lGLU -lGL"
list:
	@echo "$(SOURCES)"
clean:
	rm -rf $(BUILD_DIRECTORY) $(VCD_DIRECTORY)

# Vivado synthesizing
vivado-all: vivado-build vivado-program
vivado-program: scripts/vivado.sh scripts/program.tcl build/main.bit
	sh scripts/vivado.sh -mode batch -source $(shell pwd)/scripts/program.tcl
vivado-build: scripts/vivado.sh scripts/build.tcl
	sh scripts/vivado.sh -mode batch -source $(shell pwd)/scripts/build.tcl
