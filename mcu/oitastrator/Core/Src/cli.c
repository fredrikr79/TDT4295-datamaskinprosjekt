#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/_types.h>
#include "main.h"
#include "stm32u5xx_hal_def.h"
#include "transport.h"

#define RX_BUF_SIZE  64          /* must be power of two */
#define LINE_MAX     64
#define MAX_ARGS     8

static UART_HandleTypeDef *cli_uart;
static uint8_t  rx_byte;
static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head, rx_tail;

static void cli_printf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof buf) n = sizeof buf - 1;
    HAL_UART_Transmit(cli_uart, (uint8_t *)buf, n, 100);
}

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

#define SEND_READY_TIMEOUT_MS  100   /* waiting for backend to be free */
#define SEND_DONE_TIMEOUT_MS   100   /* waiting for transfer to finish */

typedef enum { SEND_IDLE, SEND_WAIT_READY, SEND_WAIT_DONE } send_state_t;
typedef enum { IDLE, WAIT_READY, WAIT_DONE } cmd_state_t;
typedef enum { NONE, SEND, READ } curr_cmd;

static send_state_t send_state = SEND_IDLE;
static cmd_state_t state = IDLE;
static curr_cmd     cmd = NONE;
static uint32_t     send_t0;
static uint8_t      send_opcode;

static void send_finish(const char *msg)
{
    cli_printf("%s\r\n> ", msg);
    send_state = SEND_IDLE;
    cmd = NONE;
}

static void send_poll(void)
{
    uint32_t elapsed = HAL_GetTick() - send_t0;   /* wrap-safe */

    switch (send_state) {
    case SEND_IDLE:
        break;

    case SEND_WAIT_READY:
        if (ospi_backend.done) {
            transport_hdr_t hdr = {0};
            hdr.opcode = send_opcode;
            if (ospi_backend.write(&hdr, NULL, 0) != HAL_OK) {
                send_finish("send error: write did not start");
            } else {
                send_t0    = HAL_GetTick();
                send_state = SEND_WAIT_DONE;
            }
        } else if (elapsed >= SEND_READY_TIMEOUT_MS) {
            send_finish("send error: backend not ready (timeout)");
        }
        break;

    case SEND_WAIT_DONE:
        if (ospi_backend.done) {
            char msg[24];
            snprintf(msg, sizeof msg, "sent 0x%02X", send_opcode);
            send_finish(msg);
        } else if (elapsed >= SEND_DONE_TIMEOUT_MS) {
            send_finish("send error: transfer never completed");
            /* later: abort the OSPI transfer / reset backend here */
        }
        break;

    }
}

static bool send_start(uint8_t opcode)
{
    if (send_state != SEND_IDLE) return false;   /* one at a time */
    send_opcode = opcode;
    send_t0     = HAL_GetTick();
    send_state  = SEND_WAIT_READY;
    cmd = SEND;
    send_poll();                                 /* try immediately */
    return true;
}   

static bool read_start(uint8_t opcode, uint16_t len){
    return true;
}



typedef void (*cmd_fn)(int argc, char **argv);
typedef struct { const char *name; cmd_fn fn; const char *help; } cli_cmd_t;

static void cmd_help(int argc, char **argv);

static void cmd_toggle(int argc, char **argv)
{
    (void)argc; (void)argv;
    HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);  /* your CubeMX LED label */
    cli_printf("led toggled\r\n");
}

static void cmd_send(int argc, char **argv)
{
    if (argc != 2) { cli_printf("usage: send <opcode>\r\n"); return; }

    char *end;
    unsigned long v = strtoul(argv[1], &end, 0);  /* base 0: "0xff" or "255" */
    if (*end != '\0' || v > 0xFF) {
        cli_printf("bad opcode '%s'\r\n", argv[1]);
        return;
    }
    if (!send_start((uint8_t)v)) cli_printf("send error: busy\r\n");
}

static void cmd_read(int argc, char **argv)
{
    if (argc != 3) { cli_printf("usage: send <opcode> <len>\r\n"); return; }

    char *end;
    unsigned long v = strtoul(argv[1], &end, 0);  /* base 0: "0xff" or "255" */
    if (*end != '\0' || v > 0xFF) {
        cli_printf("bad opcode '%s'\r\n", argv[1]);
        return;
    }
    uint16_t len = strtoul(argv[1], &end, 0);
    if (*end != '\0' || v > 0xFF) {
        cli_printf("bad len '%s'\r\n", argv[1]);
        return;
    }
    if (!read_start((uint8_t)v, len)) cli_printf("send error: busy\r\n");
}

static const cli_cmd_t cmds[] = {
    { "help",   cmd_help,   "list commands" },
    { "toggle", cmd_toggle, "toggle user LED" },
    { "send",   cmd_send,   "send <opcode>, e.g. send 0xff" },
    { "read",   cmd_read,   "read <opcode>, e.g. send 0xff <len>"}
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

static char     line[LINE_MAX];
static uint16_t line_len;

static void handle_char(char c)
{
    static char prev;
    if (c == '\n' && prev == '\r') { prev = c; return; }   /* swallow CRLF pairs */
    prev = c;

    if (c == '\r' || c == '\n') {
        cli_printf("\r\n");
        line[line_len] = '\0';
        dispatch(line);
        line_len = 0;
        if (send_state == SEND_IDLE) cli_printf("> ");  /* else send_finish prints it */
    } else if (c == '\b' || c == 0x7F) {                 /* backspace / DEL */
        if (line_len) { line_len--; cli_printf("\b \b"); }
    } else if (c >= 0x20 && c < 0x7F && line_len < LINE_MAX - 1) {
        line[line_len++] = c;
        HAL_UART_Transmit(cli_uart, (uint8_t *)&c, 1, 10);   /* echo */
    }
}

void cli_init(UART_HandleTypeDef *huart)
{
    cli_uart = huart;
    cli_printf("\r\nstm32 test console, type 'help'\r\n> ");
    HAL_UART_Receive_IT(cli_uart, &rx_byte, 1);
}

void cli_poll(void)
{
    while (rx_tail != rx_head) {
        char c = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) & (RX_BUF_SIZE - 1);
        handle_char(c);
    }
    if(cmd == SEND)
     send_poll();

}