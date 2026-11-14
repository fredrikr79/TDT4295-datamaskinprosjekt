# USB input: keyboard, mouse and controllers

[← back to the overview](README.md)

The game reads keys, the mouse and game controllers through one small API. On
the MCU the input comes from USB devices through TinyUSB; on the PC it comes
from the SDL window and any controller SDL knows. The game can't tell which.

```
game.c, menu.c, draw.c ── usb_input.h       input_take(&in); key_held(&in, KEY_W); in.pad[0] ...
                               │
                          usb_input.c       translation: reports -> held / pressed / released
                               │
                          usb_backend.h     the contract between the two halves
                    ┌──────────┴───────────┐
       usb_backend_tinyusb.c          usb_backend_sdl.c
       (+ hid_mouse.c: wheel)         (fake boot reports from SDL events,
                    │                  controllers read as SDL gamepads)
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
if (key_down(&in, KEY_SPACE))     ...   /* held now, or tapped since the last take */
if (mouse_pressed(&in, MOUSE_LEFT)) ...
cursor_x += in.mouse_dx;              /* movement since last take   */

if (pad_pressed(&in, 0, PAD_SOUTH)) ...        /* cross / A on the first controller */
int sx = pad_deadzone(in.pad[0].lx, 16);       /* left stick, -127..127 */
```

- **`held`** is the state right now. **`pressed`** and **`released`** collect
  everything since the previous `input_take()`. A tap shorter than one frame
  still shows as pressed and released, even though it's no longer held.
- **Mouse movement** (`mouse_dx/dy`, `wheel`, `pan`) adds up between takes. +y is
  down, +wheel is scrolling away from you, +pan is right. It's in **screen
  pixels** (640 × 360); drawing turns the cursor into a grid cell with
  `fpga_px_to_cell()` ([fpga.md](fpga.md)).
- **`in.keyboard` / `in.mouse`** tell you whether a keyboard or mouse is
  connected; **`in.pad[p].connected`** the same per controller.
- **Key codes are USB HID usage IDs** (keyboard page 0x07). They name physical
  key positions with US labels: on a Norwegian keyboard Å is `KEY_LBRACKET`,
  Ø is `KEY_SEMICOLON` and Æ is `KEY_APOSTROPHE`. Modifiers are keys too:
  `key_held(&in, KEY_LSHIFT)`.
- **Mouse buttons:** `MOUSE_LEFT`, `MOUSE_RIGHT`, `MOUSE_MIDDLE`, `MOUSE_BACK`,
  `MOUSE_FORWARD`.
- **Debugging:** `input_log(&in)` logs what changed (keys, buttons, wheel,
  controller buttons and fingers, connects; mouse movement and sticks at most
  10 times a second), and `key_name(k)` gives `"W"`, `"SPACE"`, `"LSHIFT"`.
  There's a commented-out call in `game.c`'s `run_poll()`.

Call `input_take()` once per frame and pass the `input_t` around. A second call
in the same frame finds `pressed`/`released` already cleared.

## Controllers

Up to two at once (`INPUT_PADS`), as `in.pad[0]` and `in.pad[1]`. Every
controller gets the same layout, named after a PlayStation pad:

```c
typedef struct {
    bool        connected;
    uint32_t    held, pressed, released;   /* PAD_* bits, like the keys      */
    int8_t      lx, ly, rx, ry;            /* sticks -128..127, 0 = centre, +y = down */
    uint8_t     lt, rt;                    /* triggers, 0 released .. 255 full */
    pad_touch_t touch[2];                  /* touchpad, up to two fingers   */
} pad_t;
```

| Bit | PlayStation | Xbox |
|---|---|---|
| `PAD_SOUTH` / `EAST` / `WEST` / `NORTH` | cross / circle / square / triangle | A / B / X / Y |
| `PAD_L1`, `PAD_R1` | L1, R1 | LB, RB |
| `PAD_L2`, `PAD_R2` | L2, R2 pressed past their click point (`lt`/`rt` have the analog value) | LT, RT |
| `PAD_SELECT`, `PAD_START` | share, options | view, menu |
| `PAD_L3`, `PAD_R3` | sticks pressed in | |
| `PAD_HOME` | PS button | guide |
| `PAD_TOUCH` | touchpad click | |
| `PAD_UP` / `DOWN` / `LEFT` / `RIGHT` | d-pad | |

