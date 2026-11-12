/* ======================================================================
 * demo.c - tests and demos on the FPGA, from the console ("demo <test>").
 *
 * Nothing comes from the SD card: every pattern is made here. A demo is a
 * list of steps; each step is one FPGA command (or a pause). One command
 * is in flight at a time, and when it comes back the answer is checked
 * against what was sent. Results go to the log with the tag "demo".
 *
 *   echo    ECHO of 1 byte up to the largest the FPGA takes (N - 1)
 *   info    INFO: screen size, N, session
 *   cons    STOP_SIM, WRITE_CONS a row and read it back with READ_CONS,
 *           a write that runs over the end of a row, one single byte (read
 *           back alone and in a 4-byte read: tells a write fault from a
 *           read fault), START_SIM
 *   box     STOP_SIM, WRITE_BOX / READ_BOX of 1x1 up to the biggest box
 *           that fits in N, plus a READ_CONS through a box (box and cons
 *           must agree where cells are), START_SIM
 *   sprite  WRITE_SPRITE a 16x16 smiley, SPRITE_WRITE it, move it about
 *           with SPRITE_UPDATE, hide/show, SPRITE_DELETE, then an UPDATE
 *           of the deleted one, which the FPGA must NACK
 *   scene   STOP_SIM, a little world with every material, sent row by
 *           row, START_SIM (to look at, nothing to check)
 *   clear   STOP_SIM, the whole grid air, START_SIM
 *   all     echo, info, cons, box, sprite in a row
 *
 * A check that fails also logs the whole command as hex, what was expected
 * above what came back, 16 bytes a line, with ^^ under the bytes that
 * differ (DEMO_DUMP below: 0 never, 1 when a check fails, 2 always).
 *
 * Every demo but echo starts with an INFO (so N and the screen size are
 * known), after the FPGA queue has emptied. While one runs, game_poll() leaves the
 * FPGA to it (demo_busy()).
 *
 * Sizes and positions come from the screen size INFO reports, so the
 * tests work on any screen (tried from 32x24 to 640x360): boxes shrink to
 * fit, the sprite's path and the scene scale, and game of life patterns
 * that don't fit are left out.
 *
 * Patterns depend on where a cell is (x, y) and on a number that changes
 * every run, so a write that silently did nothing can't pass by reading
 * back what an earlier run left there.
 * ====================================================================== */
#define LOG_TAG "demo"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "demo.h"
#include "fpga.h"
#include "platform.h"
#include "log.h"

#define BUF_BYTES        1024u      /* tx / rx: the most one command moves  */
#define IDLE_TIMEOUT_MS  2000u      /* the queue must empty before we start */
#define MAX_DIFFS_SHOWN  4u         /* mismatching cells logged per check   */
#define DUMP_PER_LINE    16u        /* bytes per line of a hex dump         */

/* Hex dumps of expected and received data: 0 never, 1 when a check fails,
 * 2 always (also for checks that pass). */
#ifndef DEMO_DUMP
#define DEMO_DUMP        1
#endif

static uint8_t tx[BUF_BYTES], rx[BUF_BYTES];
static uint8_t want[BUF_BYTES];     /* what a read should give, for the dump */

/* ======================================================================
 * The engine
 * ====================================================================== */
typedef enum {
    STEP_CMD,       /* *c is a command to send                     */
    STEP_WAIT,      /* pause *wait_ms (0: skip this step)          */
    STEP_END,       /* the demo is done                            */
} step_t;

typedef struct {
    const char *name, *help;
    bool        no_info;            /* runs without the INFO first (echo) */
    /* Step i: fill *c (or *wait_ms). Called again with the same i if the
     * queue was full, so it must not keep state of its own between calls. */
    step_t (*step)(unsigned i, fpga_cmd_t *c, uint32_t *wait_ms);
    /* After step i's command came back OK: check the answer. NULL = none. */
    void   (*check)(unsigned i, const fpga_cmd_t *c);
} test_t;

typedef enum {
    PH_IDLE_WAIT,   /* for the queue to empty            */
    PH_INFO,        /* send INFO                         */
    PH_INFO_WAIT,   /* for its answer                    */
    PH_STEP,        /* do step i                         */
    PH_IN_FLIGHT,   /* for step i's command              */
    PH_PAUSE,       /* step i is a pause                 */
} phase_t;

static struct {
    const test_t        *t;         /* running, NULL = none              */
    const test_t *const *seq;       /* "all": the tests, one after another */
    unsigned             seq_n, seq_i, seq_pass;

    phase_t  ph;
    unsigned i;                     /* step                              */
    uint32_t until;                 /* PH_IDLE_WAIT / PH_PAUSE end       */
    uint32_t t0;
    uint32_t gen;                   /* callbacks of a stopped demo are ignored */
    unsigned cmds;                  /* commands that came back OK        */
    unsigned bad;                   /* checks that failed                */
    bool     broken;                /* a command failed: the demo stopped */
    bool     expect_nack;           /* the step in flight must be refused */
    bool     sim_stopped;           /* our STOP_SIM without a START_SIM yet */
    uint8_t  salt;                  /* changes the patterns every run    */
} d;

static uint32_t now(void) { return plat_millis(); }

static const fpga_info_t *scr(void) { return fpga_link_info(); }

/* Most payload bytes / cells one command of this opcode can carry. */
static uint32_t max_bytes(uint8_t opcode)
{
    uint32_t n = fpga_max_payload(opcode);
    return n < BUF_BYTES ? n : BUF_BYTES;
}
static uint32_t max_cells(uint8_t opcode) { return max_bytes(opcode) * FPGA_SIM_IDS_PER_BYTE; }

