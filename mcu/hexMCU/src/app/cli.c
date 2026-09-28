/* ======================================================================
 * cli.c - portable serial console.
 *
 * On linux you could access virtual com thrue sudo minicom -b 115200 -D /dev/ttyACM0   
 * ====================================================================== */
#define LOG_TAG "cli"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>

#include "cli.h"
#include "io.h"
#include "log.h"
#include "platform.h"
#include "transport.h"
#include "fs_utils.h"

#define LINE_MAX      128        /* room for "fecho" + ~20 hex bytes */
#define MAX_ARGS      24
#define RD_BUF_SIZE   1024         /* max bytes for read / echo */
#define PROMPT        "> "
#define PROMPT_LEN    (sizeof PROMPT - 1)

#define BACKEND_READY_TIMEOUT_MS  100   /* waiting for backend to be free   */
#define XFER_DONE_TIMEOUT_MS      100   /* waiting for transfer to finish   */
#define FPGA_IRQ_TIMEOUT_MS       1000  /* waiting for FPGA_READY interrupt */

/* Opcode for the echo command: FPGA stores the payload and sends it back
 * on the next read. */
#define FECHO_OPCODE              0xEEu

/* Instruction for "hand me what I asked for" reads. */
#define READ_OPCODE               0xA5u

/* ======================================================================
 * FPGA ready reporting
 *
 * The counter itself lives in transport_core.c, bumped by the EXTI ISR on
 * hardware and by the fake FPGA on the host. The CLI only compares counts
 * from the main loop, so it never has to catch an edge live.
 * ====================================================================== */
static uint32_t ready_reported;   /* written by main loop only */

/* ======================================================================
 * Line editor state + async redraw hooks
 *
 * Anything that is not a direct reply to what the user just typed has to
 * wipe the half-typed line first and redraw it after, or output gets
 * interleaved into the command being entered.
 *
 * These two are registered with log_set_hooks() in cli_init(), so EVERY
 * LOG_* call from anywhere in the program gets this treatment for free --
 * including ones from fpga.c and the transport backend.
 *
 * Uses only \r and spaces, so it works in any serial terminal and in a
 * normal shell.
 * ====================================================================== */
static char     line[LINE_MAX];      /* what the user is currently typing */
static uint16_t line_len;
static bool     prompt_visible;      /* is "> " + line on screen right now? */

void cli_async_begin(void)
{
    if (!prompt_visible) return;
    char blank[PROMPT_LEN + LINE_MAX];
    uint16_t n = (uint16_t)(PROMPT_LEN + line_len);
    memset(blank, ' ', n);
    io_write("\r", 1);
    io_write(blank, n);
    io_write("\r", 1);
}

void cli_async_end(void)
{
    if (!prompt_visible) return;
    io_write(PROMPT, PROMPT_LEN);
    io_write(line, line_len);
}

/* Hex rows only, no hooks. Callers wrap the whole block in
 * cli_async_begin/end, since it is several log_raw() lines rather than
 * one LOG_* call. */
static void hex_rows(const uint8_t *b, uint16_t n)
{
    for (uint16_t i = 0; i < n; i += 16) {
        char row[80];
        int p = snprintf(row, sizeof row, "  %04X:", i);
        if (p < 0) break;
        for (uint16_t j = i; j < n && j < i + 16u; j++) {
            if (p >= (int)sizeof row - 4) break;
            int w = snprintf(row + p, sizeof row - (size_t)p, " %02X", b[j]);
            if (w < 0) break;
            p += w;
        }
        log_raw("%s\r\n", row);
    }
}

static void print_hex(const uint8_t *b, uint16_t n)
{
    cli_async_begin();
    log_raw("read %u bytes:\r\n", n);
    hex_rows(b, n);
    cli_async_end();
}

/* ======================================================================
 * Generic job state machine
 *
 * A job is a short list of steps. Each step is one of:
 *   WRITE     send hdr (opcode + optional x/y and len fields), then
 *             hdr.len payload bytes from data
 *   READ      send hdr (normally just READ_OPCODE), turnaround, then read
 *             hdr.len bytes -- what gets read was requested by an earlier
 *             WRITE. If expect
 *             is set the result is compared against it, else hex dumped.
 *   WAIT_IRQ  wait for an FPGA_READY interrupt since the previous transfer
 *
 * Per step:  STEP_START (wait for backend free, then launch)
 *         -> STEP_BUSY  (wait for done)  -> next step
 * send = [WRITE], read = [READ], fecho = [WRITE, WAIT_IRQ, READ]
 * ====================================================================== */
