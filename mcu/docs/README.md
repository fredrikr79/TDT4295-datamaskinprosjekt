# MCU: the microcontroller side

The STM32 code for the game. The MCU loads rooms and images from the SD card and
sends them to the FPGA, reads the USB keyboard, mouse and controller, and runs
the game logic. The FPGA runs the falling-sand simulation and draws the picture.

The same code also builds as a PC program (the **host emulator**), with a fake
FPGA in an SDL3 window, so you can work without any hardware.

It builds for three targets, all from the same code in `hexMCU/src/`:

| Target | What it is |
|---|---|
| **hexU59** | the final PCB, STM32U595ZJT6 |
| **hexU54** | the Nucleo-U545RE-Q dev board we used before the PCB existed. **Doesn't link right now**, see *Known issues* |
| **host** | the PC emulator (Linux) |
| **n6test** | a USB-only test program for the Nucleo-N657X0-Q. Not in the workspace, see *The N6 USB test* |

---

## Setup

### 1. Get the submodules

TinyUSB and SDL3 are git submodules in `hexMCU/third_party/`. Fetch them once
after cloning (from anywhere inside the repo):

```bash
git submodule update --init --recursive
```

Without them, the USB code and the host build won't compile.

### 2. Install the tools

- **VS Code** with ST's **STM32CubeIDE for Visual Studio Code** extension. It
  brings the ARM toolchain (arm-none-eabi-gcc), CMake, Ninja, clangd and the
  ST-LINK tools with it.
- **Linux:** run *Install ST-Link udev rules* from the extension once.
  Without the rules, flashing fails with a permission error.
- **For the host emulator:** gcc, CMake, Ninja and gdb, plus Microsoft's
  **C/C++** extension for debugging it in VS Code.
- **STM32CubeMX**, to generate the CubeMX code (next step) and to change pins
  or peripherals.

### 3. Generate the CubeMX code

`Drivers/` and `Middlewares/` (ST's HAL, CMSIS and FileX) are not in the git
repo, so they have to be generated before anything builds. For each board you
want to build:

1. Open its `.ioc` in STM32CubeMX:
   - `hexMCU/cube/hexU59/hexU59.ioc` (the final board)
   - `hexMCU/cube/hexU54/hexU54.ioc` (the Nucleo)
2. Click **Generate Code**, into the same folder.

Do this **before** *Setup STM32Cube project* in the next steps. Generating
doesn't touch our code: it's all outside `Core/`, and `main.c` only calls
`myMain()`.

### 4. Open the workspace

In VS Code: *File → Open Workspace from File…* → **`mcu/mcu.code-workspace`**.

Open the workspace, not a folder: it shows the shared code and each project as
folders of their own, which is what lets the STM32 extension find the board
projects (see *How the code is laid out* below).

### 5. Set up the STM32 projects

1. Open the STM32 extension (its icon in the left bar) and click
   *Setup STM32Cube project*.
2. It lists the projects it found: **hexU59**, **hexU54** (and **host**). Pick
   the one to set up, then click **Save and close**.
3. Do the same for the other board if you need it.

This only has to be done once per project and machine.

**If VS Code asks for a kit, ignore it** (close the question): the projects
pick their compiler through their CMake presets. **If it asks for a configure
preset (Debug or Release), pick Debug.**

### 6. Build, flash and debug

Everything is under **Run and Debug** (the play button with a bug, or F5).
Pick a configuration in the list at the top and press F5:

| Configuration | What it does |
|---|---|
| MCU: ST-Link (flash + debug) **(hexU59 …)** | builds the final board, flashes it, stops at `main()` |
| MCU: ST-Link (flash + debug) **(hexU54 …)** | the same for the Nucleo |
| Host: game (gdb) **(host …)** | builds the emulator and runs the game under gdb |
| Host: game, 2 % errors (gdb) | the same with 2 % failed transfers, to watch the resync |

Press F5 again (continue) to run after it stops at `main()`.

- **Nucleo:** plug the ST-LINK USB port (CN1) into the PC.
- **PCB:** connect an ST-LINK to the SWD programming header, and power the
  board first (see *Using the board*).

**The build button in the bottom bar** only becomes usable after you have
built once through *Run and Debug*. After that, a menu next to it lets you pick
the target to build (hexU59, hexU54 or host).

### If you change pins or peripherals in CubeMX

- Open the board's `.ioc` (`hexMCU/cube/hexU59/hexU59.ioc` or
  `hexMCU/cube/hexU54/hexU54.ioc`) in STM32CubeMX, change it, and generate
  code into the same folder.