static void test_begin(const test_t *t)
{
    d.t = t;
    d.ph = PH_IDLE_WAIT;
    d.until = now() + IDLE_TIMEOUT_MS;
    d.i = 0;
    d.cmds = d.bad = 0;
    d.broken = d.expect_nack = false;
    d.salt++;
    d.t0 = now();
    LOG_INFO("%s: start", t->name);
}

static void test_end(void)
{
    const test_t *t = d.t;
    unsigned long ms = (unsigned long)(now() - d.t0);
    bool pass = !d.broken && !d.bad;

    if (d.broken)   LOG_WARN("%s: FAILED, stopped after %u command%s", t->name, d.cmds,
                             d.cmds == 1 ? "" : "s");
    else if (d.bad) LOG_WARN("%s: FAILED, %u check%s wrong (%u commands, %lu ms)", t->name,
                             d.bad, d.bad == 1 ? "" : "s", d.cmds, ms);
    else            LOG_INFO("%s: PASS (%u command%s, %lu ms)", t->name, d.cmds,
                             d.cmds == 1 ? "" : "s", ms);
    if (d.sim_stopped)
        LOG_WARN("the simulation is still stopped ('sim start' runs it)");

    d.t = NULL;
    if (d.seq) {
        d.seq_pass += pass;
        if (++d.seq_i < d.seq_n) { test_begin(d.seq[d.seq_i]); return; }
        unsigned n = d.seq_n, ok = d.seq_pass;
        d.seq = NULL;
        if (ok == n) LOG_INFO("all: %u of %u passed", ok, n);
        else         LOG_WARN("all: %u of %u passed", ok, n);
    }
}

static void cmd_done(const fpga_cmd_t *c, fpga_err_t err)
{
    if ((uint32_t)(uintptr_t)c->ctx != d.gen || !d.t) return;   /* a stopped demo's */
    uint8_t op = c->hdr.opcode;

    if (d.ph == PH_INFO_WAIT) {
        if (err != FPGA_ERR_NONE) {
            LOG_WARN("INFO: %s", fpga_err_str(err));
            d.broken = true;
            test_end();
            return;
        }
        d.cmds++;
        d.ph = PH_STEP;
        return;
    }

    if (d.expect_nack) {
        d.expect_nack = false;
        if (err == FPGA_ERR_STATUS) {
            LOG_INFO("  refused as it should be: %s (0x%02X)",
                     fpga_fail_str(fpga_fail_code()), fpga_fail_code());
            d.cmds++;
            d.i++;
            d.ph = PH_STEP;
            return;
        }
        if (err == FPGA_ERR_NONE) {
            LOG_WARN("  step %u (op 0x%02X): the FPGA said OK, it should have refused",
                     d.i, op);
            d.bad++;
            d.cmds++;
            d.i++;
            d.ph = PH_STEP;
            return;
        }
        /* any other error: the command itself failed, as below */
    }

    if (err == FPGA_ERR_MISMATCH) {
        /* an ECHO came back different: its check counts and shows how */
    } else if (err != FPGA_ERR_NONE) {
        if (err == FPGA_ERR_STATUS)
            LOG_WARN("  step %u (op 0x%02X): NACK, %s (0x%02X)", d.i, op,
                     fpga_fail_str(fpga_fail_code()), fpga_fail_code());
        else
            LOG_WARN("  step %u (op 0x%02X): %s", d.i, op, fpga_err_str(err));
        d.broken = true;
        test_end();
        return;
    }

    d.cmds++;
    if (op == STOP_SIM_OPCODE)  d.sim_stopped = true;
    if (op == START_SIM_OPCODE) d.sim_stopped = false;
    if (d.t->check) d.t->check(d.i, c);
    d.i++;
    d.ph = PH_STEP;
}

static void submit(fpga_cmd_t *c, phase_t ok_phase)
{
    c->done = cmd_done;
    c->ctx  = (void *)(uintptr_t)d.gen;
    fpga_status_t st = fpga_submit(c);
    if (st == FPGA_OK)    { d.ph = ok_phase; return; }
    if (st == FPGA_EFULL) return;                      /* try again next poll */
    LOG_WARN("  step %u (op 0x%02X): not sent, %s", d.i, c->hdr.opcode, fpga_status_str(st));
    d.broken = true;
    test_end();
}

void demo_poll(void)
{
    if (!d.t) return;

    switch (d.ph) {
    case PH_IDLE_WAIT:
        /* the game's commands still queued finish first */
        if (fpga_queued() == 0) { d.ph = d.t->no_info ? PH_STEP : PH_INFO; break; }
        if ((int32_t)(now() - d.until) >= 0) {
            LOG_WARN("the FPGA queue didn't empty in %u ms: aborting it", IDLE_TIMEOUT_MS);
            fpga_abort_all();
        }
        break;

    case PH_INFO: {
        fpga_cmd_t c = fpga_cmd_info();
        c.done = cmd_done;
        c.ctx  = (void *)(uintptr_t)d.gen;
        fpga_status_t st = fpga_submit(&c);
        if (st == FPGA_OK) { d.ph = PH_INFO_WAIT; break; }
        if (st != FPGA_EFULL && st != FPGA_EBUSY) {
            LOG_WARN("INFO: not sent, %s", fpga_status_str(st));
            d.broken = true;
            test_end();
        }
        break;
    }

    case PH_STEP: {
        fpga_cmd_t c;
        uint32_t ms = 0;
        memset(&c, 0, sizeof c);
        switch (d.t->step(d.i, &c, &ms)) {
        case STEP_CMD:  submit(&c, PH_IN_FLIGHT); break;
        case STEP_WAIT:
            if (ms == 0) { d.i++; break; }
            d.until = now() + ms;
            d.ph = PH_PAUSE;
            break;
        case STEP_END:  test_end(); break;
        }
        break;
    }

    case PH_PAUSE:
        if ((int32_t)(now() - d.until) >= 0) { d.i++; d.ph = PH_STEP; }
        break;

    case PH_INFO_WAIT:
    case PH_IN_FLIGHT:
        break;                       /* cmd_done() moves on */
    }
}