- **Sticks don't spring back to exactly 0**, so read them through
  `pad_deadzone(v, dz)`: 0 within `dz` of centre, then smoothly up to ±127.
- **The touchpad** (DualShock 4) is `PAD_TOUCH_W × PAD_TOUCH_H` = 1920 × 942
  units, origin top left. `touch[f].down` says whether finger f is on it; its
  last position stays after it lifts.
- `pad_held/pressed/released(&in, p, PAD_...)` check one controller; the game
  mostly checks both with a loop over `INPUT_PADS`.

**What the game does with them** (today):

| Where | Controller |
|---|---|
| Playing | the PS button opens the menu. The player itself is still keyboard only (A, D, Space) |
| Cursor | the right stick moves it (12 px a frame at full tilt, dead zone 16), and a finger on the touchpad drags it (a swipe across the whole pad crosses the whole screen) |
| Menu (`menu.c`) | d-pad or left stick up/down to move (held repeats), cross to choose, circle or PS to close |
| Drawing (`draw.h`) | cross or R2 places, circle or L2 erases, L1/R1 or d-pad left/right change material, d-pad up/down the brush size, square pauses / runs, PS leaves |

## The translation layer: usb_input.c

Keyboards and mice hand over **HID boot protocol reports**, the fixed format
every keyboard and mouse supports. Controllers hand over a ready-made state
(`usb_gamepad_t`), since every controller sends something different:

| Device | Backend calls | Format |
|---|---|---|
| keyboard | `usb_input_keyboard_report()` | 8 bytes: `{ modifiers, reserved, key[6] }`, so up to 6 keys at once plus the 8 modifier bits |
| mouse, boot protocol | `usb_input_mouse_report()` | 3–5 bytes: `{ buttons, dx, dy, [wheel], [pan] }`, signed 8-bit |
| mouse, report protocol | `usb_input_mouse_motion()` | buttons, dx, dy, wheel, pan, already decoded (full size) |
| controller | `usb_input_gamepad(p, &state)` | `usb_gamepad_t`: PAD_* buttons, sticks, triggers, touch |

`usb_input.c` keeps two sets of state:
- `cur`: what the devices say right now (held keys, buttons, pad states, how
  many keyboards and mice are connected);
- `acc`: what has been collected since the last `input_take()` (pressed and
  released bits, movement sums).

**Keyboard report:**
1. Modifier bit n becomes key `0xE0 + n`, and each nonzero key slot sets its key.
2. Comparing with `cur.held` gives the newly pressed keys (now and not before)
   and the released ones (before and not now). They're ORed into `acc`.
3. Slots holding 0x01–0x03 mean "too many keys" (rollover). Then the report is
   ignored and the last state kept.

**Mouse:** button changes work the same way, and movement is added to `acc`.

**Controller:** the same for `buttons`; sticks and triggers are just copied.
`touch_valid = false` means "this report has no touch data": the fingers keep
their last state.

**Unplug:** `usb_input_connected(kind, false)` releases every key or button of
that kind, and `usb_input_gamepad_connected(p, false)` every button of that
controller, so nothing stays stuck down.

`input_take()` copies `cur` and `acc` into your `input_t`, then clears `acc`.

Everything runs in the main loop: TinyUSB calls its callbacks from inside
`tuh_task()`, which `usb_poll()` calls. So no locking is needed. All keyboards
share one key state; one keyboard plus one mouse is the expected setup.

## MCU backend: usb_backend_tinyusb.c

**Start and poll:**
- `usb_backend_init()`: `usb_port_init()` (the chip side), every device in boot
  protocol by default (`tuh_hid_set_default_protocol`), then `tusb_init()` as a
  host.
- `usb_backend_poll()`, every loop: `usb_port_poll()` (VBUS), `tuh_task()` (the
  callbacks below run in here), then the control requests, the watchdog, the
  report safety net and the hub log, in that order.

**A device appears** (`tuh_hid_mount_cb`, per HID interface):

| It is | Recognised by | Then |
|---|---|---|
| a controller | VID:PID: Sony DualShock 4 (v1, v2, USB dongle), Hori FC4, Hori PS4 Mini | gets the lowest free pad number; start-up sequence below |
| a keyboard | interface protocol | stays in boot protocol; Set Idle (below) |
| a mouse | interface protocol | if `hid_mouse_parse()` understands its report descriptor: switched to report protocol, for the wheel |
| anything else | | logged and ignored |