- Only code between `/* USER CODE BEGIN */` and `/* USER CODE END */` survives.
  Ours is outside `Core/` anyway. `main.c` only calls `myMain()`.
- **Keep SDMMC1 enabled** in CubeMX, with only its *init call* turned off
  (Project Manager → Advanced Settings → "Do Not Generate Function Call").
  If the peripheral itself is off, `HAL_SD_MspInit()` isn't generated, the
  SD card gets no clock, and the log says `SDMMC1 has no clock`.
- The OCTOSPI1 global interrupt must stay enabled, with a GPDMA channel on
  `hospi1`. Without it, no transfer ever finishes.

---

## How the code is laid out

The workspace shows the parts you work on and hides the rest. On disk it all
looks like this:

```
mcu/
├── README.md               points here
├── mcu.code-workspace      open this in VS Code
├── docs/                   this file; README2.md (the older docs index); io/*.md
├── tools/                  png2spr.py, spr_viewer.html, ttf2c.py
└── hexMCU/
    ├── .clangd             go-to-definition for src/ (uses hexU59's build)    [hidden]
    ├── src/
    │   ├── app/            portable: game, fpga.c, cli.c, rooms, render/ ...
    │   ├── include/        the headers both ports share
    │   └── port/
    │       ├── stm32/      OCTOSPI, SD card (FileX), UART, GPIO, usb/
    │       └── host/       fake FPGA, SDL window, a folder as SD card
    ├── cmake/              sources.cmake, stm32u5.cmake, tinyusb_patches.cmake, n6_usbtest.cmake
    ├── cube/
    │   ├── hexU59/         final board: hexU59.ioc, Core/, Drivers/, CMakeLists.txt
    │   ├── hexU54/         Nucleo: hexU54.ioc, Core/, Drivers/, CMakeLists.txt
    │   └── n6test/         USB test on the Nucleo-N657X0-Q                [not in the workspace]
    ├── host/               host CMakeLists.txt, .vscode/ (tasks, debug)
    ├── third_party/        git submodules: tinyusb, SDL                   [hidden]
    └── build/              host build output                             [hidden]
```

What the workspace shows as folders:

| Workspace folder | On disk |
|---|---|
| src (shared code) | `hexMCU/src` |
| cmake (shared build files) | `hexMCU/cmake` |
| docs | `docs` |
| tools | `tools` |
| hexU59 (final board, U595) | `hexMCU/cube/hexU59` |
| hexU54 (Nucleo-U545RE-Q) | `hexMCU/cube/hexU54` |
| host (PC emulator) | `hexMCU/host` |

Why not just open `mcu/`? VS Code's STM32 extension refuses a folder that holds
more than one CMake project, and `mcu/` holds five (the two boards, host, and
TinyUSB and SDL in `third_party/`). So each project is a folder of its own.

**What's shared and what isn't:**

- `src/app/` is the portable code. It never touches hardware; it reaches it
  through the headers in `src/include/` (`transport.h`, `storage.h`,
  `usb_backend.h`, `platform.h`, `io.h`).
- `src/port/stm32/` and `src/port/host/` implement those headers. Which one
  is compiled decides where the code runs.
- `cmake/sources.cmake` lists the shared files; `cmake/stm32u5.cmake` adds
  everything an STM32U5 build needs (our STM32 port, TinyUSB) and is used by
  both board projects.
- `cube/hexU59` and `cube/hexU54` differ only in CubeMX's generated files
  (`Core/`, `Drivers/`, startup file, linker script) and the USB controller:
  OTG_HS with UCPD on the U595, the full-speed controller on the U545.
- `cube/n6test` builds only the USB part, through `cmake/n6_usbtest.cmake`
  (see *The N6 USB test*).
- `third_party/` is not ours and is not changed. Our TinyUSB fixes live in
  `cmake/tinyusb_patches.cmake` and `src/port/stm32/usb/`.

---

## The N6 USB test

