# MCU I/O layers

How the microcontroller talks to the outside world: the FPGA, the keyboard and
mouse, and the SD card. The same code runs on the STM32 and in the PC (host)
build; only a thin backend per platform differs.

| Doc | What it covers |
|---|---|
| [fpga.md](fpga.md) | `fpga.c`: the command queue to the FPGA, the protocol, ACK/NACK, error recovery |
| [transport.md](transport.md) | `transport_*`: moving bytes over OCTOSPI (or the host emulator), the READY line, the fake FPGA |
| [sprite.md](sprite.md) | `sprite.c`: images, sprites and animations on the FPGA's HUD layer, loaded from the SD card |
| [storage.md](storage.md) | `storage.h`: files on the SD card (FileX) or in a folder on the PC |
| [usb.md](usb.md) | `usb_input.c`: keyboard and mouse through TinyUSB (MCU) or SDL (PC) |

The FPGA team's protocol spec is [`../mcu_fpga_spi_api.md`](../mcu_fpga_spi_api.md).

---

## The big picture

```
                          game.c  (state machine: probe -> info -> load -> run -> save)
          ┌───────────────────┬──────────────┼───────────────┬────────────────┐
          │                   │              │               │                │
     usb_input.c         sprite.c        rooms.c        frame_sync.c       cli.c
   (keys, mouse)    (HUD images/sprites) (tile rooms)   (FPGA frames)   (debug console)
          │              │       │          │   │
          │              │       └────┬─────┘   │
          │              │            │         │
          │              │       storage.h      │
          │              │     ┌──────┴──────┐  │
          │              │  storage_filex  storage_host
          │              │   (SD card)      (folder)
          │              │                      │
          │              └──────────┬───────────┘
          │                      fpga.c   (command queue)
          │                         │
          │                    transport.h
          │                  ┌──────┴───────┐
          │           transport_ospi    transport_host ── fake_fpga.c ── display_host.c
          │           (OCTOSPI + DMA)   (timing model)    (what the FPGA does)  (SDL window)
          │
     usb_backend.h
     ┌────┴──────────┐
usb_backend_tinyusb  usb_backend_sdl
     │                   (SDL window)
usb_port_stm32u5 / _stm32n6
(clocks, pins, VBUS)
```

## Which file is which

| Layer | Portable (app) | STM32 backend | Host backend |
|---|---|---|---|
| FPGA commands | `fpga.c`, `fpga.h` | — | — |
| Byte transport | `transport_core.c`, `transport.h` | `transport_ospi.c` | `transport_host.c` + `fake_fpga.c` |
| Sprites | `sprite.c`, `sprite.h`, `assets.c` | — | — |
| Files | `storage.h`, `rooms.c` | `storage_filex.c` | `storage_host.c` |
| Input | `usb_input.c`, `usb_input.h` | `usb_backend_tinyusb.c` + `usb_port_<chip>.c` | `usb_backend_sdl.c` |

A backend is chosen by which `.c` file CMake compiles into that target. Every
backend defines the same functions (or the same `BACKEND` pointer), so the code
above it can't tell them apart. Nothing is switched at run time and there are no
`#ifdef HOST` checks in app code.

## Rules that hold everywhere

- **Everything runs from the main loop.** Each layer has a `*_poll()` function
  that does a bit of work and returns; nothing waits for hardware. The only
  interrupt work is setting flags: the OCTOSPI transfer-done flags, the READY
  edge counter, the SYNC frame counter and the USB interrupt.
  Logging from an interrupt is not allowed: on the MCU, `LOG_*` sends over a
  blocking UART.
- **Done callbacks run from `*_poll()`**, never from an interrupt. They may queue
  new work.
- **Storage is the one blocking layer.** An SD read takes about a millisecond,
  which is fine from the main loop but never from a callback or an interrupt.
- **Errors are values, not hangs.** A missing card, a missing file, a NACK from
  the FPGA or a full pool returns an error code or `NULL` and is logged. The game
  carries on, and FPGA errors lead to a resync (see [fpga.md](fpga.md)).
- **Buffers handed to DMA stay valid** until the command's done callback. The
  FPGA queue copies the command, not the data.

## The main loop, roughly

```c
for (;;) {
    usb_poll();        /* TinyUSB task / SDL events -> key and mouse state       */
    transport_poll();  /* host only: advances the simulated link and fake FPGA    */
    fpga_poll();       /* moves the running FPGA command along, calls callbacks   */
    game_poll();       /* state machine; calls sprite_poll() in LOAD and RUN      */
    cli_poll();        /* debug console                                           */
}
```

## Frame sync, briefly

The FPGA pulses SYNC each time it finishes a frame. `frame_sync_isr()` counts
the pulses. The game calls `frame_sync_take(&seen)`, which returns how many
frames went by since it last looked: `0` means no new frame yet, `> 1` means the
game fell behind. One game step runs per new frame. On the host, the fake FPGA
pulses SYNC 30 times a second.
