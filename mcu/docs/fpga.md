# fpga.c: the FPGA command queue

[← back to the overview](README.md)

`fpga.c` turns "draw this box" or "upload this sprite" into the right sequence
of transfers on the link, one command at a time, without ever blocking. You
build a command, queue it, and get a callback when it's done.

Files: `fpga.h` (the API and the protocol table), `fpga.c`. Below it is
[transport](transport.md); above it are `sprite.c`, `rooms.c`, `game.c` and the
console.

---

## The protocol in one table

Every command is one byte stream: the opcode, the parameters in spec order, then
any payload. Multi-byte fields go MSB first. The full spec is
[`../mcu_fpga_spi_api.md`](../../documentation/SPI/mcu_fpga_spi_api.md).

| Command | Op | Parameters | Payload | Answer | Builder |
|---|---|---|---|---|---|
| ECHO | 10 | — | B1..BN | B1..BN | `fpga_cmd_echo(tx, rx, len)` |
| INFO | 20 | — | — | W H N SID | `fpga_cmd_info()` |
| WRITE_CONS | 30 | X Y N | N bytes | — | `fpga_cmd_write_cons(x, y, data, n)` |
| READ_CONS | 40 | X Y N | — | N bytes | `fpga_cmd_read_cons(x, y, buf, n)` |
| WRITE_BOX | 50 | X Y W H | IDs | — | `fpga_cmd_write_box(fpga_box(x,y,w,h), ids, len)` |
| READ_BOX | 60 | X Y W H | — | IDs | `fpga_cmd_read_box(box, buf, len)` |
| WRITE_SPRITE | 70 | LEN (2 B, payload size) | pixels | memID (2 B) | `fpga_cmd_write_sprite(px, len)` |
| SPRITE_WRITE | 80 | memID, active, X Y W H | — | spriteID (2 B) | `fpga_cmd_sprite_write(...)` |
| SPRITE_UPDATE | 90 | spriteID, active, X Y | — | — | `fpga_cmd_sprite_update(...)` |
| SPRITE_DELETE | A0 | spriteID | — | — | `fpga_cmd_sprite_delete(id)` |
| START_SIM / STOP_SIM | B0 / C0 | — | — | — | `fpga_cmd_sim_run(bool)` |

- **The grid and the screen are different sizes.** The grid (the SIM layer)
  is **160 × 90 cells**; the FPGA shows each cell as a 4 × 4 block
  (`FPGA_SIM_SCALE`) on a **640 × 360 pixel** screen. So:
  - CONS and BOX X, Y, W, H count **cells**;
  - sprites (SPRITE_WRITE / UPDATE) and the mouse cursor count **screen pixels**;
  - `fpga_px_to_cell(px)` and `fpga_cell_to_px(cell)` convert (rounding down,
    also for negative pixels);
  - `fpga_link_info()` has both: `width` / `height` (pixels) and
    `sim_w` / `sim_h` (cells).

  Drawing a 1 × 1 brush at the cursor is one cell: one byte of payload, shown
  as 4 × 4 pixels.
