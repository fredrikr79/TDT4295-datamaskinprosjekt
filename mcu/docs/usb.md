# USB input: keyboard and mouse

[← back to the overview](README.md)

The game reads keys and mouse movement through one small API. On the MCU the
input comes from a USB keyboard and mouse through TinyUSB; on the PC it comes
from the SDL window. The game can't tell which.

```
game.c ──── usb_input.h          input_take(&in); key_held(&in, KEY_W) ...
               │
          usb_input.c            translation: HID boot reports -> held / pressed / released
               │
          usb_backend.h          the contract between the two halves
       ┌───────┴────────┐
usb_backend_tinyusb.c   usb_backend_sdl.c
       │                (fake boot reports from SDL events)
usb_port_stm32u5.c / usb_port_stm32n6.c
(clocks, pins, IRQ handler, VBUS)
```

---

## Using it

```c
usb_init();                    /* once at start                    */
usb_poll();                    /* every main-loop iteration        */

input_t in;
input_take(&in);               /* once per game frame              */
if (key_held(&in, KEY_W))         ...   /* down right now            */
if (key_pressed(&in, KEY_ESCAPE)) ...   /* went down since last take */
if (mouse_pressed(&in, MOUSE_LEFT)) ...
cursor_x += in.mouse_dx;              /* movement since last take   */
```

- **`held`** is the state right now. **`pressed`** and **`released`** collect
  everything since the previous `input_take()`. A tap shorter than one frame
  still shows as pressed and released, even though it's no longer held.
- **Mouse movement** (`mouse_dx/dy`, `wheel`, `pan`) adds up between takes. +y is
  down and +wheel is scrolling away from you.
- **`in.keyboard` / `in.mouse`** tell you whether a keyboard or mouse is
  connected.
- **Key codes are USB HID usage IDs** (keyboard page 0x07). They name physical
  key positions with US labels: on a Norwegian keyboard Å is `KEY_LBRACKET`,
  Ø is `KEY_SEMICOLON` and Æ is `KEY_APOSTROPHE`. Modifiers are keys too:
  `key_held(&in, KEY_LSHIFT)`.
- **Debugging:** `input_log(&in)` logs what changed (keys, buttons, wheel,
  connects; movement at most 10 times a second), and `key_name(k)` gives
  `"W"`, `"SPACE"`, `"LSHIFT"`.

Call `input_take()` once per frame and pass the `input_t` around. A second call
in the same frame finds `pressed`/`released` already cleared.

## The translation layer: usb_input.c

Both backends hand over **HID boot protocol reports**, the fixed format every
keyboard and mouse supports:

| Device | Report |
|---|---|
| keyboard | 8 bytes: `{ modifiers, reserved, key[6] }`, so up to 6 keys at once plus the 8 modifier bits |
| mouse | 3–5 bytes: `{ buttons, dx, dy, [wheel], [pan] }`, signed 8-bit movement |

`usb_input.c` keeps two sets of state:
- `cur`: what the devices say right now (held keys and buttons, how many are
  connected);
- `acc`: what has been collected since the last `input_take()` (pressed and
  released bits, movement sums).

**Keyboard report:**
1. Modifier bit n becomes key `0xE0 + n`, and each nonzero key slot sets its key.
2. Comparing with `cur.held` gives the newly pressed keys (now and not before)
   and the released ones (before and not now). They're ORed into `acc`.
3. Slots holding 0x01–0x03 mean "too many keys" (rollover). Then the report is
   ignored and the last state kept.

**Mouse report:** button changes work the same way, and movement is added to
`acc`.

**Unplug:** `usb_input_connected(kind, false)` releases every key or button of
that kind, so nothing stays stuck down.

`input_take()` copies `cur` and `acc` into your `input_t`, then clears `acc`.

Everything runs in the main loop: TinyUSB calls its callbacks from inside
`tuh_task()`, which `usb_poll()` calls. So no locking is needed. All keyboards
share one key state; one keyboard plus one mouse is the expected setup.

## MCU backend: usb_backend_tinyusb.c

- **`usb_backend_init()`:**
  1. `usb_port_init()` sets up the chip side.
  2. `tuh_hid_set_default_protocol(HID_PROTOCOL_BOOT)` puts every device in boot
     protocol, so there's no report descriptor to parse.
  3. `tusb_init(0, host)` starts TinyUSB as a host.
- **`usb_backend_poll()`:** `usb_port_poll()` (VBUS), then `tuh_task()`, which
  runs the callbacks below.
- **`tuh_hid_mount_cb`:** a HID interface appeared. Keyboards and mice get a slot
  (up to `MAX_HID` = 8), are reported as connected, and the first report is
  requested. Other HID devices are ignored.
