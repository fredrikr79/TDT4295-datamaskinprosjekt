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
#include "fpga.h"

#define LINE_MAX      128        /* room for "fecho" + ~20 hex bytes */
#define MAX_ARGS      24
#define RD_BUF_SIZE   1024         /* max bytes for read / echo */
#define PROMPT        "> "
#define PROMPT_LEN    (sizeof PROMPT - 1)

/* ======================================================================
 * FPGA ready reporting
 *
 * The counter lives in transport_core.c, bumped by the EXTI ISR on
 * hardware and by the fake FPGA on the host. Commands in fpga.c consume
 * one READY each (fpga_ready_used()). The console only reports the ones
 * nobody waited for: READY after a raw 'send', or a stale / extra pulse.
 * ====================================================================== */
static uint32_t ready_seen, used_seen;   /* written by main loop only */
static int32_t  ready_spare;             /* READYs not (yet) consumed  */

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
 * Console side of FPGA commands
 *
 * The console runs one FPGA command of its own at a time, through the
 * same queue as everything else. It builds the command into pend.cmd and
 * submits it with cli_done() as the callback, which prints the result.
 *
 * If the queue is full, the console keeps retrying from cli_poll() for
 * up to CLI_SUBMIT_TIMEOUT_MS, then gives up.
 *
 * fecho -r is done here: each round is one echo command; cli_done()
 * records it and resubmits the same command with fresh random bytes.
 * ====================================================================== */
#define CLI_SUBMIT_TIMEOUT_MS  1000u

typedef enum { PEND_NONE, PEND_SEND, PEND_READ, PEND_FECHO, PEND_INFO } pend_t;
static const char *const pend_names[] = { "-", "send", "read", "fecho", "info" };

static struct {
    pend_t      what;
    fpga_cmd_t  cmd;          /* what is (to be) submitted                 */
    bool        waiting;      /* queue was full: retrying from cli_poll()  */
    uint32_t    since;        /* when waiting started                      */
    uint16_t    len;          /* read / fecho byte count                   */

    /* fecho repeat. rounds == 1 is a normal one-shot. */
    uint32_t    rounds;       /* total rounds requested                    */
    uint32_t    round;        /* current round, 1-based                    */
    uint16_t    rand_len;     /* >0: new random bytes before every round   */
    uint32_t    passed, failed;
    uint32_t    fail_data;    /* failed rounds that were data mismatches   */
    uint32_t    bytes_ok, bytes_bad;
    uint32_t    lane_err[8];  /* bit errors per data line IO0..IO7         */
    uint8_t     dumps_left;   /* full hex dumps still allowed              */
} pend;

#define REPEAT_MAX_DUMPS  3   /* in repeat mode, dump only the first few fails */

static uint8_t rd_buf[RD_BUF_SIZE];
static uint8_t tx_buf[RD_BUF_SIZE];   /* fecho payload; DMA reads it, so
                                         only touched while no console
                                         command is queued */

/* Compare what was sent with what came back, update the statistics, and
 * print. One-shot runs always dump both buffers; repeat runs only dump
 * the first few failures so a long run doesn't flood the console. */
static void check_echo(const uint8_t *want, const uint8_t *got, uint16_t n)
{
    uint16_t bad = 0, first = 0;
    for (uint16_t i = 0; i < n; i++) {
        uint8_t diff = (uint8_t)(want[i] ^ got[i]);
        if (diff == 0) continue;
        if (bad == 0) first = i;
        bad++;
        for (uint8_t b = 0; b < 8; b++)
            if (diff & (1u << b)) pend.lane_err[b]++;
    }
    pend.bytes_bad += bad;
    pend.bytes_ok  += (uint32_t)(n - bad);

    bool repeat = pend.rounds > 1;
    if (repeat && bad == 0) return;                 /* quiet on success  */
    if (repeat && pend.dumps_left == 0) return;     /* summary covers it */
    if (repeat) pend.dumps_left--;

    cli_async_begin();
    if (repeat) log_raw("round %lu/%lu:\r\n",
                        (unsigned long)pend.round, (unsigned long)pend.rounds);
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
}

static void print_summary(bool aborted)
{
    uint32_t done = pend.passed + pend.failed;
    cli_async_begin();
    log_raw("fecho %s: %lu/%lu rounds, %lu passed, %lu failed",
            aborted ? "aborted" : "done",
            (unsigned long)done, (unsigned long)pend.rounds,
            (unsigned long)pend.passed, (unsigned long)pend.failed);
    if (pend.failed)
        log_raw(" (%lu data mismatch, %lu transfer/irq)",
                (unsigned long)pend.fail_data,
                (unsigned long)(pend.failed - pend.fail_data));
    log_raw("\r\n");
    if (pend.bytes_ok + pend.bytes_bad)
        log_raw("bytes: %lu ok, %lu bad\r\n",
                (unsigned long)pend.bytes_ok, (unsigned long)pend.bytes_bad);
    if (pend.bytes_bad) {
        log_raw("bit errors per line:");
        for (int b = 7; b >= 0; b--)
            log_raw(" IO%d=%lu", b, (unsigned long)pend.lane_err[b]);
        log_raw("\r\n");
    }
    cli_async_end();
}