- **CONS and BOX** work on the grid, which holds **a byte per
  cell with the material ID in its high 4 bits**. The low 4 bits are the
  FPGA's own (simulation state): write them as 0 and ignore them when reading
  back. The FPGA turns materials into colours with its own lookup table.
  - The materials (`material_t` in fpga.h, the FPGA's `materials_e`):
    `MAT_AIR` 0, `MAT_STONE` 1, `MAT_SAND` 2, `MAT_WATER` 3, `MAT_LAVA` 4,
    `MAT_ACID` 5, `MAT_LIFE` 6 (game of life), `MAT_EXPLOSIVE` 7.
  - `SIM_CELL(MAT_SAND)` is the byte for a cell (0x20); `SIM_MAT(byte)` the
    material in one. `sim_put(buf, k, mat)` / `sim_get(buf, k)` fill and read
    cell k of a buffer, whichever packing is in use.
  - CONS is contiguous: N bytes from cell (X, Y) on, row after row.
  - A box's cells are one stream, row by row: X,Y is the top-left cell, W the
    cells per row and H the number of rows.
  - Whether a byte carries one cell or two materials isn't agreed yet:
    `FPGA_SIM_IDS_PER_BYTE` (1 or 2) switches it, and `fpga_sim_bytes(cells)`
    gives the byte count.
- **Only the HUD layer (sprites) is RGB565 pixels.**
- **Sprites** are the HUD layer. See [sprite.md](sprite.md) for how they're
  managed.
- **INFO** answers with the screen size in pixels (W, H: 640 × 360), N (the
  most bytes one command may be, header included) and SID, the session ID. The
  grid size is worked out from W, H (÷ `FPGA_SIM_SCALE`). A new SID means the
  FPGA restarted and everything in its memory is gone.

## Using it

```c
fpga_cmd_t c = fpga_cmd_read_box(fpga_box(10, 20, 32, 8), buf,   /* cells */
                                  fpga_sim_bytes(32 * 8));           /* 256 bytes */
c.done = my_done;          /* optional */
c.ctx  = my_thing;         /* optional, handed back to my_done */
if (fpga_submit(&c) == FPGA_EFULL) { /* try again later */ }

void my_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    if (err != FPGA_ERR_NONE) { /* failed: resync */ return; }
    /* buf now holds the answer; for sprite commands: fpga_answer_id(cmd) */
}
```

`fpga_submit()` returns at once:

| Result | Meaning |
|---|---|
| `FPGA_OK` | Queued. It will run, then call `done`. |
| `FPGA_EFULL` | The queue (10 slots, `FPGA_QUEUE_LEN`) is full. Submit the same command again later; `fpga_has_space()` tells you when. |
| `FPGA_EINVAL` | Bad arguments (a NULL buffer, zero length…). Nothing queued. |
| `FPGA_ETOOBIG` | Bigger than the FPGA's N. Nothing queued. Split writes using `fpga_max_payload()`. |
| `FPGA_EBUSY` | An INFO is already queued; they share one answer buffer. |

`fpga_poll()` must be called from the main loop as often as possible. That's
where commands advance and callbacks run.

**Buffer rule:** `tx` and `rx` belong to you and are used directly by DMA. Keep
them valid (and `tx` unchanged) until `done` is called. The `fpga_cmd_t` itself
is copied into the queue, so it can live on the stack. The sprite commands even
carry their parameters (`param[]`) and their 2-byte answer (`answer[]`) inside
the struct. `fpga_submit()` points `tx`/`rx` at the queue's copy, so you don't
need a buffer for them at all.

## How a command runs

`fpga_submit()` copies the command into a ring buffer slot. `fpga_poll()` takes
the command at the head and runs it as a short list of steps:

```
WRITE ──► WAIT_IRQ ──► READ ──► READ_READY     (the last two only if the command has an answer)
```

| Step | What happens | Ends when | Timeout |
|---|---|---|---|
| **WRITE** | `BACKEND->write()`: opcode, header fields and payload, by DMA straight from `tx` | the backend says done | 100 ms |
| **WAIT_IRQ** | wait for a READY edge from the FPGA, then look at the ACK line latched with it | the READY count has gone up **and** the line is high now | 100 ms |
| **READ** | `BACKEND->read()` with opcode `0x00` (a dummy the FPGA ignores), 4 turnaround cycles, then `rx_len` bytes into `rx` | the backend says done | 100 ms |
| **READ_READY** | the READY the FPGA gives after the READ, used up so it isn't taken as stray or by the next command (`FPGA_READY_AFTER_READ`, on by default) | as WAIT_IRQ | 100 ms |

The console's raw `read` is one bare READ step.

Details that matter:

- **No transfer starts while READY is low** (the FPGA busy, booting or being
  reset): it could pick the command up halfway and take a byte in the middle as
  the opcode. If READY stays low for 100 ms the command fails with
  `NOT_READY` and nothing is sent.
- **The READY count is noted before the transfer starts** (`irq_mark`). A fast
  FPGA can pulse READY while the WRITE's DMA is still finishing; that pulse
  still counts for this command.
- **An edge alone isn't enough:** the line must also be high. A glitch while
  the FPGA is busy (an edge, but the line back low) isn't taken as done.
- **The ACK line** is read in the READY interrupt, when the edge happens
  ([transport.md](transport.md)). High means the command worked; low is a NACK.
- **On a NACK**, the FPGA has emptied its FIFOs and put a one-byte error code in
  its answer FIFO. The rest of the command is replaced by a 1-byte READ of that
  code, and the command fails with `FPGA_ERR_STATUS`.
  `fpga_fail_code()` / `fpga_fail_str()` give the code: 0xE6 "unknown
  identifier", 0xE9 "resource exhausted" and so on.
- **When all steps are done**, ECHO compares `rx` with `tx` (`FPGA_ERR_MISMATCH`
  if they differ), and INFO parses its answer into `fpga_link_info()`. Then the
  command is removed from the queue and `done` is called.

## When something fails

A failure is anything but `FPGA_ERR_NONE`: a timeout, a transfer error, a NACK.
After a failure the MCU no longer knows what state the FPGA is in, and later
commands may depend on the failed one. So:

1. The failed command's `done` gets its error.
2. **Every command queued behind it** gets `FPGA_ERR_CANCELLED`, which means
   "never ran". Commands that those callbacks submit while this happens are kept.
3. The owner (the game) resyncs.

`fpga_abort_all()` does the same on purpose: the running command gets
`FPGA_ERR_ABORTED` (its transfer is killed) and the rest get `CANCELLED`. All
callbacks have run by the time it returns, which is why `sprite_lost()` is
called right after it.

| Error | Meaning |
|---|---|
| `LINK_BUSY` | the backend never became free (100 ms) |
| `START` | the backend refused to start a transfer |
| `XFER` / `XFER_TIMEOUT` | the transfer failed, or never finished |
| `NO_READY` | no READY from the FPGA within 100 ms (not there? not programmed?) |
| `STATUS` | the FPGA answered NACK; see `fpga_fail_code()` |
| `MISMATCH` | an ECHO came back different |
| `BAD_INFO` | the INFO answer makes no sense: a size under one cell (4 px) or over 2048, or N outside 9–8192. The raw bytes are logged (at most every 2 s) and the handshake tries again |
| `ABORTED` / `CANCELLED` | stopped by `fpga_abort_all()` / never ran |
| `NOT_READY` | READY stayed low for 100 ms, so nothing was sent |

### How the game recovers (game.c)

```
PROBE ──► INFO ──► same session? ──yes──► go back to what was interrupted (LOAD / SAVE / RUN)
  ▲                     │
  │                     no ─► sprite_forget(), load the room again
  └── any command fails: fpga_abort_all(), sprite_lost(), back to PROBE
```

- **PROBE** (`probe.c`) first waits for the FPGA's DONE pin (design loaded)
  and for READY to be high with no stray edges for 10 ms. Then it sends a
  100-byte ECHO with a new pattern each try, every 200 ms, until one comes back
  right. That proves the link works both ways.
  While it keeps failing, the console shows one line that updates in place in
  front of the prompt (`probing FPGA: x7 (no FPGA ready irq (timeout)) > `,
  `cli_status()`), and the log gets one line when it finally answers.
- **INFO** then gives the session ID. If it's the same as before, the FPGA's
  memory survived and the game picks up where it was. If not, everything is sent
  again.
- **`loading off`** on the console stops all of this (the game holds and sends
  nothing), so the FPGA can be tested by hand; `loading on` does the handshake
  again and carries on.

## N and payload sizes

Until INFO has succeeded, N isn't known: `fpga_max_payload()` returns
`UINT16_MAX` and nothing is size-checked. After INFO, each write is checked at
submit time. Its wire size is 1 (opcode) + 4 (X,Y if present) + 2 or 4 (the
N or W,H field) + payload, and it must be ≤ N, or you get `FPGA_ETOOBIG`.

`fpga_max_payload(opcode)` tells you the most payload one command can carry:

| Opcode | Header bytes | Max payload (N = 1024) |
|---|---|---|
| WRITE_CONS / READ_CONS | 7 | 1017 |
| WRITE_BOX / READ_BOX | 9 | 1015 |
| WRITE_SPRITE | 3 (opcode + LEN) | 1021 |
| ECHO, others | 1 | 1023 |

A whole grid row (160 cells) fits in one WRITE_CONS; the biggest square box is
31 × 31 cells.

## Callback rules

- Callbacks may call `fpga_submit()` and `fpga_abort_all()`; an abort from inside
  a callback is held over and done right after. They must not call `fpga_poll()`;
  that call is ignored.
- `fpga_poll()` does at most 40 steps per call (`4 × FPGA_QUEUE_LEN`). That way a
  callback that keeps resubmitting a command that fails instantly can't freeze
  the main loop.

## Debug helpers

`fpga_current()`, `fpga_current_step()` and `fpga_current_ms()` show the running
command, its step and how long it has been in that step. `fpga_ready_used()`
counts READY edges taken by commands. `fpga_last_failure()` describes the last
command that failed, in words (the resync log line uses it).
`fpga_set_link_info()` lets the console set W/H/N by hand (`info set`): W and H
in screen pixels, and the grid follows. The console (`cli.c`) can send any command by hand,
including the `sprite` and `sim` commands.

## Demos (demo.c)

`demo <test>` on the console runs a test against the FPGA without the SD
card; `demo` alone lists them. Each is a list of steps, one command in flight
at a time, with every answer checked against what was sent:

| Test | What it does |
|---|---|
| `echo` | ECHO of 1, 2, 16, 100, 512 and N−1 bytes, different bit patterns; a mismatch says which IO lines were wrong |
| `info` | INFO: the screen, the grid it gives, and the payload limits that follow from N |
| `cons` | STOP_SIM; a row written and read back; a write over a row's end (read back whole and the wrapped part alone); one byte; START_SIM |
| `box` | STOP_SIM; boxes from 1×1 to the biggest N allows, written and read back, plus one row of a box read with READ_CONS; START_SIM |
| `sprite` | a 16×16 smiley uploaded, placed, moved, hidden, shown, deleted, then an UPDATE of the deleted one, which must be NACKed |
| `scene` | a little world with every material, sent row by row (90 rows of 160 cells), then START_SIM |
| `clear` | the whole grid air |
| `all` | echo, info, cons, box, sprite |

- Every test but `echo` starts with an INFO, after the queue has emptied.
- `cons`, `box`, `scene` and `clear` work in grid cells (`sim_w` × `sim_h`);
  `sprite` moves its smiley in screen pixels.
- **While a demo runs it has the FPGA to itself:** `game_poll()` calls
  `demo_poll()` instead of the game. The game carries on afterwards.
- Patterns depend on the cell's position and change every run, so a write
  that did nothing can't pass by reading back an earlier run's cells.
- Results go to the log, tag `demo`: `PASS` / `FAILED` per test. A failed
  command stops the test; a wrong answer is counted and the test goes on.
- `sprite` uses up a memID each run (they're never freed).