typedef enum { STEP_WRITE, STEP_READ, STEP_WAIT_IRQ } step_kind_t;
typedef struct {
    step_kind_t      kind;
    transport_hdr_t  hdr;     /* WRITE/READ: header; hdr.len = data bytes */
    const uint8_t   *data;    /* WRITE: payload (hdr.len bytes), or NULL  */
    const uint8_t   *expect;  /* READ: compare against this, or NULL      */
} step_t;
typedef enum { JOB_IDLE, JOB_STEP_START, JOB_STEP_BUSY } job_state_t;

#define MAX_STEPS 4

static const char *const step_names[]  = { "write", "read", "wait-irq" };
static const char *const state_names[] = { "idle", "starting", "busy" };

static struct {
    job_state_t state;
    const char *name;
    step_t      steps[MAX_STEPS];
    uint8_t     n_steps;
    uint8_t     idx;
    uint32_t    t0;        /* start time of the current phase */
    uint32_t    irq_mark;  /* ready_count when the last transfer started */

    /* Repeat support (fecho -r). rounds == 1 is a normal one-shot job. */
    uint32_t    rounds;       /* total rounds requested                    */
    uint32_t    round;        /* current round, 1-based                    */
    uint16_t    rand_len;     /* >0: refill tx_buf with this many random
                                 bytes before every round                  */
    uint32_t    passed, failed;
    uint32_t    fail_data;    /* failed rounds that were data mismatches   */
    uint32_t    bytes_ok, bytes_bad;
    uint32_t    lane_err[8];  /* bit errors per data line IO0..IO7         */
    uint8_t     dumps_left;   /* full hex dumps still allowed this job     */
} job;

#define REPEAT_MAX_DUMPS  3   /* in repeat mode, dump only the first few fails */

static uint8_t rd_buf[RD_BUF_SIZE];
static uint8_t tx_buf[RD_BUF_SIZE];   /* fecho payload; DMA reads it, so
                                         only touched while job is idle */

/* Compare what was sent with what came back, update the job statistics,
 * and print. One-shot jobs always dump both buffers; repeat jobs only dump
 * the first few failures so a long run doesn't flood the console.
 * Returns true if everything matched. */
static bool check_echo(const uint8_t *want, const uint8_t *got, uint16_t n)
{
    uint16_t bad = 0, first = 0;
    for (uint16_t i = 0; i < n; i++) {
        uint8_t diff = (uint8_t)(want[i] ^ got[i]);
        if (diff == 0) continue;
        if (bad == 0) first = i;
        bad++;
        for (uint8_t b = 0; b < 8; b++)
            if (diff & (1u << b)) job.lane_err[b]++;
    }
    job.bytes_bad += bad;
    job.bytes_ok  += (uint32_t)(n - bad);

    bool repeat = job.rounds > 1;
    if (repeat && bad == 0) return true;               /* quiet on success */
    if (repeat && job.dumps_left == 0) return false;   /* summary covers it */
    if (repeat) job.dumps_left--;

    cli_async_begin();
    if (repeat) log_raw("round %lu/%lu:\r\n",
                        (unsigned long)job.round, (unsigned long)job.rounds);
    log_raw("sent %u bytes:\r\n", n);
    hex_rows(want, n);
    log_raw("got  %u bytes:\r\n", n);
    hex_rows(got, n);
    if (bad == 0)
        log_raw("MATCH\r\n");
    else
        log_raw("MISMATCH: %u of %u bytes differ, first at %u "
                "(sent %02X, got %02X)\r\n",
                bad, n, first, want[first], got[first]);
    cli_async_end();
    return bad == 0;
}

static void print_summary(bool aborted)
{
    uint32_t done = job.passed + job.failed;
    cli_async_begin();
    log_raw("%s %s: %lu/%lu rounds, %lu passed, %lu failed",
            job.name, aborted ? "aborted" : "done",
            (unsigned long)done, (unsigned long)job.rounds,
            (unsigned long)job.passed, (unsigned long)job.failed);
    if (job.failed)
        log_raw(" (%lu data mismatch, %lu transfer/irq)",
                (unsigned long)job.fail_data,
                (unsigned long)(job.failed - job.fail_data));
    log_raw("\r\n");
    if (job.bytes_ok + job.bytes_bad)
        log_raw("bytes: %lu ok, %lu bad\r\n",
                (unsigned long)job.bytes_ok, (unsigned long)job.bytes_bad);
    if (job.bytes_bad) {
        log_raw("bit errors per line:");
        for (int b = 7; b >= 0; b--)
            log_raw(" IO%d=%lu", b, (unsigned long)job.lane_err[b]);
        log_raw("\r\n");
    }
    cli_async_end();
}