static uint8_t rand8(void);
static void pend_submit(void);

/* The console's command is over, or never got into the queue. */
static void pend_end(bool aborted)
{
    if (pend.what == PEND_FECHO && pend.rounds > 1) print_summary(aborted);
    pend.what    = PEND_NONE;
    pend.waiting = false;
}

/* One fecho round has ended: record it, then start the next or finish. */
static void fecho_round_done(fpga_err_t err)
{
    bool ok = err == FPGA_ERR_NONE;

    if (err == FPGA_ERR_ABORTED || err == FPGA_ERR_CANCELLED) {
        if (pend.rounds <= 1) LOG_ERR("fecho %s", fpga_err_str(err));
        pend_end(true);
        return;
    }

    if (ok || err == FPGA_ERR_MISMATCH)
        check_echo(tx_buf, rd_buf, pend.len);       /* stats + dump */
    if (err == FPGA_ERR_MISMATCH)
        pend.fail_data++;
    else if (!ok && pend.rounds > 1)
        LOG_ERR("fecho round %lu/%lu failed: %s",
                (unsigned long)pend.round, (unsigned long)pend.rounds,
                fpga_err_str(err));
    else if (!ok)
        LOG_ERR("fecho failed: %s", fpga_err_str(err));

    if (pend.rounds <= 1) { pend_end(false); return; }

    if (ok) pend.passed++; else pend.failed++;
    if (pend.round >= pend.rounds) { pend_end(false); return; }

    /* Our echo is out of the queue, so tx_buf is safe to refill. */
    pend.round++;
    for (uint16_t i = 0; i < pend.rand_len; i++) tx_buf[i] = rand8();
    pend_submit();
}

static void print_info(void)
{
    const fpga_info_t *in = fpga_link_info();
    cli_async_begin();
    if (!in->valid)
        log_raw("fpga info: unknown (no INFO yet), size check off\r\n");
    else
        log_raw("fpga info: %ux%u, N=%u bytes, session 0x%04X\r\n",
                in->width, in->height, in->max_cmd, in->session);
    cli_async_end();
}

/* Done callback for every console command, called from fpga_poll(). */
static void cli_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    (void)cmd;
    if (pend.what == PEND_FECHO) { fecho_round_done(err); return; }

    pend_t what = pend.what;
    pend_end(err == FPGA_ERR_ABORTED || err == FPGA_ERR_CANCELLED);

    if (err == FPGA_ERR_STATUS)
        LOG_ERR("%s failed: FPGA status %u", pend_names[what],
                fpga_ready_status());
    else if (err != FPGA_ERR_NONE)
        LOG_ERR("%s failed: %s", pend_names[what], fpga_err_str(err));
    else if (what == PEND_INFO)
        print_info();
    else if (what == PEND_READ)
        print_hex(rd_buf, pend.len);
    else
        LOG_INFO("%s: ok", pend_names[what]);
}

/* Put pend.cmd in the queue. Full queue: keep trying from cli_poll(). */
static void pend_submit(void)
{
    pend.cmd.done = cli_done;
    fpga_status_t st = fpga_submit(&pend.cmd);

    if (st == FPGA_OK) { pend.waiting = false; return; }
    if (st == FPGA_EFULL) {
        if (!pend.waiting) { pend.waiting = true; pend.since = plat_millis(); }
        return;
    }
    if (st == FPGA_ETOOBIG)
        LOG_ERR("%s refused: %s, N=%u", pend_names[pend.what],
                fpga_status_str(st), fpga_link_info()->max_cmd);
    else
        LOG_ERR("%s refused: %s", pend_names[pend.what], fpga_status_str(st));
    pend_end(true);
}

static void pend_start(pend_t what, fpga_cmd_t cmd)
{
    pend.what    = what;
    pend.cmd     = cmd;
    pend.waiting = false;
    pend_submit();
}

