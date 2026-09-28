#pragma once
#include <stdint.h>
#include <stdbool.h>

uint32_t plat_millis(void);
void     plat_led_toggle(void);
bool     plat_fpga_ready_pin(void);

/* Terminate the program. Returns false on targets where that makes no
 * sense (there is nothing to return to on bare metal). */
bool plat_exit(int code);