/* Fill tx_buf for the next round. Only called while no transfer is in
 * flight, so DMA is not reading tx_buf. */
static uint8_t rand8(void);
static void refill_random(void)
{
    for (uint16_t i = 0; i < job.rand_len; i++) tx_buf[i] = rand8();
}

/* A round has ended, successfully or not: next round, or finish. */
static void job_end(bool ok)
{
    if (job.rounds <= 1) {
        if (ok) LOG_INFO("%s: ok", job.name);
        job.state = JOB_IDLE;
        return;
    }

    if (ok) job.passed++; else job.failed++;

    if (job.round >= job.rounds) {
        print_summary(false);
        job.state = JOB_IDLE;
        return;
    }

    job.round++;
    if (job.rand_len) refill_random();
    job.idx      = 0;
    job.t0       = plat_millis();
    job.irq_mark = transport_ready_count();
    job.state    = JOB_STEP_START;
}

static void job_fail(const char *why)
{
    if (job.rounds > 1)
        LOG_ERR("%s round %lu/%lu failed (step %u, %s): %s", job.name,
                (unsigned long)job.round, (unsigned long)job.rounds,
                job.idx + 1, step_names[job.steps[job.idx].kind], why);
    else
        LOG_ERR("%s failed (step %u, %s): %s", job.name, job.idx + 1,
                step_names[job.steps[job.idx].kind], why);
    job_end(false);
}

static void job_next_step(void)
{
    if (++job.idx >= job.n_steps) {
        job_end(true);
    } else {
        job.state = JOB_STEP_START;
        job.t0    = plat_millis();
    }
}

static void job_poll(void)
{
    if (job.state == JOB_IDLE) return;

    const step_t *s = &job.steps[job.idx];
    uint32_t elapsed = plat_millis() - job.t0;   /* wrap-safe */

    if (job.state == JOB_STEP_START) {
        if (s->kind == STEP_WAIT_IRQ) {
            if (transport_ready_count() != job.irq_mark)
                job_next_step();
            else if (elapsed >= FPGA_IRQ_TIMEOUT_MS)
                job_fail("no FPGA ready irq (timeout)");
            return;
        }

        if (!BACKEND->done) {
            if (elapsed >= BACKEND_READY_TIMEOUT_MS)
                job_fail("backend not ready (timeout)");
            return;
        }

        /* Take the mark BEFORE starting: an IRQ that fires during the
         * transfer then still counts for a following WAIT_IRQ step. */
        job.irq_mark = transport_ready_count();

        LOG_DBG("%s step %u/%u: %s op=0x%02X fields=0x%X x=0x%04X "
                "y=0x%04X lenf=%u len=%u", job.name,
                job.idx + 1, job.n_steps, step_names[s->kind],
                s->hdr.opcode, s->hdr.fields, s->hdr.x, s->hdr.y,
                s->hdr.len_field, s->hdr.len);

        transport_status_t st = (s->kind == STEP_WRITE)
            ? BACKEND->write(&s->hdr, s->data)
            : BACKEND->read(&s->hdr, rd_buf);

        if (st != TRANSPORT_OK) {
            job_fail(st == TRANSPORT_BUSY ? "backend busy"
                                          : "transfer did not start");
            return;
        }
        job.state = JOB_STEP_BUSY;
        job.t0    = plat_millis();
        return;
    }

    /* JOB_STEP_BUSY */
    if (BACKEND->done) {
        if (BACKEND->error) {
            job_fail("transfer error");
            return;
        }
        if (s->kind == STEP_READ) {
            if (s->expect) {
                if (!check_echo(s->expect, rd_buf, s->hdr.len)) {
                    job.fail_data++;
                    job_fail("echo data mismatch");
                    return;
                }
            } else {
                print_hex(rd_buf, s->hdr.len);
            }
        }
        job_next_step();
    } else if (elapsed >= XFER_DONE_TIMEOUT_MS) {
        BACKEND->abort();
        job_fail("transfer never completed");
    }
}

/* rounds: how many times to run the step list (1 = once). rand_len: if
 * nonzero, tx_buf gets that many fresh random bytes before each round
 * after the first (the caller fills it for round 1). */