/* One console command at a time: its buffers are shared. */
static bool console_busy(void)
{
    if (pend.what == PEND_NONE) return false;
    log_raw("busy: '%s' still running (abort to cancel)\r\n",
            pend_names[pend.what]);
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

/* send <op>                       opcode only
 * send <op> <x> <y>               + address phase (x, y)
 * send <op> <x> <y> <n>           + 16-bit alt bytes (e.g. a line's N)
 * send <op> <x1> <y1> <x2> <y2>   + 32-bit alt bytes (a box's x2:y2)
 * Header only in all cases: no payload follows. */
static void cmd_send(int argc, char **argv)
{
    unsigned long op, x, y, a1, a2;
    transport_hdr_t hdr = {0};

    if (argc != 2 && argc != 4 && argc != 5 && argc != 6) {
        log_raw("usage: send <op> [<x> <y> [<n> | <x2> <y2>]]\r\n");
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
        if (!parse_arg("n", argv[4], 0xFFFF, &a1)) return;
        hdr.fields |= TRANSPORT_F_ALT16;
        hdr.alt     = (uint32_t)a1;
    }
    if (argc == 6) {
        if (!parse_arg("x2", argv[4], 0xFFFF, &a1)) return;
        if (!parse_arg("y2", argv[5], 0xFFFF, &a2)) return;
        hdr.fields |= TRANSPORT_F_ALT32;
        hdr.alt     = ((uint32_t)a1 << 16) | (uint32_t)a2;
    }

    if (console_busy()) return;
    pend_start(PEND_SEND, fpga_cmd_raw_write(hdr, NULL));
}

static void cmd_read(int argc, char **argv)
{
    unsigned long len;
    if (argc != 2) { log_raw("usage: read <len>\r\n"); return; }
    if (!parse_num(argv[1], 1, RD_BUF_SIZE, &len)) {
        log_raw("bad len '%s' (1..%u)\r\n", argv[1], RD_BUF_SIZE); return;
    }
    transport_hdr_t hdr = { .opcode = READ_OPCODE, .len = (uint16_t)len };
    if (console_busy()) return;
    pend.len = (uint16_t)len;
    pend_start(PEND_READ, fpga_cmd_raw_read(hdr, rd_buf));
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
 * One round is one echo command. With -r the console runs <rounds> of
 * them (new random bytes each round) and ends with a pass/fail summary. */
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
    if (console_busy()) return;

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

    memset(&pend, 0, sizeof pend);
    pend.len        = n;
    pend.rounds     = (uint32_t)rounds;
    pend.round      = 1;
    pend.rand_len   = rand_len;
    pend.dumps_left = REPEAT_MAX_DUMPS;

    pend_start(PEND_FECHO, fpga_cmd_echo(tx_buf, rd_buf, n));
}

/* info                          show what we know about the FPGA
 * info query                    send INFO and show the answer
 * info set <w> <h> <n> [<sid>]  pretend INFO said this (debugging)
 * info clear                    forget it: no size check until next INFO */
static void cmd_info(int argc, char **argv)
{
    if (argc == 1) { print_info(); return; }

    if (argc == 2 && strcmp(argv[1], "query") == 0) {
        if (console_busy()) return;
        pend_start(PEND_INFO, fpga_cmd_info());
        return;
    }

    if (argc == 2 && strcmp(argv[1], "clear") == 0) {
        fpga_info_t in = {0};
        fpga_set_link_info(&in);
        print_info();
        return;
    }

    if ((argc == 5 || argc == 6) && strcmp(argv[1], "set") == 0) {
        unsigned long w, h, n, sid = 0;
        if (!parse_arg("width",  argv[2], 0xFFFF, &w)) return;
        if (!parse_arg("height", argv[3], 0xFFFF, &h)) return;
        if (!parse_arg("n",      argv[4], 0xFFFF, &n)) return;
        if (argc == 6 && !parse_arg("session", argv[5], 0xFFFF, &sid)) return;
        fpga_info_t in = { .valid = true, .width = (uint16_t)w,
                           .height = (uint16_t)h, .max_cmd = (uint16_t)n,
                           .session = (uint16_t)sid };
        fpga_set_link_info(&in);
        print_info();
        return;
    }

    log_raw("usage: info [query | clear | set <w> <h> <n> [<session>]]\r\n");
}

static void cmd_readrdy(int argc, char **argv)
{
    (void)argc; (void)argv;
    log_raw("ready pin : %s\r\n", plat_fpga_ready_pin() ? "high" : "low");
    log_raw("ready irqs: %lu\r\n", (unsigned long)transport_ready_count());
    log_raw("backend   : done=%u error=%u\r\n",
            (unsigned)BACKEND->done, (unsigned)BACKEND->error);
    log_raw("status    : %u at last READY\r\n", fpga_ready_status());
    log_raw("queue     : %u/%u\r\n", fpga_queued(), (unsigned)FPGA_QUEUE_LEN);
    const fpga_cmd_t *cur = fpga_current();
    if (cur)
        log_raw("running   : op 0x%02X, %s for %lu ms\r\n", cur->hdr.opcode,
                fpga_current_step(), (unsigned long)fpga_current_ms());
    else
        log_raw("running   : -\r\n");
    if (pend.what == PEND_FECHO && pend.rounds > 1)
        log_raw("console   : fecho round %lu/%lu", (unsigned long)pend.round,
                (unsigned long)pend.rounds);
    else
        log_raw("console   : %s", pend_names[pend.what]);
    log_raw("%s\r\n", pend.waiting ? " (waiting for queue space)" : "");
}

/* Abort the running FPGA command and cancel everything queued -- the
 * game's commands too. The done callbacks report what was stopped. */
static void cmd_abort(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint8_t n = fpga_queued();
    if (n == 0 && pend.what == PEND_NONE) {
        log_raw("nothing running\r\n");
        return;
    }
    if (pend.waiting) {                 /* never made it into the queue */
        log_raw("%s dropped\r\n", pend_names[pend.what]);
        pend_end(true);
    }
    fpga_abort_all();
    if (n) log_raw("aborted %u fpga command%s\r\n", n, n == 1 ? "" : "s");
}

/* trace [on|off] -- one debug line per FPGA transfer and step */
static void cmd_trace(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "on") == 0)       transport_set_trace(true);
    else if (argc == 2 && strcmp(argv[1], "off") == 0) transport_set_trace(false);
    else if (argc != 1) { log_raw("usage: trace [on|off]\r\n"); return; }
    log_raw("trace: %s%s\r\n", transport_trace_on() ? "on" : "off",
            transport_trace_on() && log_get_level() < 4
                ? " (shows at log level 4: 'log 4')" : "");
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

