# hexMCU: the microcontroller side

This folder holds the STM32 code for the game. The MCU loads rooms and images
from the SD card and sends them to the FPGA, reads the USB keyboard and mouse,
and runs the game logic. The FPGA runs the falling-sand simulation and draws
the picture.

The same code also builds as a PC program (the **host emulator**), with a fake
FPGA in an SDL3 window, so you can work without any hardware.

| Folder | What it is |
|---|---|
| `./` (this folder) | **Nucleo-U545RE-Q** dev board: CubeMX project `hexMCU.ioc`, `CMakeLists.txt` |
| `cube/hexU59/` | **The final PCB** (STM32U595ZJT6): its own CubeMX project `hexU59.ioc` and `CMakeLists.txt` |
| `src/app/` | the game and drivers, portable: the same files in every build |
| `src/port/stm32/` | STM32 backends: OCTOSPI, SD card (FileX), UART, GPIO |
| `src/port/host/` | PC backends: fake FPGA, SDL window, a folder as the SD card |
| `host/` | CMake project for the host emulator |
| `cmake/` | `sources.cmake` (the common file list), `stm32u5.cmake` (shared by both STM32 projects) |
| `tools/` | `png2spr.py` (PNG → sprite file), `spr_viewer.html`, `ttf2c.py` (font → C) |
| `docs/` | how each part works, see [docs/io/README.md](docs/io/README.md) |

Both STM32 projects compile the same code from `src/` through
`cmake/stm32u5.cmake`. Only CubeMX's generated files (`Core/`, `Drivers/`,
startup file, linker script) and the USB controller setting differ.

---

## Setup

### 1. Get the code, with submodules

```bash
git clone <repo url>
cd <repo>
git submodule update --init --recursive
```

SDL3 and TinyUSB are submodules. Without them, the host build and the USB
input won't compile.

### 2. Tools

- **VS Code** with ST's **STM32Cube for Visual Studio Code** extension. It
  installs the ARM toolchain (arm-none-eabi-gcc), CMake, Ninja and the ST-LINK
  tools.
- **STM32CubeMX**, only if you need to change pins or peripherals (`.ioc`).
- **Linux:** run *Install ST-Link udev rules* from the extension's command
  list once. Without the rules, flashing fails with a permission error.
- **Host emulator:** a C compiler, CMake ≥ 3.22. SDL3 comes from the submodule.

### 3. Open the right folder in VS Code

The extension only recognises a project when its folder is the **root** of the
VS Code window:

| Board | Open this folder |
|---|---|
| Nucleo-U545RE-Q | `mcu/hexMCU` |
| Final PCB (STM32U595ZJT6) | `mcu/hexMCU/cube/hexU59` |

Then click **Setup STM32Cube project** when it asks, or run that from the
command list. Pick the `Debug` preset, and build with the extension's build button.

> Opening the parent folder (or the repo root) gives you no build, flash or
> debug buttons: the extension looks for the `.ioc` + `CMakeLists.txt` in the
> root only.

### 4. Flash and debug

- **Nucleo:** plug the ST-LINK USB port (CN1) into the PC.
- **PCB:** connect an ST-LINK to the SWD programming header. Power the board
  first; see *Boot order* below.

Use **Run and Debug** (F5) in VS Code. It builds, flashes and stops at `main()`.
Press F5 again to run.

### If you regenerate code in CubeMX

- Only code between `/* USER CODE BEGIN */` and `/* USER CODE END */` survives.
  Ours is outside `Core/` anyway. `main.c` only calls `myMain()`.
- **Keep SDMMC1 enabled** in CubeMX, with only its *init call* turned off
  (Project Manager → Advanced Settings → "Do Not Generate Function Call").
  If the peripheral itself is off, `HAL_SD_MspInit()` isn't generated, the
  SD card gets no clock, and the log says `SDMMC1 has no clock`.
- The OCTOSPI1 global interrupt must stay enabled, with a GPDMA channel on
  `hospi1`. Without it, no transfer ever finishes.

---

## The debug console

The MCU has a command line on **USART1**: 115200 baud, 8N1, no flow control.

- **Nucleo:** USART1 goes through the ST-LINK's **virtual COM port**, so the
  same USB cable used for flashing also carries the console.
- **PCB:** <!-- TODO: say how: ST-LINK V3's VCP pins on the header, or a USB-serial adapter on PA9/PA10 --> connect USART1 TX (PA9) and RX (PA10).

Any serial terminal works:

```bash
# Linux (the port is usually /dev/ttyACM0; check with: ls /dev/ttyACM*)
minicom -b 115200 -D /dev/ttyACM0        # quit: Ctrl-A, then X
picocom -b 115200 /dev/ttyACM0           # quit: Ctrl-A, then Ctrl-X
screen /dev/ttyACM0 115200               # quit: Ctrl-A, then K
```

- **Windows:** PuTTY, connection type *Serial*, the COM port shown in Device
  Manager under "Ports (COM & LPT)", speed 115200.
- **VS Code:** the *Serial Monitor* extension works too.

