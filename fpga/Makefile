TOP_MODULE ?= tb_main

SOURCE_DIRECTORIES ?= src test

SIM_BUILD_DIRECTORY := sim-build

VCD_DIRECTORY := vcd
TOP_VCD := $(VCD_DIRECTORY)/$(TOP_MODULE).vcd

SOURCES := $(shell find $(SOURCE_DIRECTORIES) \-type f \( -name "*.v" -o -name "*.sv" \))

.PHONY: default gtkwave run-vcd compile-vcd run-graphics compile-graphics clean list vivado-all vivado-build vivado-program

# Simulation
default: run-graphics
gtkwave: run-vcd
	gtkwave $(TOP_VCD)
run-vcd: compile-vcd $(VCD_DIRECTORY)
	./$(SIM_BUILD_DIRECTORY)/V$(TOP_MODULE)
run-graphics: compile-graphics
	./$(SIM_BUILD_DIRECTORY)/V$(TOP_MODULE)
compile-vcd: $(SIM_BUILD_DIRECTORY) $(SOURCES)
	verilator \
		$(SOURCES) \
		--binary --timing --trace --Wall --Wno-fatal \
		--top-module $(TOP_MODULE) \
		--Mdir $(SIM_BUILD_DIRECTORY) \
		-GDUMPFILEPATH=\"$(TOP_VCD)\"
compile-graphics: $(SIM_BUILD_DIRECTORY) $(SOURCES) test/vga-simulation/simulator.cpp
	verilator \
		$(SOURCES) test/vga-simulation/simulator.cpp \
		--cc --exe --build --timing --trace --Wall --Wno-fatal \
		--top-module $(TOP_MODULE) \
		--Mdir $(SIM_BUILD_DIRECTORY) \
		-LDFLAGS "-lglut -lGLU -lGL"
$(SIM_BUILD_DIRECTORY):
	@mkdir -p $(SIM_BUILD_DIRECTORY)
$(VCD_DIRECTORY):
	@mkdir -p $(VCD_DIRECTORY)

# Vivado synthesizing
vivado-all: vivado-build vivado-program
vivado-program: scripts/vivado.sh scripts/program.tcl build/main.bit
	sh scripts/vivado.sh -mode batch -source $(shell pwd)/scripts/program.tcl
vivado-build: scripts/vivado.sh scripts/build.tcl
	sh scripts/vivado.sh -mode batch -source $(shell pwd)/scripts/build.tcl

clean:
	rm -rf $(SIM_BUILD_DIRECTORY) $(VCD_DIRECTORY) build