static bool job_start(const char *name, const step_t *steps, uint8_t n,
                      uint32_t rounds, uint16_t rand_len)
{
    if (job.state != JOB_IDLE || n == 0 || n > MAX_STEPS || rounds == 0)
        return false;
    memset(&job, 0, sizeof job);
    job.rounds     = rounds;
    job.round      = 1;
    job.rand_len   = rand_len;
    job.dumps_left = REPEAT_MAX_DUMPS;
    job.name     = name;
    memcpy(job.steps, steps, n * sizeof *steps);
    job.n_steps  = n;
    job.idx      = 0;
    job.t0       = plat_millis();
    job.irq_mark = transport_ready_count();
    job.state    = JOB_STEP_START;
    job_poll();                          /* try immediately */
    return true;
}

/* ======================================================================
 * Commands
 * ====================================================================== */
typedef void (*cmd_fn)(int argc, char **argv);
typedef struct { const char *name; cmd_fn fn; const char *help; } cli_cmd_t;

static bool parse_num(const char *s, unsigned long min, unsigned long max,
                      unsigned long *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, 0);   /* base 0: "0xff" or "255" */
    if (end == s || *end != '\0' || v < min || v > max) return false;
    *out = v;
    return true;
}

static void cmd_help(int argc, char **argv);

static void cmd_toggle(int argc, char **argv)
{
    (void)argc; (void)argv;
    plat_led_toggle();
    log_raw("led toggled\r\n");
}

/* parse_num + an error message naming the argument. */
static bool parse_arg(const char *what, const char *s, unsigned long max,
                      unsigned long *out)
{
    if (parse_num(s, 0, max, out)) return true;
    log_raw("bad %s '%s' (0..0x%lX)\r\n", what, s, max);
    return false;
}

/* send <op>                 opcode only
 * send <op> <x> <y>         + address phase (x, y)
 * send <op> <x> <y> <len>   + len field in the alt-bytes phase
 * Header only in all cases: len is just a field, no payload follows. */
static void cmd_send(int argc, char **argv)
{
    unsigned long op, x, y, len;
    transport_hdr_t hdr = {0};

    if (argc != 2 && argc != 4 && argc != 5) {
        log_raw("usage: send <op> [<x> <y> [<len>]]\r\n");
        return;
    }
    if (!parse_arg("opcode", argv[1], 0xFF, &op)) return;
    hdr.opcode = (uint8_t)op;

    if (argc >= 4) {
        if (!parse_arg("x", argv[2], 0xFFFF, &x)) return;
        if (!parse_arg("y", argv[3], 0xFFFF, &y)) return;
        hdr.fields |= TRANSPORT_F_XY;
        hdr.x = (uint16_t)x;
        hdr.y = (uint16_t)y;
    }
    if (argc == 5) {
        if (!parse_arg("len", argv[4], 0xFFFF, &len)) return;
        hdr.fields   |= TRANSPORT_F_LEN;
        hdr.len_field = (uint16_t)len;
    }

    step_t steps[] = { { .kind = STEP_WRITE, .hdr = hdr } };
    if (!job_start("send", steps, 1, 1, 0)) log_raw("busy\r\n");
}

static void cmd_read(int argc, char **argv)
{
    unsigned long len;
    if (argc != 2) { log_raw("usage: read <len>\r\n"); return; }
    if (!parse_num(argv[1], 1, RD_BUF_SIZE, &len)) {
        log_raw("bad len '%s' (1..%u)\r\n", argv[1], RD_BUF_SIZE); return;
    }
    step_t steps[] = { { .kind = STEP_READ,
                         .hdr  = { .opcode = READ_OPCODE, .len = (uint16_t)len } } };
    if (!job_start("read", steps, 1, 1, 0)) log_raw("busy\r\n");
}

/* Small xorshift32, seeded from the clock on first use. Good enough for
 * test patterns, and avoids pulling rand()'s state into the firmware. */
static uint8_t rand8(void)
{
    static uint32_t st;
    if (st == 0) st = plat_millis() * 2654435761u | 1u;
    st ^= st << 13;
    st ^= st >> 17;
    st ^= st << 5;
    return (uint8_t)(st >> 24);
}

/* fecho [-r <rounds>] <n>            echo n random bytes
 * fecho [-r <rounds>] <b0> <b1> ...   echo exactly these bytes
 *
 * [WRITE op + payload] -> [WAIT_IRQ] -> [READ op, len bytes, compare]
 * With -r the whole thing runs <rounds> times (new random bytes each
 * round) and ends with a pass/fail summary. */