/* ======================================================================
 * Cells: patterns, filling, checking
 * ====================================================================== */
typedef uint8_t (*pattern_fn)(uint32_t x, uint32_t y);     /* material at x, y */

static void cell_xy(uint32_t k, uint16_t x0, uint16_t y0, uint16_t box_w,
                    uint32_t *x, uint32_t *y);

/* Logs exp (expected) above got (received), DUMP_PER_LINE bytes a line,
 * with ^^ under the bytes where (exp ^ got) & mask isn't 0. cells: the
 * line starts with the grid cell its first byte is (x0, y0, box_w as for
 * cell_xy()); false: with the byte offset (echo).
 *
 *   demo:   cell (0,180)  want 00 00 00 00  10 10 10 10  20 20 20 20  30 30 30 30
 *   demo:                 got  00 00 00 00  10 10 10 10  20 20 20 20  30 30 30 30
 */
static void dump(bool failed, const uint8_t *exp, const uint8_t *got, uint32_t bytes,
                 uint8_t mask, bool cells, uint16_t x0, uint16_t y0, uint16_t box_w)
{
    if (DEMO_DUMP == 0 || (DEMO_DUMP == 1 && !failed)) return;

    for (uint32_t off = 0; off < bytes; off += DUMP_PER_LINE) {
        uint32_t end = off + DUMP_PER_LINE < bytes ? off + DUMP_PER_LINE : bytes;
        char head[24], a[4 * DUMP_PER_LINE + 8], b[sizeof a], m[sizeof a];
        int na = 0, nb = 0, nm = 0;
        bool diff = false;

        if (cells) {
            uint32_t x, y;
            cell_xy(off * FPGA_SIM_IDS_PER_BYTE, x0, y0, box_w, &x, &y);
            snprintf(head, sizeof head, "cell (%lu,%lu)", (unsigned long)x, (unsigned long)y);
        } else {
            snprintf(head, sizeof head, "byte %lu", (unsigned long)off);
        }
        for (uint32_t k = off; k < end; k++) {
            const char *gap = (k > off && (k - off) % 4u == 0) ? " " : "";
            bool bad = ((exp[k] ^ got[k]) & mask) != 0;
            diff |= bad;
            na += snprintf(a + na, sizeof a - (size_t)na, "%s %02X", gap, exp[k]);
            nb += snprintf(b + nb, sizeof b - (size_t)nb, "%s %02X", gap, got[k]);
            nm += snprintf(m + nm, sizeof m - (size_t)nm, "%s %s", gap, bad ? "^^" : "  ");
        }
        while (nm > 0 && m[nm - 1] == ' ') m[--nm] = '\0';     /* no trailing spaces */
        if (failed) {
            LOG_WARN("  %-14s want%s", head, a);
            LOG_WARN("  %-14s got %s", "", b);
            if (diff) LOG_WARN("  %-14s     %s", "", m);
        } else {
            LOG_INFO("  %-14s want%s", head, a);
            LOG_INFO("  %-14s got %s", "", b);
        }
    }
}

static const char *mat_name(uint8_t m)
{
    static const char *const names[MAT_ID_MAX + 1] = {
        [MAT_AIR] = "air", [MAT_STONE] = "stone", [MAT_SAND] = "sand",
        [MAT_WATER] = "water", [MAT_LAVA] = "lava", [MAT_ACID] = "acid",
        [MAT_LIFE] = "life", [MAT_EXPLOSIVE] = "explosive",
    };
    return m <= MAT_ID_MAX && names[m] ? names[m] : "unused id";
}

/* Where cell k of a command lands. CONS (box_w 0): k cells on from
 * (x0, y0), on to the next row at the screen's right edge. BOX: a
 * box_w-wide box with its top-left at (x0, y0). */
static void cell_xy(uint32_t k, uint16_t x0, uint16_t y0, uint16_t box_w,
                    uint32_t *x, uint32_t *y)
{
    if (box_w) { *x = x0 + k % box_w; *y = y0 + k / box_w; return; }
    uint32_t w = scr()->width, p = x0 + k;
    *x = p % w;
    *y = y0 + p / w;
}

/* Fill buf with n cells of pattern p, packed. Returns the bytes. */
static uint16_t fill_cells(uint8_t *buf, uint32_t n, uint16_t x0, uint16_t y0,
                           uint16_t box_w, pattern_fn p)
{
    uint32_t bytes = fpga_sim_bytes(n), x, y;
    memset(buf, 0, bytes);
    for (uint32_t k = 0; k < n; k++) {
        cell_xy(k, x0, y0, box_w, &x, &y);
        sim_put(buf, k, p(x, y));
    }
    return (uint16_t)bytes;
}

/* Compare n cells read back with pattern p, by material (the low 4 bits
 * are the FPGA's own and only counted). */
