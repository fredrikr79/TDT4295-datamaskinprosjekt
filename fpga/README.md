# FPGA Development Setup

This project uses VS Code + TerosHDL + Verilator for development and AMD Vivado for Xilinx FPGA synthesis, implementation, bitstream generation, and JTAG programming.

Vivado is run inside a Distrobox container, but the VS Code tasks do not depend on that setup directly.

## Project Structure

fpga/
├── .vivado.env              # Machine-specific Vivado/Distrobox settings
├── build/                    # Generated files (safe to delete)
│   ├── main.bit
│   └── vivado/
├── constraints/
│   └── arty_a7_35T.xdc       # Arty A7-35T pin constraints
├── scripts/
│   ├── build.tcl             # Vivado build script
│   ├── program.tcl           # JTAG programming script
│   └── vivado.sh             # Vivado launcher wrapper
└── src/
    └── main.v                 # Verilog source

## Requirements

VS Code

TerosHDL extension

Verilator

AMD Vivado

Distrobox (for the current Vivado setup)

Arty A7-35T FPGA board for programming

TerosHDL/VS Code handles editing, HDL analysis and development. Vivado handles the Xilinx-specific build and programming steps.

## Machine-Specific Vivado Setup

The file:

fpga/.vivado.env

contains the local Vivado launch configuration.

Example:

VIVADO_CONTAINER="vivado"
VIVADO_SETTINGS="/home/oyvne/2026.1/Vivado/settings64.sh"

Do not commit this file if it contains paths or settings specific to your machine.

The launcher is:

fpga/scripts/vivado.sh

It starts Vivado in the configured environment and uses:

fpga/build/vivado/

as its working directory so Vivado log/journal files do not appear in the repository root.

## Building the FPGA

From the project root, run:

./fpga/scripts/vivado.sh     -mode batch     -source fpga/scripts/build.tcl

A successful build produces:

fpga/build/main.bit

Vivado's generated project and temporary files are stored under:

fpga/build/vivado/

## Programming the FPGA

Connect the Arty A7-35T via USB/JTAG, then run:

./fpga/scripts/vivado.sh     -mode batch     -source fpga/scripts/program.tcl

The programming script uses:

fpga/build/main.bit

and programs the FPGA through Vivado Hardware Manager/JTAG.

If Vivado reports that no hardware target is available, check that the board is connected and that the Distrobox environment has access to the Digilent JTAG device and hw_server.

## VGA Simulation

Simulate the VGA output with Verilator instead of building a bitstream. Opens a window
showing what the design would drive onto the VGA connector; rebuilds take seconds.

nix develop -c make -C fpga/sim run     # NixOS, or no verilator installed
make -C fpga/sim run                    # verilator already on PATH
make -C fpga/sim clean

On Debian/Ubuntu, instead of Nix:
  sudo apt-get install build-essential verilator libglu1-mesa-dev freeglut3-dev

## VS Code Tasks

The VS Code tasks provide the normal workflow without exposing the Distrobox details.

Open the project in VS Code and use:

Terminal → Run Task

Available tasks:

Vivado: Build

Runs synthesis, implementation and bitstream generation.

Vivado: Program FPGA

Programs the generated .bit file onto the connected FPGA.

FPGA: Build and Program

Runs both tasks sequentially:

Build → Program

This is the normal one-click workflow after making a change.

Typical Workflow

Edit Verilog in fpga/src/.

Use TerosHDL/Verilator for HDL checking.

Run Vivado: Build.

Confirm fpga/build/main.bit was generated.

Connect the Arty A7-35T.

Run Vivado: Program FPGA.

Or simply run:

FPGA: Build and Program

from the VS Code task menu.

Cleaning the Build

All Vivado-generated files are contained in fpga/build/.

To completely clean the generated build:

rm -rf fpga/build

The next build recreates the required directories.

Git

Generated files should not normally be committed.

Recommended .gitignore entries:

fpga/build/
fpga/.vivado.env

Keep the source, constraints, Tcl scripts, VS Code task configuration, and other project configuration under version control.

## Notes

The Vivado part is configured for the Arty A7-35T:
xc7a35tcsg324-1.

The Vivado top-level module is currently main.

The Verilog source should therefore contain a module named main.

The XDC constraints must use port names matching the top-level Verilog module.