#define FECHO_MAX_ROUNDS  100000ul

static void cmd_fecho(int argc, char **argv)
{
    unsigned long v, rounds = 1;
    uint16_t n, rand_len = 0;
    int a = 1;                      /* first non-option argument */

    if (argc >= 2 && strcmp(argv[1], "-r") == 0) {
        if (argc < 3 || !parse_num(argv[2], 1, FECHO_MAX_ROUNDS, &rounds)) {
            log_raw("bad repeat count (1..%lu)\r\n", FECHO_MAX_ROUNDS);
            return;
        }
        a = 3;
    }
    if (argc - a < 1) {
        log_raw("usage: fecho [-r <rounds>] <count>  or  "
                "fecho [-r <rounds>] <b0> <b1> ...\r\n");
        return;
    }
    /* tx_buf may still be feeding a DMA transfer -- check before touching it */
    if (job.state != JOB_IDLE) { log_raw("busy\r\n"); return; }

    if (argc - a == 1) {
        if (!parse_num(argv[a], 1, RD_BUF_SIZE, &v)) {
            log_raw("bad count '%s' (1..%u)\r\n", argv[a], RD_BUF_SIZE);
            return;
        }
        n = rand_len = (uint16_t)v;
        for (uint16_t i = 0; i < n; i++) tx_buf[i] = rand8();
    } else {
        n = (uint16_t)(argc - a);
        for (uint16_t i = 0; i < n; i++) {
            if (!parse_arg("byte", argv[a + i], 0xFF, &v)) return;
            tx_buf[i] = (uint8_t)v;
        }
    }

    step_t steps[] = {
        { .kind = STEP_WRITE,
          /* No len field: the FPGA takes the opcode, then drains its
           * FIFO until empty, so the payload length needs no header.
           * Keep n below the FPGA FIFO depth. */
          .hdr  = { .opcode = FECHO_OPCODE, .len = n },
          .data = tx_buf },
        { .kind = STEP_WAIT_IRQ },
        { .kind = STEP_READ, .hdr = { .opcode = READ_OPCODE, .len = n },
          .expect = tx_buf },
    };
    if (!job_start("fecho", steps, 3, (uint32_t)rounds, rand_len))
        log_raw("busy\r\n");
}

static void cmd_readrdy(int argc, char **argv)
{
    (void)argc; (void)argv;
    log_raw("ready pin : %s\r\n", plat_fpga_ready_pin() ? "high" : "low");
    log_raw("ready irqs: %lu\r\n", (unsigned long)transport_ready_count());
    log_raw("backend   : done=%u error=%u\r\n",
            (unsigned)BACKEND->done, (unsigned)BACKEND->error);
    if (job.state == JOB_IDLE) {
        log_raw("job       : idle\r\n");
    } else {
        log_raw("job       : %s, step %u/%u (%s), %s for %lu ms\r\n",
                job.name, job.idx + 1, job.n_steps,
                step_names[job.steps[job.idx].kind],
                state_names[job.state],
                (unsigned long)(plat_millis() - job.t0));
    }
}

static void cmd_abort(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (job.state == JOB_IDLE) { log_raw("nothing running\r\n"); return; }
    if (job.state == JOB_STEP_BUSY) BACKEND->abort();
    job.state = JOB_IDLE;
    if (job.rounds > 1) print_summary(true);   /* stats up to this point */
    else                log_raw("%s aborted\r\n", job.name);
}

static void cmd_loglevel(int argc, char **argv)
{
    unsigned long v;
    if (argc == 2) {
        if (!parse_num(argv[1], 0, 4, &v)) {
            log_raw("bad level '%s' (0=none 1=err 2=warn 3=info 4=debug)\r\n",
                    argv[1]);
            return;
        }
        log_set_level((int)v);
    }
    log_raw("log level: %d (compiled max %d)\r\n",
            log_get_level(), LOG_LEVEL);
}

static void cmd_exit(int argc, char **argv)
{
    (void)argc; (void)argv;

    fs_unmount();                 /* no-op if never mounted */
    log_raw("bye\r\n");

    if (!plat_exit(0))
        log_raw("cannot exit terminal on hardware\r\n");
}

