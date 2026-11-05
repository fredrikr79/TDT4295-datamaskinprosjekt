# Transport: bytes on the wire

[← back to the overview](README.md)

The transport layer moves bytes between the MCU and the FPGA and reports the
FPGA's READY and ACK lines. It doesn't know what a command means; that's
[fpga.c](fpga.md).

| File | Role |
|---|---|
| `transport.h` | the interface: header struct, backend table, READY counter |
| `transport_core.c` | portable part: `transport_init/poll`, READY counter and ACK latch, trace switch |
| `transport_ospi.c` | STM32 backend: OCTOSPI + GPDMA through the HAL |
| `transport_host.c` | host backend: models timing and errors of the wires |
| `fake_fpga.c` | host only: what the FPGA does with the bytes |
| `display_host.c` | host only: the SDL window showing the FPGA's two layers |

---

## The backend table

A backend is a table of functions plus two flags:

```c
typedef struct {
    transport_status_t (*init)(void);
    transport_status_t (*write)(const transport_hdr_t *hdr, const uint8_t *data);
    transport_status_t (*read)(const transport_hdr_t *hdr, uint8_t *buf);
    void    (*abort)(void);
    void    (*poll)(void);          /* NULL on hardware */
    uint8_t (*read_status)(void);   /* the ACK line: 0 = OK, 1 = NACK */
    volatile uint8_t done;          /* 1 = nothing in flight */
    volatile uint8_t error;         /* 1 = the last transfer failed (valid when done) */
} spi_backend_t;

extern spi_backend_t *const BACKEND;   /* defined once, in whichever backend is linked */
```

- **`write` and `read` only start a transfer** and return straight away. Later
  `done` goes to 1, set from an interrupt on hardware. If `error` is also 1, the
  transfer failed.
- **The link is half duplex:** at most one transfer is in flight, so one pair of
  flags is enough.
- **The buffer** (`data` / `buf`) must stay valid until `done` is 1, because DMA
  uses it directly.

## The header

```c
typedef struct {
    uint8_t  opcode;
    uint8_t  fields;     /* TRANSPORT_F_XY | TRANSPORT_F_ALT16 | TRANSPORT_F_ALT32 */
    uint16_t x, y;       /* sent if F_XY (4 bytes)                                  */
    uint32_t alt;        /* sent if F_ALT16 (2 bytes) or F_ALT32 (4 bytes)          */
    uint16_t len;        /* write: payload bytes; read: bytes to clock in           */
} transport_hdr_t;
```

On the wire the order is always: opcode, [x y], [alt], [payload]. How each
command fills it:

| Command | fields | x, y | alt | len (write) |
|---|---|---|---|---|
| WRITE_CONS / READ_CONS | XY + ALT16 | X, Y | N | N / 0 |
| WRITE_BOX / READ_BOX | XY + ALT32 | X, Y | W:H (hi:lo) | material IDs / 0 |
| WRITE_SPRITE, ECHO | — | — | — | payload |
| SPRITE_* | — | — | — | the parameters, as payload |
| INFO, START/STOP_SIM | — | — | — | 0 |
| answer READ | — | — | — | (read) answer bytes, opcode 0x00 |

The FPGA just sees bytes in order. Whether a byte went in the address phase or
the data phase makes no difference to it.

## READY and ACK (transport_core.c)

The FPGA pulses READY when it has finished a command. Its ACK line says whether
the command worked.

```c
void transport_ready_isr(void)          /* called on each READY rising edge */
{
    ready_status = BACKEND->read_status();   /* ACK, latched FIRST...       */
    ready_count++;                            /* ...then the count goes up   */
}
```

- On the board this runs from the FPGA_READY EXTI interrupt. On the host,
  `transport_host.c` calls it at the moment the fake FPGA "pulses" READY.
  Everything above sees the same thing.
- **The main loop never has to catch an edge live.** It only compares
  `transport_ready_count()` with a number it noted earlier. If the count went up,
  READY came, and `transport_ready_status()` is the ACK that went with it.
- The status is stored before the count goes up, so whoever sees the new count
  also sees its status.

## STM32 backend: transport_ospi.c

OCTOSPI1 in indirect mode, 8 lines, with one GPDMA channel. The HAL needs an
instruction phase in every command, and that's the opcode, for reads too.

**The phases:**

| Phase | Carries |
|---|---|
| instruction (8 bits) | the opcode |
| address (32 bits) | X:Y, if `F_XY` |
| alternate bytes (16/32 bits) | `alt`, if `F_ALT16` / `F_ALT32` |
| dummy cycles | read only: 4 turnaround cycles (`TRANSPORT_TURNAROUND_CYCLES`), so the FPGA can take the bus |
| data | the payload (write) or the answer (read), by DMA |

