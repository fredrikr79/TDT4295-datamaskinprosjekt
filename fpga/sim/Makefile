# VGA simulator. Run from the repo root:
#
#   nix develop -c make -C fpga/sim run     # NixOS / no verilator installed
#   make -C fpga/sim run                    # verilator already on PATH
#
# -Wno-fatal is required: the vendored RTL has unused-parameter warnings that
# -Wall makes fatal on Verilator 5.x. Two steps instead of --build, which only
# exists in Verilator >= 4.204 (Ubuntu 22.04 ships 4.038).

VGA := ../src/vga/vga-simulation
OBJ := ../build/sim

run: $(OBJ)/Vdisplay
	$<

$(OBJ)/Vdisplay: $(VGA)/display.v $(VGA)/vga_controller.v $(VGA)/graphics.v $(VGA)/simulator.cpp
	verilator -Wall -Wno-fatal --cc --exe --Mdir $(OBJ) \
		-LDFLAGS "-lglut -lGLU -lGL" $^
	$(MAKE) -C $(OBJ) -f Vdisplay.mk Vdisplay

clean:
	rm -rf $(OBJ)

.PHONY: run clean