static void check_cells(const char *what, const uint8_t *buf, uint32_t n,
                        uint16_t x0, uint16_t y0, uint16_t box_w, pattern_fn p)
{
    uint32_t wrong = 0, low = 0, x, y;
    for (uint32_t k = 0; k < n; k++) {
        cell_xy(k, x0, y0, box_w, &x, &y);
        uint8_t got = sim_get(buf, k), want = p(x, y);
        if (FPGA_SIM_IDS_PER_BYTE == 1 && (buf[k] & 0x0Fu)) low++;
        if (got == want) continue;
        if (wrong < MAX_DIFFS_SHOWN)
            LOG_WARN("    cell (%lu,%lu): wrote %s, read %s (byte 0x%02X)",
                     (unsigned long)x, (unsigned long)y, mat_name(want), mat_name(got),
                     buf[k / FPGA_SIM_IDS_PER_BYTE]);
        wrong++;
    }
    if (wrong) {
        LOG_WARN("  %s: %lu of %lu cells wrong", what, (unsigned long)wrong, (unsigned long)n);
        d.bad++;
    } else {
        LOG_INFO("  %s: %lu cell%s OK", what, (unsigned long)n, n == 1 ? "" : "s");
    }
    /* the whole thing as bytes: compared by material only, like above */
    uint16_t bytes = fill_cells(want, n, x0, y0, box_w, p);
    dump(wrong != 0, want, buf, bytes, FPGA_SIM_IDS_PER_BYTE == 1 ? 0xF0u : 0xFFu,
         true, x0, y0, box_w);
    if (low)
        LOG_INFO("  %s: %lu cells came back with low bits set (the FPGA's state bits, "
                 "not compared)", what, (unsigned long)low);
}

/* ======================================================================
 * echo: sizes and bit patterns
 * ====================================================================== */
static const uint16_t echo_len[] = { 1, 2, 16, 100, 512, 0 /* the most N allows */ };
#define ECHO_STEPS (sizeof echo_len / sizeof echo_len[0])

static uint16_t echo_n(unsigned i)
{
    uint32_t max = max_bytes(ECHO_OPCODE);
    uint32_t n = echo_len[i] ? echo_len[i] : max;
    return (uint16_t)(n < max ? n : max);
}

static step_t echo_step(unsigned i, fpga_cmd_t *c, uint32_t *ms)
{
    if (i >= ECHO_STEPS) return STEP_END;
    if (echo_len[i] == 0 && !scr()->valid) {
        LOG_INFO("  N not known yet (no INFO): the N-1 byte echo is skipped");
        *ms = 0;
        return STEP_WAIT;
    }
    uint16_t n = echo_n(i);
    uint32_t r = 0x12345678u ^ d.salt;
    for (uint16_t k = 0; k < n; k++) {
        switch (i % 6) {
        case 0:  tx[k] = 0xA5; break;
        case 1:  tx[k] = (k & 1u) ? 0xFF : 0x00; break;          /* every line flips */
        case 2:  tx[k] = (uint8_t)(k + d.salt); break;           /* counting         */
        case 3:  r = r * 1664525u + 1013904223u; tx[k] = (uint8_t)(r >> 24); break;
        case 4:  tx[k] = 0xFF; break;
        default: tx[k] = (uint8_t)(1u << ((k + d.salt) % 8u)); break;  /* one line at a time */
        }
    }
    *c = fpga_cmd_echo(tx, rx, n);
    return STEP_CMD;
}

static void echo_check(unsigned i, const fpga_cmd_t *c)
{
    (void)c;
    uint16_t n = echo_n(i);
    unsigned wrong = 0, first = 0;
    uint8_t lanes = 0;                      /* bits that were wrong anywhere */
    for (uint16_t k = 0; k < n; k++) {
        if (tx[k] == rx[k]) continue;
        if (!wrong) first = k;
        lanes |= (uint8_t)(tx[k] ^ rx[k]);
        wrong++;
    }
    if (!wrong) {
        LOG_INFO("  echo %u byte%s OK", n, n == 1 ? "" : "s");
    } else {
        LOG_WARN("  echo %u bytes: %u wrong, first at %u (sent 0x%02X, got 0x%02X), "
                 "bad bits 0x%02X (bit b = IO line b)", n, wrong, first, tx[first], rx[first], lanes);
        d.bad++;
    }
    dump(wrong != 0, tx, rx, n, 0xFFu, false, 0, 0, 0);
}

/* ======================================================================
 * info
 * ====================================================================== */
static step_t info_step(unsigned i, fpga_cmd_t *c, uint32_t *ms)
{
    (void)c; (void)ms;
    if (i == 0) {
        const fpga_info_t *in = scr();
        LOG_INFO("  screen %ux%u, N = %u bytes, session 0x%08lX", in->width, in->height,
                 in->max_cmd, (unsigned long)in->session);
        LOG_INFO("  most payload: CONS %lu B, BOX %lu B, sprite/echo %lu B",
                 (unsigned long)fpga_max_payload(WRITE_CONS_OPCODE),
                 (unsigned long)fpga_max_payload(WRITE_BOX_OPCODE),
                 (unsigned long)fpga_max_payload(WRITE_SPRITE_OPCODE));
    }
    return STEP_END;
}

/* ======================================================================
 * cons: WRITE_CONS / READ_CONS
 * ====================================================================== */
static uint8_t pat_cons(uint32_t x, uint32_t y)
{
    return (uint8_t)((x / 4u + y + d.salt) % MAT_COUNT);     /* stripes, 4 cells each */
}