Because the header fits in the command phases, a payload (material IDs, sprite pixels) is sent
by DMA straight from the caller's buffer, with no copying into a header + payload
buffer.

**Write:**
- Header only (`len == 0`): `HAL_OSPI_Command_IT()`, and `HAL_OSPI_CmdCpltCallback`
  sets `done`.
- With payload: `HAL_OSPI_Command()` sets up the registers, then
  `HAL_OSPI_Transmit_DMA()` starts it, and `HAL_OSPI_TxCpltCallback` sets `done`.

**Read:** `HAL_OSPI_Command()` + `HAL_OSPI_Receive_DMA()`, then
`HAL_OSPI_RxCpltCallback` sets `done`.

**Errors:** `HAL_OSPI_ErrorCallback` sets `error`, then `done`, in that order.
If a start fails halfway, `ospi_recover()` aborts the HAL so it isn't stuck
rejecting every later command.

**ACK line:** `ospi_read_status()` reads the `FPGA_ACK` GPIO directly (register
read, no HAL, because it runs in the interrupt). High means OK.

**CubeMX must have:**
- the OCTOSPI1 global interrupt enabled. The HAL waits for the transfer-complete
  interrupt before calling the callbacks, so without it `done` never goes to 1;
- a GPDMA channel linked to `hospi1`, byte width;
- `hospi1.Init.DeviceSize = 32`, so no X:Y address counts as out of range;
- a GPIO input labelled `FPGA_ACK`, with a pull-down so an unprogrammed FPGA
  reads as "failed";
- `USE_HAL_OSPI_REGISTER_CALLBACKS` left at 0. The callbacks here replace the
  HAL's weak ones.

No `LOG_*` calls in these callbacks: on the MCU, logging is a blocking UART send.

## Host backend: transport_host.c

It models only the wires; what the FPGA does is `fake_fpga.c`.

- **Timing:** a transfer takes `bytes × 8 / (clock MHz × lanes)` microseconds,
  plus the turnaround for reads. The defaults are 20 MHz × 8 lanes = 20 MB/s;
  change them with `host_fpga_set_clock()`.
- **Error injection:** `host_fpga_set_error_rate(percent)` makes that share of
  transfers fail. A failed transfer never reaches the fake FPGA and gets no
  READY, just like a garbled one on real hardware. This is how resync was tested.
- **`host_poll()` stands in for the interrupts.** When a transfer's time is up,
  a write goes to `fake_fpga_write()`, a read gets its bytes from
  `fake_fpga_read()`, and `done` is set. When the fake FPGA's READY delay has
  passed, the ACK pins are set and `transport_ready_isr()` is called, the same
  entry point as the EXTI.

### The fake FPGA (fake_fpga.c)

| Thing | How it's modelled |
|---|---|
| Screen | 640 × 360 (`DISPLAY_W/H`), reported by INFO |
| N | 1024 bytes (input FIFO); bytes beyond it are dropped with a warning |
| Grid | the SIM layer: a 4-bit material ID per cell, shown through a stand-in colour table (air black, sand tan, water blue, wall grey, life green, unused IDs bright magenta); CONS and BOX read and write IDs, one or two per byte (`FPGA_SIM_IDS_PER_BYTE`) |
| Sprite memory | 2 MB, append-only; memIDs 1–1023, each WRITE_SPRITE takes one |
| Sprite instances | 1024 (spriteIDs 0–1023); redrawn into the HUD layer every frame in ID order, 0xF81F see-through |
| READY | 200 µs after each command |
| SYNC | 30 times a second, whatever the MCU does |
| NACK | unknown ID, wrong parameter count, out of memIDs…: ACK low, FIFOs emptied, error code in the answer FIFO |
| Session | a random 32-bit ID, new on each `fake_fpga_reset()` |

Choices marked `PROTOCOL` in the source are guesses where the spec isn't settled.

`display_host.c` holds the two layers as plain arrays. At 30 Hz it combines them
(a HUD pixel wins unless it's 0xF81F) and shows the result in the SDL window.
Keyboard and mouse events in that window belong to the
[SDL input backend](usb.md); the display only handles quit.

## Trace

`transport_set_trace(true)` (the console's `trace` command) plus log level 4
logs one line per transfer and per step (`LOG_TRACE`). It's off by default, since
the game makes hundreds of transfers a second.