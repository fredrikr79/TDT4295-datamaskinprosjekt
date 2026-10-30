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
[`../mcu_fpga_spi_api.md`](../mcu_fpga_spi_api.md).

| Command | Op | Parameters | Payload | Answer | Builder |
|---|---|---|---|---|---|
| ECHO | 10 | — | B1..BN | B1..BN | `fpga_cmd_echo(tx, rx, len)` |
| INFO | 20 | — | — | W H N SID | `fpga_cmd_info()` |
| WRITE_CONS | 30 | X Y N | N bytes | — | `fpga_cmd_write_cons(x, y, data, n)` |
| READ_CONS | 40 | X Y N | — | N bytes | `fpga_cmd_read_cons(x, y, buf, n)` |
| WRITE_BOX | 50 | X Y W H | pixels | — | `fpga_cmd_write_box(fpga_box(x,y,w,h), px, len)` |
| READ_BOX | 60 | X Y W H | — | pixels | `fpga_cmd_read_box(box, buf, len)` |
| WRITE_SPRITE | 70 | — | pixels | memID (2 B) | `fpga_cmd_write_sprite(px, len)` |
| SPRITE_WRITE | 80 | memID, active, X Y W H | — | spriteID (2 B) | `fpga_cmd_sprite_write(...)` |
| SPRITE_UPDATE | 90 | spriteID, active, X Y | — | — | `fpga_cmd_sprite_update(...)` |
| SPRITE_DELETE | A0 | spriteID | — | — | `fpga_cmd_sprite_delete(id)` |
| START_SIM / STOP_SIM | B0 / C0 | — | — | — | `fpga_cmd_sim_run(bool)` |

- **CONS and BOX** work on the grid (the SIM layer). CONS is contiguous memory:
  N bytes from (X, Y) on, row after row. A box's pixels are one stream, row by
  row; X,Y is the top-left corner, W the pixels per row and H the number of rows.
- **Sprites** are the HUD layer. See [sprite.md](sprite.md) for how they're
  managed.
- **INFO** answers with the screen size (W, H), N (the most bytes one command
  may be, header included) and SID, the session ID. A new SID means the FPGA
  restarted and everything in its memory is gone.

## Using it

```c
fpga_cmd_t c = fpga_cmd_read_box(fpga_box(10, 20, 32, 8), buf, 32 * 8 * 2);
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
WRITE ──► WAIT_IRQ ──► READ          (READ only if the command has an answer)
```

| Step | What happens | Ends when | Timeout |
|---|---|---|---|
| **WRITE** | `BACKEND->write()`: opcode, header fields and payload, by DMA straight from `tx` | the backend says done | 100 ms |
| **WAIT_IRQ** | wait for a READY edge from the FPGA, then look at the ACK line latched with it | the READY count has gone up | 100 ms |
| **READ** | `BACKEND->read()` with opcode `0x00` (a dummy the FPGA ignores), 4 turnaround cycles, then `rx_len` bytes into `rx` | the backend says done | 100 ms |

Details that matter:

- **The READY count is noted before the transfer starts** (`irq_mark`). A fast
  FPGA can pulse READY while the WRITE's DMA is still finishing; that pulse
  still counts for this command.
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
| `BAD_INFO` | the INFO answer makes no sense (zero size, or N below 9) |
| `ABORTED` / `CANCELLED` | stopped by `fpga_abort_all()` / never ran |

### How the game recovers (game.c)

```
PROBE ──► INFO ──► same session? ──yes──► go back to what was interrupted (LOAD / SAVE / RUN)
  ▲                     │
  │                     no ─► sprite_forget(), load the room again
  └── any command fails: fpga_abort_all(), sprite_lost(), back to PROBE
```

- **PROBE** sends a 100-byte ECHO with a new pattern each try, every 200 ms,
  until one comes back right. That proves the link works both ways.
- **INFO** then gives the session ID. If it's the same as before, the FPGA's
  memory survived and the game picks up where it was. If not, everything is sent
  again.

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
| WRITE_SPRITE, ECHO, others | 1 | 1023 |

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
counts READY edges taken by commands. `fpga_set_link_info()` lets the console or
tests set W/H/N by hand. The console (`cli.c`) can send any command by hand,
including the `sprite` and `sim` commands.