/* What step i of cons moves: n cells from (x, y). */
static void cons_geo(unsigned i, uint16_t *x, uint16_t *y, uint32_t *n)
{
    uint16_t w = scr()->width, y0 = (uint16_t)(scr()->height / 2u);
    uint32_t row = max_cells(WRITE_CONS_OPCODE);
    if (max_cells(READ_CONS_OPCODE) < row) row = max_cells(READ_CONS_OPCODE);
    if (row > w) row = w;
    row -= row % FPGA_SIM_IDS_PER_BYTE;

    switch (i) {
    case 1: case 2: *x = 0;              *y = y0;               *n = row; break;
    case 3: case 5: *x = (uint16_t)(w - 8u); *y = (uint16_t)(y0 + 2u); *n = 16; break;
    case 4:         *x = 0;              *y = (uint16_t)(y0 + 3u); *n = 8;  break;
    default:        *x = (uint16_t)(w / 2u); *y = (uint16_t)(y0 + 5u); *n = FPGA_SIM_IDS_PER_BYTE; break;
    }
}

static step_t cons_step(unsigned i, fpga_cmd_t *c, uint32_t *ms)
{
    (void)ms;
    uint16_t x, y;
    uint32_t n;
    cons_geo(i, &x, &y, &n);
    switch (i) {
    case 0: *c = fpga_cmd_sim_run(false); return STEP_CMD;
    case 1: case 3: case 6:
        *c = fpga_cmd_write_cons(x, y, tx, fill_cells(tx, n, x, y, 0, pat_cons));
        return STEP_CMD;
    case 2: case 4: case 5: case 7:
        *c = fpga_cmd_read_cons(x, y, rx, (uint16_t)fpga_sim_bytes(n));
        return STEP_CMD;
    case 8:                             /* the same cell, in a 4-byte read */
        *c = fpga_cmd_read_cons(x, y, rx, 4u);
        return STEP_CMD;
    case 9: *c = fpga_cmd_sim_run(true); return STEP_CMD;
    default: return STEP_END;
    }
}

static void cons_check(unsigned i, const fpga_cmd_t *c)
{
    (void)c;
    static const char *const what[] = {
        [2] = "a row", [4] = "the part that ran onto the next row, read alone",
        [5] = "a write over the end of a row", [7] = "one byte",
        [8] = "the same byte, in a 4-byte read",    /* write or read at fault? */
    };
    if (i >= sizeof what / sizeof what[0] || !what[i]) return;
    uint16_t x, y;
    uint32_t n;
    cons_geo(i, &x, &y, &n);
    check_cells(what[i], rx, n, x, y, 0, pat_cons);
}

/* ======================================================================
 * box: WRITE_BOX / READ_BOX, and READ_CONS through a box
 * ====================================================================== */
static uint8_t pat_box(uint32_t x, uint32_t y)
{
    return (uint8_t)((x / 2u + (y / 2u) * 3u + d.salt) % MAT_COUNT);
}

/* Box sizes. They shrink to fit the screen, and are laid out left to right
 * (on to the next line when one is full) from the top-left corner. */
static const struct { uint16_t w, h; } boxes[] = {
    {  1,  1 },
    {  8,  8 },
    { 40,  4 },                     /* wide: one row read with READ_CONS too */
    {  3, 40 },                     /* tall */
    { 32,  0 },                     /* as many rows as N allows              */
};
#define NBOX       (sizeof boxes / sizeof boxes[0])
#define CROSS_BOX  2u
#define BOX_MARGIN 2u               /* from the screen's edges, and between boxes */

/* Size of box b, shrunk to the screen and to N. */
static void box_size(unsigned b, uint16_t *w, uint16_t *h)
{
    uint32_t sw = scr()->width, sh = scr()->height;
    uint32_t maxw = sw > 2u * BOX_MARGIN ? sw - 2u * BOX_MARGIN : 1u;
    uint32_t maxh = sh > 2u * BOX_MARGIN ? sh - 2u * BOX_MARGIN : 1u;
    uint32_t bw = boxes[b].w < maxw ? boxes[b].w : maxw, bh = boxes[b].h;
    if (bh == 0) {
        uint32_t cells = max_cells(WRITE_BOX_OPCODE);
        if (max_cells(READ_BOX_OPCODE) < cells) cells = max_cells(READ_BOX_OPCODE);
        bh = cells / bw;
        while (bh && fpga_sim_bytes(bw * bh) > max_bytes(WRITE_BOX_OPCODE)) bh--;
    }
    if (bh > maxh) bh = maxh;
    if (FPGA_SIM_IDS_PER_BYTE == 2 && (bw * bh) % 2u) bh += bh < maxh ? 1u : -1u;  /* whole bytes */
    *w = (uint16_t)bw;
    *h = (uint16_t)bh;
}

/* Where box b goes, and its size. Boxes that don't fit beside the earlier
 * ones go to the top-left corner on top of them; that's fine, since each
 * box is read back right after it's written. false: no room at all. */
static bool box_geo(unsigned b, uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *h)
{
    uint32_t sw = scr()->width, sh = scr()->height;
    uint32_t cx = BOX_MARGIN, cy = BOX_MARGIN, line_h = 0;
    for (unsigned k = 0; k <= b; k++) {
        box_size(k, w, h);
        if (cx + *w + BOX_MARGIN > sw) { cx = BOX_MARGIN; cy += line_h + BOX_MARGIN; line_h = 0; }
        *x = (uint16_t)cx;
        *y = (uint16_t)cy;
        if (cy + *h + BOX_MARGIN > sh) { *x = BOX_MARGIN; *y = BOX_MARGIN; }   /* no room left */
        cx += *w + BOX_MARGIN;
        if (*h > line_h) line_h = *h;
    }
    return *h && *x + *w <= sw && *y + *h <= sh;
}

/* After STOP_SIM: write box 0, read it, write box 1, read it..., with the
 * READ_CONS through the wide box straight after that box is read (later
 * boxes may land on top of it). */
