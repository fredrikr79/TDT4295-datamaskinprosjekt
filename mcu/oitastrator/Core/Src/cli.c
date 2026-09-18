#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "main.h"
#include "transport.h"
#include "cli.h"

#define RX_BUF_SIZE   64          /* must be power of two */
#define LINE_MAX      64
#define MAX_ARGS      8
#define RD_BUF_SIZE   256         /* max bytes for read / echo */
#define PROMPT        "> "
#define PROMPT_LEN    (sizeof PROMPT - 1)

#define BACKEND_READY_TIMEOUT_MS  100   /* waiting for backend to be free   */
#define XFER_DONE_TIMEOUT_MS      100   /* waiting for transfer to finish   */
#define FPGA_IRQ_TIMEOUT_MS       1000  /* waiting for FPGA_READY interrupt */

/* ======================================================================
 * UART RX (interrupt -> ring buffer)
 * ====================================================================== */
static UART_HandleTypeDef *cli_uart;
static uint8_t  rx_byte;
static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head, rx_tail;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != cli_uart) return;
    uint16_t next = (rx_head + 1) & (RX_BUF_SIZE - 1);
    if (next != rx_tail) {               /* drop byte if buffer full */
        rx_buf[rx_head] = rx_byte;
        rx_head = next;
    }
    HAL_UART_Receive_IT(cli_uart, &rx_byte, 1);   /* re-arm */
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    /* overrun/noise aborts reception in HAL, so re-arm or RX dies silently */
    if (huart == cli_uart) HAL_UART_Receive_IT(cli_uart, &rx_byte, 1);
}

/* ======================================================================
 * FPGA ready interrupt
 * The ISR only counts. Printing happens in cli_poll(), in main context,
 * so the UART is never used from two places at once.
 * ====================================================================== */
static volatile uint32_t ready_count;     /* written by ISR only       */
static uint32_t          ready_reported;  /* written by main loop only */

void cli_fpga_ready_irq(void)
{
    ready_count++;
}

/* ======================================================================
 * Output
 * ====================================================================== */
static char     line[LINE_MAX];      /* what the user is currently typing */
static uint16_t line_len;
static bool     prompt_visible;      /* is "> " + line on screen right now? */

static void uart_tx(const char *s, uint16_t n)
{
    if (n) HAL_UART_Transmit(cli_uart, (uint8_t *)s, n, 100);
}

static void cli_vprintf(const char *fmt, va_list ap)
{
    char buf[128];
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    if (n <= 0) return;
    if (n >= (int)sizeof buf) n = sizeof buf - 1;
    uart_tx(buf, (uint16_t)n);
}

static void cli_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    cli_vprintf(fmt, ap);
    va_end(ap);
}

/*
 * Async output (anything not a direct reply to what was just typed):
 *   async_begin(): wipe "> half-typed-cmd" off the current line
 *   ...print full lines ending in \r\n...
 *   async_end():   redraw "> half-typed-cmd" below
 * Uses only \r and spaces, so it works in any serial terminal.
 */
static void async_begin(void)
{
    if (!prompt_visible) return;
    char blank[PROMPT_LEN + LINE_MAX];
    uint16_t n = (uint16_t)(PROMPT_LEN + line_len);
    memset(blank, ' ', n);
    uart_tx("\r", 1);
    uart_tx(blank, n);
    uart_tx("\r", 1);
}

static void async_end(void)
{
    if (!prompt_visible) return;
    uart_tx(PROMPT, PROMPT_LEN);
    uart_tx(line, line_len);
}

static void cli_async(const char *fmt, ...)
{
    va_list ap;
    async_begin();
    va_start(ap, fmt);
    cli_vprintf(fmt, ap);
    va_end(ap);
    uart_tx("\r\n", 2);
    async_end();
}