`hexMCU/cube/n6test` is a small program for the **Nucleo-N657X0-Q** that only
tests USB input. We used it to develop and debug the USB host before the PCB
existed, since the N6 has the same high-speed USB controller as the U595 (the
U545 doesn't). It shares most of the USB code with the real builds
(`src/port/stm32/usb/`, TinyUSB with our fixes), but builds through its own
`cmake/n6_usbtest.cmake` instead of `stm32u5.cmake`, with none of the game.

It is not in the workspace. To build and flash it, open **`hexMCU/cube/n6test`
as the root folder** in VS Code (*File → Open Folder…*), set it up with
*Setup STM32Cube project* as above (generate its `.ioc` in CubeMX first, like
the boards), and use *Run and Debug*.

---

## Known issues

### hexU54 (the Nucleo) doesn't link

Building hexU54 now stops with:

```
RAM:      744640 B       256 KB    284.06%
collect2: error: ld returned 1 exit status
```

The rooms grew to the final screen size: `src/include/rooms.h` changed from

```c
#define ROOM_X 40
#define ROOM_Y 60
```

to

```c
#define ROOM_X 640
#define ROOM_Y 360
```

and a whole room is kept in RAM while it's loaded or saved. That fits in the
U595's 2.5 MB, but not in the U545's 256 KB.

- **To build hexU54 anyway**, for example to test something else on the
  Nucleo: set `ROOM_X` and `ROOM_Y` back to 40 and 60 for that build.
- **The real fix** would be to load and save a room a smaller chunk at a time
  (a few rows from the SD card, send them to the FPGA, the next few rows, …)
  instead of through one buffer for the whole room. We didn't, since the U595
  has the memory; the report discusses this choice in the section on the MCU.

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
| `info` | Ask the FPGA for screen size (and the grid it gives), N and session |
| `readrdy` | READY pin, ACK, and the command queue right now |
| `sd`, `sd ls [folder]` | SD card status (mounts it if it can), and what's on it |
| `led list`, `led show <state>`, `led cycle`, `led r` … `led auto` | Test the status LED, no FPGA needed |
| `led pins` | The LED pins read back from the GPIO registers: mode, what is driven, what the pin reads |
| `button`, `button watch` | The board button now / watch it for 15 s and say whether pin and interrupt work |
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
sprites/*.spr      images and animations (`tools/png2spr.py`)
```

---

## Host emulator

Runs the game on the PC. A fake FPGA in an SDL3 window takes the same commands
as the real one, the keyboard and mouse are SDL's, and the console is the
terminal.

Run it from **Run and Debug** (*Host: game*). It builds `hexMCU/build/host`
first. The same without VS Code, from `mcu/hexMCU`:

```bash
cmake -S host -B build/host -G Ninja
cmake --build build/host
./build/host/hexmcu_host              # the game
./build/host/hexmcu_host --errors=2   # the same, 2 % of transfers fail (resync test)
./build/host/hexmcu_host --help       # also --mhz=N and --lanes=N for the link
```

- **The SD card:** the folder in `$HEXMCU_SD` (the debug configuration sets it
  to `hexMCU/sd/`). Lay it out like the card: `sd/rom/`, `sd/save/`,
  `sd/sprites/`. No folder behaves like no card.
- **Tasks** (*Terminal → Run Task*): `build host`.
- **The link:** modelled at 20 MHz × 8 lanes, with a READY 200 µs after each
  command and SYNC at 25 Hz. A share of transfers can be made to fail on
  purpose, to test the resync.
- **The picture:** like the real FPGA, a 640x360 screen with the simulation
  grid at 160x90 cells, each cell shown as a 4x4 block (`FPGA_SIM_SCALE` in
  `fpga.h`). CONS and BOX commands count cells; sprites and the mouse count
  screen pixels. INFO reports the screen (640x360).

See [io/transport.md](io/transport.md) for what the fake FPGA does.

---

## Documentation

- [README2.md](README2.md): the older docs index
- [io/README.md](io/README.md): overview and the main loop
- [io/fpga.md](io/fpga.md): the command queue, protocol, error recovery
- [io/transport.md](io/transport.md): OCTOSPI, READY/ACK, the fake FPGA
- [io/sprite.md](io/sprite.md): images and sprites on the HUD
- [io/storage.md](io/storage.md): the SD card
- [io/usb.md](io/usb.md): keyboard, mouse and controller