If you get no access to `/dev/ttyACM0` on Linux, add yourself to the
`dialout` group (`sudo usermod -aG dialout $USER`, then log in again), or use
`sudo`.

Press Enter and you get the `>` prompt. `help` lists every command.

### Logging

The log is quiet by default. Turn it up with `log`:

| Level | Shows |
|---|---|
| `log 0` | nothing |
| `log 1` | errors |
| `log 2` | + warnings |
| `log 3` | + info: state changes, what is loading, the handshake |
| `log 4` | + debug |

`trace on` adds one line per OCTOSPI transfer; you need `log 4` to see them.
The highest level that can be shown is set at build time with
`HEXMCU_LOG_LEVEL` in CMake (default info). `log` with no number shows both.

<!-- TODO: check which level the board starts at, and write it here -->

### Useful commands

| Command | What it does |
|---|---|
| `loading off` / `loading on` | Off: the game stops, along with the handshake retries, and the FPGA is yours to test by hand. On: handshake again, then carry on. |
| `demo` | Lists the FPGA tests; `demo echo`, `demo cons`, `demo box`, `demo sprite`, `demo scene`, `demo clear` runs one (PASS/FAIL in the log) |
| `fecho -r 100 64` | 100 rounds of a 64-byte random ECHO: a quick link test |
| `info` | Ask the FPGA for screen size, N and session |
| `readrdy` | READY pin, ACK, and the command queue right now |
| `sd`, `sd ls [folder]` | SD card status (mounts it if it can), and what's on it |
| `led list`, `led show <state>`, `led cycle`, `led r` … `led auto` | Test the status LED, no FPGA needed |
| `reset fpga` / `reset mcu` / `reset` | Reset the FPGA (reset pin), the MCU, or both |
| `abort` | Drop every queued FPGA command |

---

## Using the board

### Boot order

<!-- TODO with the PCB team: power source, switches, what must not be connected during boot -->

1. Power the board. The FPGA loads its design from flash; DONE goes high.
2. The MCU waits for DONE, then for READY to be steady, then starts the
   handshake (ECHO, INFO). The console shows `probing FPGA: x3 (...)` while it
   retries.
3. Once the FPGA answers, the MCU loads the first room from the SD card and
   the sprites, then the game runs.

### The status LED

The most important thing going on, as colour and blink. `led list` shows
them in this order:

| Colour, pattern | Meaning |
|---|---|
| red, steady | FPGA has no design loaded (DONE low) |
| magenta, fast blink | READY storm: the READY line is toggling by itself |
| red, slow blink | the FPGA doesn't answer the handshake |
| yellow, fast blink (1 s) | a command failed, resyncing |
| yellow, double blink | no SD card |
| blue, double blink | no keyboard or mouse |
| cyan, steady | hold (`loading off`) |
| green, heartbeat | the game runs |

### When it hangs

- Type `reset fpga` (or `reset` for both) on the console. Or press the reset
  button for the MCU only.
- Red slow blink that never stops: check the FPGA is programmed (DONE), and
  look at `readrdy`.
- The game recovers by itself from a failed command: it does the handshake
  again, and if the FPGA kept its memory (same session) it carries on.

### The SD card

FAT32. Laid out like this:

```
rom/x,y.dat        the rooms as shipped (x,y = room coordinates)
save/x,y.dat       saved rooms; read before rom/ if they exist
sprites/*.spr      images and animations (tools/png2spr.py)
```

---

## Host emulator

Runs the game on the PC. A fake FPGA in an SDL3 window takes the same
commands as the real one. A folder plays the SD card. The keyboard and mouse
are SDL's, and the console is the terminal.

```bash
cmake -S host -B build/host
cmake --build build/host -j
./build/host/<executable>        # TODO: name of the host executable
```

- **The SD card:** the folder in `$HEXMCU_SD`, or `host/sd/` by default. Lay
  it out like the card: `sd/rom/`, `sd/save/`, `sd/sprites/`. No folder
  behaves like no card.
- **The link:** modelled at 20 MHz × 8 lanes, with a READY 200 µs after each
  command and SYNC at 25 Hz. A share of transfers can be made to fail on
  purpose, to test the resync.
- **`test_host`** is a second executable: a small example game on top of
  `fpga.c` (picture into the grid, bouncing sprite). `--errors=N` fails N % of
  transfers.

See [docs/io/transport.md](docs/io/transport.md) for what the fake FPGA does.

---

## Documentation

- [docs/io/README.md](docs/io/README.md): overview and the main loop
- [docs/io/fpga.md](docs/io/fpga.md): the command queue, protocol, error recovery
- [docs/io/transport.md](docs/io/transport.md): OCTOSPI, READY/ACK, the fake FPGA
- [docs/io/sprite.md](docs/io/sprite.md): images and sprites on the HUD
- [docs/io/storage.md](docs/io/storage.md): the SD card
- [docs/io/usb.md](docs/io/usb.md): keyboard and mouse
- [mcu_fpga_spi_api.md](mcu_fpga_spi_api.md): the protocol spec agreed with the FPGA team