/* Leave cleanly: wipe the prompt line, unmount, exit. Used by the exit
 * command and by the host port when the window is closed. */
void cli_quit(const char *why)
{
    bool was_visible = prompt_visible;

    cli_async_begin();              /* wipe "> half-typed" if it is shown */
    prompt_visible = false;         /* and keep log hooks from redrawing it */

    fs_unmount();                   /* no-op if never mounted */
    if (why) log_raw("%s, bye\r\n", why);
    else     log_raw("bye\r\n");

    if (plat_exit(0)) return;       /* host: does not come back */

    log_raw("cannot exit terminal on hardware\r\n");
    prompt_visible = was_visible;
    cli_async_end();
}

static void cmd_exit(int argc, char **argv)
{
    (void)argc; (void)argv;
    cli_quit(NULL);
}
static const cli_cmd_t cmds[] = {
    { "help",    cmd_help,     "list commands" },
    { "toggle",  cmd_toggle,   "toggle user LED" },
    { "send",    cmd_send,     "send <op> [<x> <y> [<n> | <x2> <y2>]]  e.g. send 0x66 0xfada 0x2345 0" },
    { "read",    cmd_read,     "read <len>          e.g. read 4" },
    { "fecho",   cmd_fecho,    "fecho [-r N] <n> | <b0> <b1> ...  echo random / given bytes, N rounds" },
    { "readrdy", cmd_readrdy,  "show ready pin, backend and fpga queue" },
    { "info",    cmd_info,     "info [query | clear | set <w> <h> <n> [<sid>]]" },
    { "abort",   cmd_abort,    "abort all queued fpga commands" },
    { "log",     cmd_loglevel, "log [0-4]           get/set log level" },
    { "trace",   cmd_trace,    "trace [on|off]      per-transfer debug lines" },
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
    ready_seen = transport_ready_count();
    used_seen  = fpga_ready_used();
}

void cli_poll(void)
{
    /* Results arrive through cli_done(), called from fpga_poll() just
     * before this. Here: retry a submit that found the queue full. */
    if (pend.waiting) {
        if (fpga_has_space()) {
            pend_submit();
        } else if (plat_millis() - pend.since >= CLI_SUBMIT_TIMEOUT_MS) {
            LOG_ERR("%s: fpga queue still full after %u ms, gave up",
                    pend_names[pend.what], CLI_SUBMIT_TIMEOUT_MS);
            pend_end(true);
        }
    }

    /* A READY can land between fpga_poll() and here, before the command
     * waiting for it has consumed it -- so only report spare READYs once
     * the queue is empty and nothing can still claim them. */
    uint32_t rc = transport_ready_count(), used = fpga_ready_used();
    ready_spare += (int32_t)(rc - ready_seen) - (int32_t)(used - used_seen);
    ready_seen = rc;
    used_seen  = used;
    if (ready_spare < 0) ready_spare = 0;
    if (ready_spare > 0 && fpga_queued() == 0) {
        if (ready_spare == 1) LOG_INFO("fpga ready");
        else LOG_INFO("fpga ready (x%ld)", (long)ready_spare);
        ready_spare = 0;
    }

    int c;
    while ((c = io_getc()) >= 0)
        handle_char((char)c);
}