Up to `MAX_HID` = 8 interfaces are tracked (TinyUSB itself takes
`CFG_TUH_HID` = 4). Control requests (Set Idle, Set Protocol, the controller's
start-up) go out one at a time from `hid_control_poll()` in the main loop, not
from the mount callback, because during enumeration the control pipe may still
be busy.

**Mice and the wheel:** the boot report has no wheel. A mouse whose report
layout `hid_mouse.c` can read is asked for report protocol; until it confirms,
its reports are still read as boot reports. After that, `hid_mouse_decode()`
pulls buttons, X, Y, wheel and pan out of each report. A mouse that refuses
stays in boot protocol, without a wheel.

**Controllers (DualShock 4):**
- It only starts sending once the host has read its report descriptor (507
  bytes), as every PC does, so `CFG_TUH_ENUMERATION_BUFSIZE` is 512.
- Start-up, as Linux's driver does it: read the pairing info (0x81), firmware
  info (0xa3) and calibration (0x02) feature reports, then set the light bar to
  the pad number (**blue = pad 0, red = pad 1**). A refused step is logged and
  skipped.
- Report 0x01, every few ms: sticks, d-pad hat, buttons, triggers and the
  newest touchpad record go into a `usb_gamepad_t`. Gyro and battery are
  ignored.

**Keeping reports coming.** Each report request gives one report, so the next
one is asked for in the callback. Three things make sure that chain never
breaks:
- **Keyboard watchdog:** keyboards are asked (Set Idle) to repeat their report
  every 500 ms. A keyboard silent for 1.5 s with a request outstanding (and a
  controller, which reports all the time anyway) has lost its transfer: it's
  aborted and asked again, logged, and the first two times `usb_debug_dump()`
  logs the state. `-DUSB_KBD_WATCHDOG=0` only logs it, to look at the stuck
  state in the debugger.
- **`tuh_hid_report_failed_cb`:** a failed transfer (a glitch on a low-speed
  device behind a hub) isn't a report, so the next one is asked for here.
- **`hid_keep_polling()`:** any device with no request outstanding gets one,
  at most every 100 ms, logged.

**Hubs** work (`CFG_TUH_HUB`, up to 4 devices). TinyUSB doesn't call
`tuh_mount_cb` for hubs, so `hubs_poll()` logs them when they come and go,
with their speed and whether they're single- or multi-TT.

**Two TinyUSB fixes, without changing `third_party/`.** Both wrap a TinyUSB
function at link time; `stm32u5.cmake` (and `n6_usbtest.cmake`) add the
`-Wl,--wrap=...` options, and the build fails to link if they're missing:

| Wrapped | Problem | Fix |
|---|---|---|
| `hcd_setup_send` | TinyUSB's Get Hub Descriptor has wValue = 0; strict hubs (StarTech 5G2A2CPDB) stall it | that one setup packet gets the descriptor type (0x29) put in |
| `hcd_edpt_xfer` | dwc2: the USB interrupt can land between starting a periodic transfer and marking it "not pending", and the device goes silent until replugged (seen with two devices behind a hub) | runs with interrupts masked for those few µs |

**Debugging:** `usb_debug_dump()` logs every device (reports so far, time
since the last, restarts, whether a request is outstanding), then the
controller's registers through `usb_port_dump()` (U595 only, from
`usb_dwc2_debug.c`). TinyUSB's own log is `CFG_TUSB_DEBUG` in `tusb_config.h`,
off by default: logging over the blocking UART slows TinyUSB down enough to
break enumeration.

**`tusb_config.h`:** TinyUSB master as of Oct 2026, no RTOS
(`tusb_time_millis_api()` returns `plat_millis()`). Full speed by default (the
U545); the U595 build sets `CFG_TUH_MAX_SPEED=OPT_MODE_HIGH_SPEED`, which its
built-in HS PHY needs. Low- and full-speed keyboards and mice still work on the
high-speed port. dwc2 runs without DMA (the CPU copies the FIFOs).

## Chip side: usb_port_stm32u5.c

Everything TinyUSB doesn't do itself. CubeMX must **not** generate the USB init
(`MX_USB_*_Init`) or a USB interrupt handler; this file replaces both.

