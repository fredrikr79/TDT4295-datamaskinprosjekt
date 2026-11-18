#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

uint32_t plat_millis(void);
/* Microseconds since start, for timing inside a frame (frame_sync.c).
 * Wraps after 71 minutes: compare differences, never absolute values.
 * STM32: the 1 ms tick plus how far SysTick is into the current one, so it
 * runs with or without a debugger attached. */
uint32_t plat_micros(void);
void plat_led_pins(char *buf, size_t n);
bool     plat_fpga_ready_pin(void);

/* Terminate the program. Returns false on targets where that makes no
 * sense (there is nothing to return to on bare metal). */
bool plat_exit(int code);

void plat_fpga_reset(bool hold);   /* true: reset pin low */
void plat_mcu_reset(void);         /* doesn't return */

/* Why the MCU last started, from the reset flags the chip keeps through a
 * reset (STM32: RCC->CSR). Read once, then the flags are cleared so the
 * next start only sees its own. The host is always PLAT_RESET_POWER.
 *   POWER     power-up or a brown-out: the FPGA started with us
 *   PIN       only the NRST pin: the reset button, not the FPGA
 *   SOFTWARE  NVIC_SystemReset(): 'reset mcu', or the ST-LINK after flashing
 *   WATCHDOG  the independent or window watchdog
 * (The chip also pulls NRST low for its own resets, so PIN counts only when
 * nothing else is flagged.) */
typedef enum {
    PLAT_RESET_POWER, PLAT_RESET_PIN, PLAT_RESET_SOFTWARE,
    PLAT_RESET_WATCHDOG, PLAT_RESET_OTHER
} plat_reset_t;
plat_reset_t plat_reset_cause(void);
static inline const char *plat_reset_name(plat_reset_t r)
{
    static const char *const names[] = { "power-up", "reset pin (button)", "software reset",
                                         "watchdog", "other" };
    return (unsigned)r < sizeof names / sizeof names[0] ? names[r] : "?";
}

void plat_led_rgb(bool r, bool g, bool b);
bool plat_fpga_done(void);          /* true: the FPGA has loaded its design */
void plat_fpga_program(bool hold);  /* true: PROGRAM_B low (wipes the FPGA) */
bool plat_button_pressed(void);     /* true once per press                  */

bool plat_button_down(void);
void plat_button_counts(uint32_t *edges, uint32_t *presses);