enum { BOX_WRITE, BOX_READ, BOX_CROSS };
#define BOX_OPS (2u * NBOX + 1u)

static void box_op(unsigned k, unsigned *b, unsigned *op)
{
    const unsigned cross = 2u * (CROSS_BOX + 1u);
    if (k == cross) { *b = CROSS_BOX; *op = BOX_CROSS; return; }
    if (k > cross) k--;
    *b = k / 2u;
    *op = k % 2u ? BOX_READ : BOX_WRITE;
}

static step_t box_step(unsigned i, fpga_cmd_t *c, uint32_t *ms)
{
    uint16_t x, y, w, h;
    unsigned b, op;
    if (i == 0) { *c = fpga_cmd_sim_run(false); return STEP_CMD; }
    if (i == BOX_OPS + 1u) { *c = fpga_cmd_sim_run(true); return STEP_CMD; }
    if (i > BOX_OPS + 1u) return STEP_END;

    box_op(i - 1u, &b, &op);
    if (!box_geo(b, &x, &y, &w, &h)) {
        if (op == BOX_WRITE) LOG_INFO("  box %u doesn't fit the screen: skipped", b);
        *ms = 0;
        return STEP_WAIT;
    }
    switch (op) {
    case BOX_WRITE:
        *c = fpga_cmd_write_box(fpga_box(x, y, w, h), tx,
                                fill_cells(tx, (uint32_t)w * h, x, y, w, pat_box));
        break;
    case BOX_READ:
        *c = fpga_cmd_read_box(fpga_box(x, y, w, h), rx,
                               (uint16_t)fpga_sim_bytes((uint32_t)w * h));
        break;
    default:                            /* row 1 (or 0) of the wide box, with READ_CONS */
        *c = fpga_cmd_read_cons(x, (uint16_t)(y + (h > 1u)), rx, (uint16_t)fpga_sim_bytes(w));
        break;
    }
    return STEP_CMD;
}

static void box_check(unsigned i, const fpga_cmd_t *c)
{
    (void)c;
    uint16_t x, y, w, h;
    unsigned b, op;
    char what[48];
    if (i == 0 || i > BOX_OPS) return;
    box_op(i - 1u, &b, &op);
    box_geo(b, &x, &y, &w, &h);
    if (op == BOX_READ) {
        snprintf(what, sizeof what, "box %ux%u at (%u,%u)", w, h, x, y);
        check_cells(what, rx, (uint32_t)w * h, x, y, w, pat_box);
    } else if (op == BOX_CROSS) {
        check_cells("a box row read with READ_CONS", rx, w, x, (uint16_t)(y + (h > 1u)), 0, pat_box);
    }
}

/* ======================================================================
 * sprite: upload, place, move, hide, show, delete, and a refused UPDATE
 * ====================================================================== */
#define SMILEY       16u
#define SPRITE_MOVES 40u
#define HUD_CLEAR    0xF81Fu
#define PX565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

static uint16_t spr_mem, spr_id;    /* from the answers (sprite_check) */

static uint16_t smiley_px(uint32_t x, uint32_t y)
{
    int32_t dx = 2 * (int32_t)x - 15, dy = 2 * (int32_t)y - 15;    /* from the centre, x2 */
    int32_t r2 = dx * dx + dy * dy;
    if (r2 > 15 * 15) return HUD_CLEAR;
    if (r2 > 13 * 13) return PX565(120, 60, 0);                       /* outline */
    if ((x == 5 || x == 10) && (y == 5 || y == 6)) return PX565(20, 20, 20);   /* eyes */
    if (y == 10 && x >= 5 && x <= 10) return PX565(20, 20, 20);                /* mouth */
    if (y == 9 && (x == 4 || x == 11)) return PX565(20, 20, 20);
    return PX565(255, 210, 0);
}

/* Position j of the path: left to right across the middle three quarters
 * of the screen, bouncing up and down (at most 40 rows) on the way. */
static void sprite_pos(unsigned j, uint16_t *x, uint16_t *y)
{
    int32_t w = scr()->width, h = scr()->height;
    int32_t x0 = w / 8, x1 = w - w / 8 - (int32_t)SMILEY;
    int32_t y0 = h / 4, amp = h - (int32_t)SMILEY - y0 - 1;
    if (x1 < x0) x1 = x0;
    if (amp > 40) amp = 40;
    if (amp < 0)  amp = 0;
    int32_t ph  = (int32_t)((j * 8u) % 80u);
    int32_t tri = ph < 40 ? ph : 80 - ph;               /* 0 .. 40 .. 0 */
    *x = (uint16_t)(x0 + (x1 - x0) * (int32_t)j / (int32_t)(SPRITE_MOVES - 1u));
    *y = (uint16_t)(y0 + tri * amp / 40);
}