- **`tuh_hid_report_received_cb`:** passes the report to `usb_input.c`, then asks
  for the next one. Each request gives one report, so asking again is what keeps
  reports coming.
- **`tuh_hid_umount_cb`:** frees the slot and reports the device as disconnected.
- **`tusb_time_millis_api()`** returns `plat_millis()`. TinyUSB needs a clock when
  it runs without an RTOS (`OPT_OS_NONE`).

TinyUSB's callbacks are weak functions; defining them here replaces its empty
ones. `tusb_config.h` picks the driver (`CFG_TUSB_MCU` and friends).

## Chip side: usb_port_stm32u5.c

Everything TinyUSB doesn't do itself. CubeMX must **not** generate the USB init
(`MX_USB_*_Init`) or a USB interrupt handler; this file replaces both.

- **USB block:** the chip header picks it.
  - U535/U545 (the Nucleo): `USB_DRD_FS`, TinyUSB driver `stm32_fsdev`.
  - U595/U5A5 (the final board): `USB_OTG_HS` with built-in HS PHY, driver `dwc2`.
    The PHY reference clock must match the HSE crystal: `USB_HS_PHY_REFCLK`,
    default 16 MHz.
- **Init:** D−/D+ on PA11/PA12, the USB supply domain (`HAL_PWREx_EnableVddUSB`),
  the USB clock (and PHY on the U595). "Dead battery" Rd is turned off on chips
  with UCPD.
- **IRQ handler:** `USB_IRQHandler` / `OTG_HS_IRQHandler` calls
  `tusb_int_handler(0, true)`.
- **VBUS switch (TPS2051C).** Give the pins these CubeMX labels:
  - `USB_VBUS_EN`: output, starts low;
  - `USB_VBUS_FLT`: input with pull-up; low means overcurrent.

  Without the labels (as on the Nucleo), VBUS isn't ours to switch. The VBUS
  state machine in `usb_port_poll()`:

  ```
  OFF ──(device attached, 150 ms debounce)──► ON ──(detached, 15 ms)──► OFF
                                              │
                                     /FLT low (overcurrent)
                                              ▼
                                            FAULT ──(1 s)──► OFF
  ```

- **Attach detection** (`USB_USE_UCPD=1`, U595 only): UCPD1 on PA15/PB15 shows
  Rp on both CC lines. A device (or an A-to-C adapter) shows Rd on one of them,
  and that's an attach. Without it, VBUS turns on at start and stays on.

## Chip side: usb_port_stm32n6.c (Nucleo-N657X0-Q)

Same job, for the N6 test board:
- It runs in the **secure FSBL**. With the RIF HAL module, USB1 is set to secure
  and privileged.
- It waits for VDD33USB to come up, then runs the PHY from **HSE / 2 = 24 MHz**
  (FSEL = 010), using TinyUSB's reset sequence. CubeMX doesn't set this clock up
  right on the N6.
- **VBUS** goes through the board's TCPP03-M20, controlled over I2C2 (`USB_TCPP_I2C`).
  It's turned on at start; INT low means a fault, which turns VBUS off for 1 s.
  There's no attach detection.

## Host backend: usb_backend_sdl.c

- **Keyboard:** SDL3 scancodes *are* HID usage IDs, so each key event updates a
  fake 8-byte boot report, which goes to `usb_input.c` on every change, just like
  a real keyboard. Key repeat is ignored, because real keyboards don't repeat.
- **Mouse:**
  - The first click in the window **grabs** the mouse (relative mode). That click
    isn't passed on, and **F12** lets go.
  - SDL3 gives movement as floats. The whole part is sent and the fraction kept
    for next time, so slow movement isn't lost. Big jumps are split into several
    reports, like a fast real mouse.
- **Losing focus** (alt-tab): SDL won't deliver key-ups any more, so every key and
  button is released.
- **Sharing SDL's event queue with `display_host.c`:** this backend takes only
  the keyboard and mouse event ranges (`SDL_PeepEvents`). The display empties
  everything else and leaves those ranges alone, so it doesn't matter which polls
  first.
- The keyboard and mouse count as connected from the start.

## Moving to another chip

1. Write `usb_port_<chip>.c` with `usb_port_init()` (clocks, power, pins, PHY),
   `usb_port_poll()` (VBUS) and the USB interrupt handler that calls
   `tusb_int_handler()`.
2. Set `tusb_config.h` for the chip (`CFG_TUSB_MCU`, which port is host) and add
   TinyUSB's driver for that USB block to the build.
3. Nothing else changes: `usb_input.c`, the TinyUSB backend and the game stay
   the same.
