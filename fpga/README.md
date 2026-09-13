# FPGA Development Setup

This project uses VS Code + TerosHDL + Verilator or Make + Verilator for development and AMD Vivado
for Xilinx FPGA synthesis, implementation, bitstream generation, and JTAG
programming.

Vivado is run inside a Distrobox container, but the Makefile targets and VSCode
tasks do not depend on that setup directly.

## Project Structure

```
fpga
├── .vivado.env             # Machine specific Vivado/Distrobox settings
├── constraints
│   └── arty_a7_35T.xdc     # Arty A7-35T pin contstraints
├── Makefile
│   ├── sim-vga             # Runs an OpenGL simulation of the verilog VGA-pins
│   ├── generate-vcd        # Generates a VCD-file of the top module
│   ├── sim-vcd             # Opens the VCD-file directly in gtkwave
│   ├── vivado-build        # Build the verilog source to build folder
│   ├── vivado-program      # Program connected FPGA with current vivado build
│   ├── vivado-all          # Run vivado-build and vivado-program in sequence
├── scripts
│   ├── build.tcl           # Vivado build script
│   ├── program.tcl         # JTAG programming script
│   └── vivado.sh           # Vivado launcher wrapper
├── src                     # Verilog source code
│   └── vga                 # VGA-specific verilog source code
└── test                    # Verilog testbenches and C++-simulation code
    ├── tb_main.sv          # Top module in our verilog project
    └── vga-simulation
        └── simulator.cpp   # Responsible for simulating the VGA-screen
```

## Requirements

- Verilator
  - Simulates the verilog code without the FPGA-board
- AMD Vivado
  - Handles the Xilinx-specific build and programming steps.
- Distrobox (for the current Vivado setup)
- Arty A7-35T FPGA board for programming
- make

### With VSCode

- TerosHDL extension
  - handles editing, HDL analysis and development
- VaporView

## Machine-Specific Vivado Setup

### `fpga/.vivado.env`

- Contains the local Vivado launch configuration.
- Example:
  - `VIVADO_CONTAINER="vivado"`
  - `VIVADO_SETTINGS="/home/oyvne/2026.1/Vivado/settings64.sh"`
- Do not commit this file if it contains paths or settings specific to your
  machine.

The launcher is:

### `fpga/scripts/vivado.sh`

It starts Vivado in the configured environment and uses `fpga/build/vivado/` as
its working directory so Vivado log/journal files do not appear in the
repository root.

## Building the FPGA

From the project root, run:

```sh
./fpga/scripts/vivado.sh -mode batch -source fpga/scripts/build.tcl
```

A successful build produces `fpga/build/main.bit`

Vivado's generated project and temporary files are stored under
`fpga/build/vivado/`

## Programming the FPGA

Connect the Arty A7-35T via USB/JTAG, then run:

```sh
./fpga/scripts/vivado.sh -mode batch -source fpga/scripts/program.tcl
```

The programming script uses `fpga/build/main.bit` and programs the FPGA through
Vivado Hardware Manager/JTAG.

If Vivado reports that no hardware target is available, check that the board is
connected and that the Distrobox environment has access to the Digilent JTAG
device and hw_server.

## VGA Simulation

Simulate the VGA output with Verilator instead of building a bitstream. Opens a
window showing what the design would drive onto the VGA connector; rebuilds take
seconds.

Install dependencies on Debian/Ubuntu

```sh
sudo apt install build-essential verilator libglu1-mesa-dev freeglut3-dev
```

Now from the `fpga` folder run this script to simulate the vga

```sh
make sim-vga
```

This is also possible using VSCode tasks

## VS Code Tasks

The VS Code tasks provide the normal workflow without exposing the Distrobox
details.

Open the project in VS Code and use:

Terminal → Run Task

### Vivado: Build

Runs synthesis, implementation and bitstream generation.

### Vivado: Program FPGA

Programs the generated .bit file onto the connected FPGA.

### FPGA: Build and Program

Runs both tasks sequentially:

Build → Program

This is the normal one-click workflow after making a change.

### Verilator: Simulate VGA

Runs a simulation of the VGA-screen using current verilog code

### Verilator: Generate VCD

Generates a VCD file in `fpga/vcd`.
This file can be viewed using `gtkwave` or the VSCode extension VaporView.

## Typical Workflow

### Without FPGA-board

- Edit Verilog in `fpga/src/` or `fpga/test`.
- Run Verilator: Simulate VGA or `make sim-vga` to see if VGA has correct output
- Run Verilator: Generate VCD or `make sim-vcd` to view the generated waveforms

### With FPGA-board

- Edit Verilog in `fpga/src/`.
- Run Vivado: Build or `make vivado-build`
- Confirm `fpga/build/main.bit` was generated.
- Connect the Arty A7-35T.
- Run Vivado: Program FPGA or `make vivado-program`

It's also possible to run FPGA: Build and Program or `make vivado-all` to do both
steps at once.

## Cleaning the Build

All Vivado-generated files are contained in `fpga/build/`.

To completely clean the generated Vivado-build run:

```sh
rm -rf fpga/build
```

The next build recreates the required directories.

All genererated Verilator files are in `fpga/vcd-build` or `fpga/vga-build`.
The VCD waveform file is in `fpga/vcd`.

To completley clean the generated simulation files run

```sh
rm -rf fpga/vcd-build fpga/vga-build fpga/vcd
```

To clean both Vivado and Verilator files completley run `make clean` or FPGA: Clean.

## Git

Generated files should not normally be committed.

Recommended `.gitignore` entries:

```.gitignore
fpga/build/
fpga/.vivado.env
fpga/vga-build/
fpga/vcd-build/
fpga/vcd/
```

Keep the source, constraints, Tcl scripts, VS Code task configuration, and other
project configuration under version control.

## Notes

- The Vivado part is configured for the Arty A7-35T: xc7a35tcsg324-1.
- The Vivado top-level module is currently `main`.
  - The Verilog source should therefore contain a module named `main`.
- The XDC constraints must use port names matching the top-level Verilog module.
