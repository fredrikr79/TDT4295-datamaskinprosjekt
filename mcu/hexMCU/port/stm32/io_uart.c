#include "io.h"
#include "main.h"

#define RX_BUF_SIZE 64   /* power of two */

extern UART_HandleTypeDef huart1;   /* whatever CubeMX named yours */
static UART_HandleTypeDef *u = &huart1;

static uint8_t  rx_byte;
static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint16_t rx_head, rx_tail;

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != u) return;
    uint16_t next = (rx_head + 1) & (RX_BUF_SIZE - 1);
    if (next != rx_tail) { rx_buf[rx_head] = rx_byte; rx_head = next; }
    HAL_UART_Receive_IT(u, &rx_byte, 1);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == u) HAL_UART_Receive_IT(u, &rx_byte, 1);
}

void io_init(void) { HAL_UART_Receive_IT(u, &rx_byte, 1); }

void io_write(const char *buf, uint16_t n)
{
    if (n) HAL_UART_Transmit(u, (uint8_t *)buf, n, 100);
}

int io_getc(void)
{
    if (rx_tail == rx_head) return -1;
    int c = rx_buf[rx_tail];
    rx_tail = (rx_tail + 1) & (RX_BUF_SIZE - 1);
    return c;
}