static step_t sprite_step(unsigned i, fpga_cmd_t *c, uint32_t *ms)
{
    const unsigned base = 2u + 2u * SPRITE_MOVES;
    uint16_t x, y;
    if (i == 0) {
        for (uint32_t k = 0; k < SMILEY * SMILEY; k++) {
            uint16_t v = smiley_px(k % SMILEY, k / SMILEY);
            tx[2u * k] = (uint8_t)(v >> 8);
            tx[2u * k + 1u] = (uint8_t)v;                   /* MSB first */
        }
        *c = fpga_cmd_write_sprite(tx, (uint16_t)(2u * SMILEY * SMILEY));
        return STEP_CMD;
    }
    if (i == 1) {
        sprite_pos(0, &x, &y);
        *c = fpga_cmd_sprite_write(spr_mem, true, x, y, SMILEY, SMILEY);
        return STEP_CMD;
    }
    if (i < base) {                     /* pause, move, pause, move... */
        unsigned j = (i - 2u) / 2u;
        if ((i - 2u) % 2u == 0) { *ms = 20; return STEP_WAIT; }
        sprite_pos(j, &x, &y);
        *c = fpga_cmd_sprite_update(spr_id, true, x, y);
        return STEP_CMD;
    }
    sprite_pos(SPRITE_MOVES - 1u, &x, &y);
    switch (i - base) {
    case 0: *c = fpga_cmd_sprite_update(spr_id, false, x, y); return STEP_CMD;   /* hide */
    case 1: *ms = 400; return STEP_WAIT;
    case 2: *c = fpga_cmd_sprite_update(spr_id, true, x, y);  return STEP_CMD;   /* show */
    case 3: *ms = 400; return STEP_WAIT;
    case 4: *c = fpga_cmd_sprite_delete(spr_id); return STEP_CMD;
    case 5:                             /* the deleted one: must be refused */
        *c = fpga_cmd_sprite_update(spr_id, true, x, y);
        d.expect_nack = true;
        return STEP_CMD;
    default: return STEP_END;
    }
}

static void sprite_check(unsigned i, const fpga_cmd_t *c)
{
    if (i == 0) {
        spr_mem = fpga_answer_id(c);
        LOG_INFO("  image uploaded: memID %u (memIDs are never freed: each run uses one)",
                 spr_mem);
    } else if (i == 1) {
        spr_id = fpga_answer_id(c);
        LOG_INFO("  sprite placed: spriteID %u, now moving it", spr_id);
    } else if (i == 2u + 2u * SPRITE_MOVES + 4u) {
        LOG_INFO("  moved, hidden, shown and deleted");
    }
}

/* ======================================================================
 * scene / clear: the whole grid, row by row
 * ====================================================================== */

/* Game of Life patterns, 'O' = alive. */
static const char *const gosper_gun[] = {           /* 36 x 9, fires gliders down-right */
    "........................O...........",
    "......................O.O...........",
    "............OO......OO............OO",
    "...........O...O....OO............OO",
    "OO........O.....O...OO..............",
    "OO........O...O.OO....O.O...........",
    "..........O.....O.......O...........",
    "...........O...O....................",
    "............OO......................",
};
static const char *const pulsar[] = {               /* 13 x 13, period 3 */
    "..OOO...OOO..",
    ".............",
    "O....O.O....O",
    "O....O.O....O",
    "O....O.O....O",
    "..OOO...OOO..",
    ".............",
    "..OOO...OOO..",
    "O....O.O....O",
    "O....O.O....O",
    "O....O.O....O",
    ".............",
    "..OOO...OOO..",
};

static bool life_at(const char *const *pat, uint32_t rows, uint32_t ox, uint32_t oy,
                    uint32_t x, uint32_t y)
{
    if (y < oy || y >= oy + rows || x < ox) return false;
    const char *row = pat[y - oy];
    uint32_t cx = x - ox;
    return cx < strlen(row) && row[cx] == 'O';
}

/* inside [x0, x1) x [y0, y1)? (in % of the screen; at least 1 cell each
 * way, so thin shelves don't vanish on a small screen) */
static bool in_pct(uint32_t x, uint32_t y, uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1)
{
    uint32_t w = scr()->width, h = scr()->height;
    uint32_t xa = w * x0 / 100u, xb = w * x1 / 100u, ya = h * y0 / 100u, yb = h * y1 / 100u;
    if (xb <= xa) xb = xa + 1u;
    if (yb <= ya) yb = ya + 1u;
    return x >= xa && x < xb && y >= ya && y < yb;
}

/* Where the game of life patterns go. On a small screen the gun moves to
 * the left edge; a pattern that doesn't fit is left out (-1). */
#define GUN_W     36u
#define GUN_H     9u
#define PULSAR_W  13u

static void life_places(int32_t *gx, int32_t *gy, int32_t *px, int32_t *py)
{
    int32_t w = scr()->width, h = scr()->height;
    *gy = h >= 120 ? 6 : 2;
    *gx = w >= 128 ? w * 30 / 100 : 4;
    if (*gx + (int32_t)GUN_W > w - 4 || *gy + (int32_t)GUN_H > h - 5) *gx = -1;

    *py = h >= 120 ? 8 : 2;
    *px = w * 82 / 100;
    if (*px + (int32_t)PULSAR_W > w - 4) *px = w - 4 - (int32_t)PULSAR_W;
    int32_t left = *gx >= 0 ? *gx + (int32_t)GUN_W + 2 : 4;          /* right of the gun */
    if (*px < left || *py + (int32_t)PULSAR_W > h - 5) *px = -1;
}