static void print_hex(const uint8_t *b, uint16_t n)
{
    async_begin();
    cli_printf("read %u bytes:\r\n", n);
    for (uint16_t i = 0; i < n; i += 16) {
        char row[64];
        int p = snprintf(row, sizeof row, "  %04X:", i);
        for (uint16_t j = i; j < n && j < i + 16u; j++)
            p += snprintf(row + p, sizeof row - p, " %02X", b[j]);
        cli_printf("%s\r\n", row);
    }
    async_end();
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
    cli_async("%s error (step %u, %s): %s", job.name, job.idx + 1,
              step_names[job.steps[job.idx].kind], why);
    job.state = JOB_IDLE;
}

static void job_next_step(void)
{
    if (++job.idx >= job.n_steps) {
        cli_async("%s: ok", job.name);
        job.state = JOB_IDLE;
    } else {
        job.state = JOB_STEP_START;
        job.t0    = HAL_GetTick();
    }
}

static void job_poll(void)
{
    if (job.state == JOB_IDLE) return;

    const step_t *s = &job.steps[job.idx];
    uint32_t elapsed = HAL_GetTick() - job.t0;   /* wrap-safe */

    if (job.state == JOB_STEP_START) {
        if (s->kind == STEP_WAIT_IRQ) {
            if (ready_count != job.irq_mark)
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
        job.irq_mark = ready_count;

        HAL_StatusTypeDef st = (s->kind == STEP_WRITE)
            ? BACKEND->write(&hdr, NULL)
            : BACKEND->read(&hdr, rd_buf);

        if (st != HAL_OK) {
            job_fail("transfer did not start");
            return;
        }
        job.state = JOB_STEP_BUSY;
        job.t0    = HAL_GetTick();
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
    job.name    = name;
    memcpy(job.steps, steps, n * sizeof *steps);
    job.n_steps = n;
    job.idx     = 0;
    job.t0      = HAL_GetTick();
    job.irq_mark = ready_count;
    job.state   = JOB_STEP_START;
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
    HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
    cli_printf("led toggled\r\n");
}

static void cmd_send(int argc, char **argv)
{
    unsigned long op;
    if (argc != 2) { cli_printf("usage: send <opcode>\r\n"); return; }
    if (!parse_num(argv[1], 0, 0xFF, &op)) {
        cli_printf("bad opcode '%s'\r\n", argv[1]); return;
    }
    step_t steps[] = { { STEP_WRITE, (uint8_t)op, 0 } };
    if (!job_start("send", steps, 1)) cli_printf("busy\r\n");
}

static void cmd_read(int argc, char **argv)
{
    unsigned long len;
    if (argc != 2) { cli_printf("usage: read <len>\r\n"); return; }
    if (!parse_num(argv[1], 1, RD_BUF_SIZE, &len)) {
        cli_printf("bad len '%s' (1..%u)\r\n", argv[1], RD_BUF_SIZE); return;
    }
    step_t steps[] = { { STEP_READ, 0, (uint16_t)len } };
    if (!job_start("read", steps, 1)) cli_printf("busy\r\n");
}

static void cmd_echo(int argc, char **argv)
{
    unsigned long op, len;
    if (argc != 3) { cli_printf("usage: echo <opcode> <len>\r\n"); return; }
    if (!parse_num(argv[1], 0, 0xFF, &op)) {
        cli_printf("bad opcode '%s'\r\n", argv[1]); return;
    }
    if (!parse_num(argv[2], 1, RD_BUF_SIZE, &len)) {
        cli_printf("bad len '%s' (1..%u)\r\n", argv[2], RD_BUF_SIZE); return;
    }
    step_t steps[] = {
        { STEP_WRITE,    (uint8_t)op, 0 },               /* 0x00, op     */
        { STEP_WAIT_IRQ, 0,           0 },               /* FPGA_READY   */
        { STEP_READ,     0,           (uint16_t)len },   /* 0x00, data.. */
    };
    if (!job_start("echo", steps, 3)) cli_printf("busy\r\n");
}

static void cmd_readrdy(int argc, char **argv)
{
    (void)argc; (void)argv;
    GPIO_PinState pin = HAL_GPIO_ReadPin(FPGA_READY_GPIO_Port, FPGA_READY_Pin);
    cli_printf("ready pin : %s\r\n", pin == GPIO_PIN_SET ? "high" : "low");
    cli_printf("ready irqs: %lu\r\n", (unsigned long)ready_count);
    cli_printf("backend   : done=%u error=%u\r\n",
               (unsigned)BACKEND->done, (unsigned)BACKEND->error);
    if (job.state == JOB_IDLE) {
        cli_printf("job       : idle\r\n");
    } else {
        cli_printf("job       : %s, step %u/%u (%s), %s for %lu ms\r\n",
                   job.name, job.idx + 1, job.n_steps,
                   step_names[job.steps[job.idx].kind],
                   state_names[job.state],
                   (unsigned long)(HAL_GetTick() - job.t0));
    }
}

static void cmd_abort(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (job.state == JOB_IDLE) { cli_printf("nothing running\r\n"); return; }
    if (job.state == JOB_STEP_BUSY) BACKEND->abort();
    job.state = JOB_IDLE;
    cli_printf("%s aborted\r\n", job.name);
}

static const cli_cmd_t cmds[] = {
    { "help",    cmd_help,    "list commands" },
    { "toggle",  cmd_toggle,  "toggle user LED" },
    { "send",    cmd_send,    "send <op>           e.g. send 0x01" },
    { "read",    cmd_read,    "read <len>          e.g. read 4" },
    { "echo",    cmd_echo,    "echo <op> <len>     send op, wait irq, read" },
    { "readrdy", cmd_readrdy, "show ready pin, backend and job state" },
    { "abort",   cmd_abort,   "cancel the running job" },
};
#define NUM_CMDS (sizeof cmds / sizeof cmds[0])

static void cmd_help(int argc, char **argv)
{
    (void)argc; (void)argv;
    for (size_t i = 0; i < NUM_CMDS; i++)
        cli_printf("  %-8s %s\r\n", cmds[i].name, cmds[i].help);
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

    cli_printf("unknown command '%s' (try help)\r\n", argv[0]);
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
        line_len = 0;                 /* clear before dispatch, so an async */
        uart_tx("\r\n", 2);           /* print during it doesn't redraw the */
        prompt_visible = false;       /* old command                        */
        dispatch(cmdline);
        uart_tx(PROMPT, PROMPT_LEN);  /* prompt is always back right away; */
        prompt_visible = true;        /* job results arrive as async lines  */
    } else if (c == '\b' || c == 0x7F) {                 /* backspace / DEL */
        if (line_len) { line_len--; uart_tx("\b \b", 3); }
    } else if (c >= 0x20 && c < 0x7F && line_len < LINE_MAX - 1) {
        line[line_len++] = c;
        uart_tx(&c, 1);                                   /* echo */
    }
}

/* ======================================================================
 * Public
 * ====================================================================== */
void cli_init(UART_HandleTypeDef *huart)
{
    cli_uart = huart;
    cli_printf("\r\nstm32 test console, type 'help'\r\n" PROMPT);
    prompt_visible = true;
    ready_reported = ready_count;
    HAL_UART_Receive_IT(cli_uart, &rx_byte, 1);
}

void cli_poll(void)
{
    while (rx_tail != rx_head) {
        char c = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) & (RX_BUF_SIZE - 1);
        handle_char(c);
    }

    uint32_t rc = ready_count;         /* one read: atomic on Cortex-M */
    if (rc != ready_reported) {
        uint32_t n = rc - ready_reported;
        ready_reported = rc;
        if (n == 1) cli_async("[irq] fpga ready");
        else        cli_async("[irq] fpga ready (x%lu)", (unsigned long)n);
    }

    job_poll();
}