- **USB block:** the chip header picks it.
  - U535/U545 (the Nucleo): `USB_DRD_FS`, TinyUSB driver `stm32_fsdev`.
  - U595/U5A5 (the final board): `USB_OTG_HS` with built-in HS PHY, driver `dwc2`.
- **Init:** D−/D+ on PA11/PA12, the USB supply domain (`HAL_PWREx_EnableVddUSB`)
  and the USB clock. On the U595 also:
  - the PHY clock, straight from the HSE crystal (started here if CubeMX
    didn't). The PHY must be told the frequency; it's picked from `HSE_VALUE`
    (16, 19.2, 20, 24, 26 or 32 MHz, or twice those through HSE/2). A wrong one
    and no device is ever seen, with no other error. `USB_HS_PHY_REFCLK`
    overrides it;
  - the HS transceiver supply, which needs voltage range 1 or 2 (CubeMX: Power).
  - "Dead battery" Rd is turned off on chips with UCPD.
- **IRQ handler:** `USB_IRQHandler` / `OTG_HS_IRQHandler` calls
  `tusb_int_handler(0, true)` (on the U595 after counting the event for the
  debug dump).
- **VBUS switch (TPS2051C).** Give the pins these CubeMX labels (the
  schematic's `Drive_VBUS` / `fault_signal` work too):
  - `USB_VBUS_EN`: output, starts low;
  - `USB_VBUS_FLT`: input; low means overcurrent. `usb_port_init()` sets both
    up itself, /FLT with the internal pull-up (R60 isn't fitted on our board).

  Without the labels (as on the Nucleo), VBUS isn't ours to switch. The VBUS
  state machine in `usb_port_poll()`:

  ```
  OFF ──(device attached, 150 ms debounce)──► ON ──(detached, 15 ms)──► OFF
                                              │
                                     /FLT low (overcurrent)
                                              ▼
                                            FAULT ──(1 s)──► OFF
  ```

- **Attach detection** (`USB_USE_UCPD=1`, U595 only; `stm32u5.cmake` sets it
  from `HEXMCU_USB_USE_UCPD`, default 1): UCPD1 on PA15/PB15 shows Rp on both
  CC lines. A device (or an A-to-C adapter) shows Rd on one of them, and that's
  an attach. Without it, VBUS turns on at start and stays on.

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
    isn't passed on, and **F12** lets go. If relative mode isn't available,
    clicks go straight through without a grab.
  - It's handed over like a report-protocol mouse: full-size movement, wheel,
    horizontal wheel (pan), back and forward buttons. SDL3 gives movement and
    wheel as floats; the whole part is sent and the fraction kept for next
    time, so slow movement isn't lost.
- **Controllers:** any controller SDL knows (DualShock, DualSense, Xbox, Switch
  Pro, ...), as pad 0 and 1 in plug-in order, with the same PAD_* layout. SDL
  looks for new ones every 500 ms. Their state is read every poll rather than
  through events. SDL has no L2/R2 buttons, so a trigger past 32 of 255 sets
  `PAD_L2`/`PAD_R2`; the touchpad is scaled to the DualShock 4's 1920 × 942.
- **Losing focus** (alt-tab): SDL won't deliver key-ups any more, so every key,
  button and controller input is released, and the mouse let go.
- **Sharing SDL's event queue with `display_host.c`:** this backend takes only
  the keyboard and mouse event ranges (`SDL_PeepEvents`). The display empties
  everything else and leaves those ranges alone, so it doesn't matter which polls
  first. F4 or closing the window quits (that's the display's).
- The keyboard and mouse count as connected from the start.

## Moving to another chip

1. Write `usb_port_<chip>.c` with `usb_port_init()` (clocks, power, pins, PHY),
   `usb_port_poll()` (VBUS) and the USB interrupt handler that calls
   `tusb_int_handler()`. `usb_port_dump()` is optional.
2. Set `tusb_config.h` for the chip (`CFG_TUSB_MCU`, speed) and add TinyUSB's
   driver for that USB block to the build, with the two `--wrap` options.
3. Nothing else changes: `usb_input.c`, the TinyUSB backend and the game stay
   the same.

## Adding a controller

A controller that copies the DualShock 4's USB report only needs its VID:PID
in `is_ds4()`. Anything else needs its own report decoder next to
`ds4_report()`, filling the same `usb_gamepad_t`; the rest of the code doesn't
change.