/* A small world with every material. Later rules win over earlier ones. */
static uint8_t pat_scene(uint32_t x, uint32_t y)
{
    uint32_t w = scr()->width, h = scr()->height;

    /* frame: walls and floor */
    if (x < 3 || x >= w - 3u || y >= h - 4u) return MAT_STONE;

    /* game of life: a glider gun at the top, a pulsar top right */
    int32_t gx, gy, px, py;
    life_places(&gx, &gy, &px, &py);
    if (gx >= 0 && life_at(gosper_gun, GUN_H, (uint32_t)gx, (uint32_t)gy, x, y)) return MAT_LIFE;
    if (px >= 0 && life_at(pulsar, PULSAR_W, (uint32_t)px, (uint32_t)py, x, y)) return MAT_LIFE;

    /* left: a block of sand above a ledge */
    if (in_pct(x, y,  0, 30, 45, 47)) return MAT_STONE;
    if (in_pct(x, y,  5, 25, 22, 40)) return MAT_SAND;

    /* top middle: a lump of acid on a thin shelf (it eats through) */
    if (in_pct(x, y, 33, 50, 36, 37)) return MAT_STONE;
    if (in_pct(x, y, 38, 45, 29, 36)) return MAT_ACID;

    /* middle: a stone basin of water, and more water above it that pours in */
    if (in_pct(x, y, 35, 37, 68, 100) || in_pct(x, y, 58, 60, 68, 100)) return MAT_STONE;
    if (in_pct(x, y, 37, 58, 76, 100)) return MAT_WATER;
    if (in_pct(x, y, 52, 64, 18, 26))  return MAT_WATER;

    /* right: a lava pool behind a stone lip, explosive on a shelf above */
    if (in_pct(x, y, 70, 72, 76, 100)) return MAT_STONE;
    if (in_pct(x, y, 72, 100, 84, 100)) return MAT_LAVA;
    if (in_pct(x, y, 75, 100, 66, 67)) return MAT_STONE;
    if (in_pct(x, y, 80, 88, 58, 66))  return MAT_EXPLOSIVE;

    /* bottom left: a few grains of sand scattered in the air */
    if (in_pct(x, y, 5, 30, 55, 90) && ((x * 7u + y * 13u) % 23u) == 0) return MAT_SAND;
    return MAT_AIR;
}

static uint8_t pat_air(uint32_t x, uint32_t y) { (void)x; (void)y; return MAT_AIR; }

/* STOP_SIM, every row (in pieces if a row is longer than one command
 * carries), START_SIM. */
static step_t grid_step(unsigned i, fpga_cmd_t *c, pattern_fn p)
{
    uint32_t w = scr()->width, h = scr()->height;
    uint32_t piece = max_cells(WRITE_CONS_OPCODE);
    piece -= piece % FPGA_SIM_IDS_PER_BYTE;
    if (piece > w) piece = w;
    uint32_t per_row = (w + piece - 1u) / piece, total = per_row * h;

    if (i == 0) { *c = fpga_cmd_sim_run(false); return STEP_CMD; }
    if (i <= total) {
        uint32_t j = i - 1u, y = j / per_row, x = (j % per_row) * piece;
        uint32_t n = w - x < piece ? w - x : piece;
        *c = fpga_cmd_write_cons((uint16_t)x, (uint16_t)y, tx,
                                 fill_cells(tx, n, (uint16_t)x, (uint16_t)y, 0, p));
        return STEP_CMD;
    }
    if (i == total + 1u) { *c = fpga_cmd_sim_run(true); return STEP_CMD; }
    return STEP_END;
}

static step_t scene_step(unsigned i, fpga_cmd_t *c, uint32_t *ms) { (void)ms; return grid_step(i, c, pat_scene); }
static step_t clear_step(unsigned i, fpga_cmd_t *c, uint32_t *ms) { (void)ms; return grid_step(i, c, pat_air); }

/* ======================================================================
 * The list, and the public functions
 * ====================================================================== */
static const test_t t_echo   = { "echo",   "ECHO, 1 byte up to N-1, several bit patterns", true, echo_step, echo_check };
static const test_t t_info   = { "info",   "INFO: screen size, N, session", false, info_step, NULL };
static const test_t t_cons   = { "cons",   "WRITE_CONS then READ_CONS: a row, over a row end, 1 byte", false, cons_step, cons_check };
static const test_t t_box    = { "box",    "WRITE_BOX then READ_BOX: 1x1 to the biggest, + READ_CONS through one", false, box_step, box_check };
static const test_t t_sprite = { "sprite", "upload a smiley, place, move, hide, delete (uses a memID)", false, sprite_step, sprite_check };
static const test_t t_scene  = { "scene",  "draw a world with every material, then run the simulation", false, scene_step, NULL };
static const test_t t_clear  = { "clear",  "the whole grid air", false, clear_step, NULL };

static const test_t *const tests[] = { &t_echo, &t_info, &t_cons, &t_box, &t_sprite, &t_scene, &t_clear };
static const test_t *const all[]   = { &t_echo, &t_info, &t_cons, &t_box, &t_sprite };
#define NTESTS (sizeof tests / sizeof tests[0])
#define NALL   (sizeof all / sizeof all[0])

bool demo_start(const char *name)
{
    if (d.t) {
        LOG_WARN("'%s' is still running ('demo stop' stops it)", d.t->name);
        return true;
    }
    if (strcmp(name, "all") == 0) {
        d.seq = all;
        d.seq_n = NALL;
        d.seq_i = d.seq_pass = 0;
        test_begin(all[0]);
        return true;
    }
    for (unsigned k = 0; k < NTESTS; k++)
        if (strcmp(name, tests[k]->name) == 0) { test_begin(tests[k]); return true; }
    return false;
}

void demo_stop(void)
{
    if (!d.t) { LOG_INFO("no demo running"); return; }
    LOG_INFO("%s: stopped", d.t->name);
    d.gen++;                        /* a command still in flight is ignored */
    d.t = NULL;
    d.seq = NULL;
    if (d.sim_stopped) LOG_WARN("the simulation is still stopped ('sim start' runs it)");
}

bool demo_busy(void) { return d.t != NULL; }

void demo_list(void)
{
    log_raw("demo <test>, the FPGA to itself while it runs:\r\n");
    for (unsigned k = 0; k < NTESTS; k++)
        log_raw("  %-7s %s\r\n", tests[k]->name, tests[k]->help);
    log_raw("  %-7s %s\r\n", "all", "echo, info, cons, box, sprite");
    log_raw("  %-7s %s\r\n", "stop", "stop the one running");
}