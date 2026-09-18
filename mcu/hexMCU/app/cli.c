/* ======================================================================
 * cli.c -- portable serial console.
 *
 * Talks only to io.h, log.h, platform.h and transport.h.
 * MUST NOT include main.h or any stm32*.h -- this file is compiled
 * unchanged by both the firmware build and the host build.
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

#define LINE_MAX      64
#define MAX_ARGS      8
#define RD_BUF_SIZE   256         /* max bytes for read / echo */
#define PROMPT        "> "
#define PROMPT_LEN    (sizeof PROMPT - 1)

#define BACKEND_READY_TIMEOUT_MS  100   /* waiting for backend to be free   */
#define XFER_DONE_TIMEOUT_MS      100   /* waiting for transfer to finish   */
#define FPGA_IRQ_TIMEOUT_MS       1000  /* waiting for FPGA_READY interrupt */

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

/* Hex dump. Wrapped in the hooks by hand because it is several lines of
 * log_raw() rather than one LOG_* call. */
static void print_hex(const uint8_t *b, uint16_t n)
{
    cli_async_begin();
    log_raw("read %u bytes:\r\n", n);
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
    cli_async_end();
}

/* ======================================================================
 * Generic job state machine
 *
 * A job is a short list of steps. Each step is one of:
 *   WRITE     header-only write with <opcode>
 *   READ      read <len> bytes (transport sends only the 0x00 skip byte;
 *             what gets read was requested by an earlier WRITE)
 *   WAIT_IRQ  wait for an FPGA_READY interrupt since the previous transfer
 *
 * Per step:  STEP_START (wait for backend free, then launch)
 *         -> STEP_BUSY  (wait for done)  -> next step
 * send = [WRITE], read = [READ], echo = [WRITE, WAIT_IRQ, READ]
 * ====================================================================== */
typedef enum { STEP_WRITE, STEP_READ, STEP_WAIT_IRQ } step_kind_t;
typedef struct { step_kind_t kind; uint8_t opcode; uint16_t len; } step_t;
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
} job;

static uint8_t rd_buf[RD_BUF_SIZE];

static void job_fail(const char *why)
{
    LOG_ERR("%s failed (step %u, %s): %s", job.name, job.idx + 1,
            step_names[job.steps[job.idx].kind], why);
    job.state = JOB_IDLE;
}

static void job_next_step(void)
{
    if (++job.idx >= job.n_steps) {
        LOG_INFO("%s: ok", job.name);
        job.state = JOB_IDLE;
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

        transport_hdr_t hdr = {0};
        hdr.opcode = s->opcode;
        hdr.len    = s->len;

        /* Take the mark BEFORE starting: an IRQ that fires during the
         * transfer then still counts for a following WAIT_IRQ step. */
        job.irq_mark = transport_ready_count();

        LOG_DBG("%s step %u/%u: %s op=0x%02X len=%u", job.name,
                job.idx + 1, job.n_steps, step_names[s->kind],
                s->opcode, s->len);

        transport_status_t st = (s->kind == STEP_WRITE)
            ? BACKEND->write(&hdr, NULL)
            : BACKEND->read(&hdr, rd_buf);

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
        if (s->kind == STEP_READ)
            print_hex(rd_buf, s->len);
        job_next_step();
    } else if (elapsed >= XFER_DONE_TIMEOUT_MS) {
        BACKEND->abort();
        job_fail("transfer never completed");
    }
}

static bool job_start(const char *name, const step_t *steps, uint8_t n)
{
    if (job.state != JOB_IDLE || n == 0 || n > MAX_STEPS) return false;
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

static void cmd_send(int argc, char **argv)
{
    unsigned long op;
    if (argc != 2) { log_raw("usage: send <opcode>\r\n"); return; }
    if (!parse_num(argv[1], 0, 0xFF, &op)) {
        log_raw("bad opcode '%s'\r\n", argv[1]); return;
    }
    step_t steps[] = { { STEP_WRITE, (uint8_t)op, 0 } };
    if (!job_start("send", steps, 1)) log_raw("busy\r\n");
}

static void cmd_read(int argc, char **argv)
{
    unsigned long len;
    if (argc != 2) { log_raw("usage: read <len>\r\n"); return; }
    if (!parse_num(argv[1], 1, RD_BUF_SIZE, &len)) {
        log_raw("bad len '%s' (1..%u)\r\n", argv[1], RD_BUF_SIZE); return;
    }
    step_t steps[] = { { STEP_READ, 0, (uint16_t)len } };
    if (!job_start("read", steps, 1)) log_raw("busy\r\n");
}

static void cmd_fecho(int argc, char **argv)
{
    unsigned long op, len;
    if (argc != 3) { log_raw("usage: echo <opcode> <len>\r\n"); return; }
    if (!parse_num(argv[1], 0, 0xFF, &op)) {
        log_raw("bad opcode '%s'\r\n", argv[1]); return;
    }
    if (!parse_num(argv[2], 1, RD_BUF_SIZE, &len)) {
        log_raw("bad len '%s' (1..%u)\r\n", argv[2], RD_BUF_SIZE); return;
    }
    step_t steps[] = {
        { STEP_WRITE,    (uint8_t)op, 0 },               /* 0x00, op     */
        { STEP_WAIT_IRQ, 0,           0 },               /* FPGA_READY   */
        { STEP_READ,     0,           (uint16_t)len },   /* 0x00, data.. */
    };
    if (!job_start("fecho", steps, 3)) log_raw("busy\r\n");
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
    log_raw("%s aborted\r\n", job.name);
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
    { "send",    cmd_send,     "send <op>           e.g. send 0x01" },
    { "read",    cmd_read,     "read <len>          e.g. read 4" },
    { "fecho",   cmd_fecho,    "fecho <op> <len>    send op, wait irq, read" },
    { "readrdy", cmd_readrdy,  "show ready pin, backend and job state" },
    { "abort",   cmd_abort,    "cancel the running job" },
    { "log",     cmd_loglevel, "log [0-4]           get/set log level" },
    { "ls",      fs_cmd_ls,    "ls [path]           list a directory" },
    { "cd",      fs_cmd_cd,    "cd [path]           change directory" },
    { "cat",     fs_cmd_cat,   "cat <file>          print a file" },
    { "echo",    fs_cmd_echo,  "echo [-a] <f> <txt> write text to a file" },
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
    for (char *t = strtok(s, " \t"); t && argc < MAX_ARGS; t = strtok(NULL, " \t"))
        argv[argc++] = t;
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
        if (n == 1) LOG_INFO("fpga ready");
        else        LOG_INFO("fpga ready (x%lu)", (unsigned long)n);
    }

    job_poll();
}