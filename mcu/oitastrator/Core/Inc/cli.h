#ifndef CLI_H
#define CLI_H

#include "main.h"

void cli_init(UART_HandleTypeDef *huart);
void cli_poll(void);               /* call every main-loop iteration */
void cli_fpga_ready_irq(void);     /* ISR-safe: only bumps a counter */

#endif /* CLI_H */