static const cli_cmd_t cmds[] = {
    { "help",    cmd_help,     "list commands" },
    { "toggle",  cmd_toggle,   "toggle user LED" },
    { "send",    cmd_send,     "send <op> [<x> <y> [<len>]]  e.g. send 0x66 0xfada 0x2345 0" },
    { "read",    cmd_read,     "read <len>          e.g. read 4" },
    { "fecho",   cmd_fecho,    "fecho [-r N] <n> | <b0> <b1> ...  echo random / given bytes, N rounds" },
    { "readrdy", cmd_readrdy,  "show ready pin, backend and job state" },
    { "abort",   cmd_abort,    "cancel the running job" },
    { "log",     cmd_loglevel, "log [0-4]           get/set log level" },
    { "ls",      fs_cmd_ls,    "ls [path]           list a directory" },
    { "cd",      fs_cmd_cd,    "cd [path]           change directory" },
    { "cat",     fs_cmd_cat,   "cat <file>          print a file" },
    { "head",    fs_cmd_head,  "head <file> [n]     first n lines" },
    { "echo",    fs_cmd_echo,  "echo [-a] <f> <txt> write text to a file" },
    { "mkdir",   fs_cmd_mkdir, "mkdir [-r] <dir>    make a dir (-r: parents too)" },
    { "rm",      fs_cmd_rm,    "rm [-r] <path>      delete (-r: dir + contents)" },
    { "exist",   fs_cmd_exist, "exist <path>        true/false" },
    { "mv",      fs_cmd_mv,    "mv <src> <dst>      rename / move" },
    { "cp",      fs_cmd_cp,    "cp [-r] <src> <dst> copy (-r: dir + contents)" },
    { "exit",    cmd_exit,     "exit the emulator (host only)" },
};
#define NUM_CMDS (sizeof cmds / sizeof cmds[0])

static void cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    for (size_t i = 0; i < NUM_CMDS; i++)
        log_raw("  %-9s %s\r\n", cmds[i].name, cmds[i].help);
}

static void dispatch(char *s)
{
    char *argv[MAX_ARGS];
    int argc = 0;
    for (char *t = strtok(s, " \t"); t; t = strtok(NULL, " \t")) {
        if (argc == MAX_ARGS) {
            log_raw("too many arguments (max %d)\r\n", MAX_ARGS - 1);
            return;
        }
        argv[argc++] = t;
    }
    if (argc == 0) return;

    for (size_t i = 0; i < NUM_CMDS; i++)
        if (strcmp(argv[0], cmds[i].name) == 0) { cmds[i].fn(argc, argv); return; }

    log_raw("unknown command '%s' (try help)\r\n", argv[0]);
}

/* ======================================================================
 * Line editor
 * ====================================================================== */
static void handle_char(char c)
{
    static char prev;
    if (c == '\n' && prev == '\r') { prev = c; return; }   /* swallow CRLF pairs */
    prev = c;

    if (c == '\r' || c == '\n') {
        char cmdline[LINE_MAX];
        memcpy(cmdline, line, line_len);
        cmdline[line_len] = '\0';
        line_len = 0;                 /* clear before dispatch, so an async  */
        io_write("\r\n", 2);          /* print during it doesn't redraw the  */
        prompt_visible = false;       /* old command                         */
        dispatch(cmdline);
        io_write(PROMPT, PROMPT_LEN); /* prompt is always back right away;   */
        prompt_visible = true;        /* job results arrive as async lines   */
    } else if (c == '\b' || c == 0x7F) {                 /* backspace / DEL */
        if (line_len) { line_len--; io_write("\b \b", 3); }
    } else if (c >= 0x20 && c < 0x7F && line_len < LINE_MAX - 1) {
        line[line_len++] = c;
        io_write(&c, 1);                                  /* echo */
    }
}

/* ======================================================================
 * Public
 * ====================================================================== */
void cli_init(void)
{
    io_init();
    log_set_hooks(cli_async_begin, cli_async_end);

    log_raw("\r\nhexmcu console, type 'help'\r\n" PROMPT);
    prompt_visible = true;
    ready_reported = transport_ready_count();
}

void cli_poll(void)
{
    int c;
    while ((c = io_getc()) >= 0)
        handle_char((char)c);

    uint32_t rc = transport_ready_count();
    if (rc != ready_reported) {
        uint32_t n = rc - ready_reported;
        ready_reported = rc;
        if (job.state != JOB_IDLE && job.rounds > 1) {
            /* one READY per round is expected -- don't flood the console */
        } else if (n == 1) LOG_INFO("fpga ready");
        else        LOG_INFO("fpga ready (x%lu)", (unsigned long)n);
    }

    job_poll();
}