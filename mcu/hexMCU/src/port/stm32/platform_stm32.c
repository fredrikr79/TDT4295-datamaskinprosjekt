#define LOG_TAG "platform"

#include "log.h"
#include "platform.h"
#include "transport.h"
#include "frame_sync.h"
#include "main.h"

#include <stdio.h>
#include <string.h>

uint32_t plat_millis(void)
{
    return HAL_GetTick();
}

/* ---- LED ------------------------------------------------------------------
 * Only led.c calls these. Nucleo: the green LED (high = on). PCB: an RGB LED
 * with a common anode on 3V3, so each colour lights when its pin is LOW.
 *
 * The pins come from CubeMX's user labels, spelled exactly LED_R, LED_G,
 * LED_B (PCB) or LED_GREEN (Nucleo): main.h then has LED_R_Pin and so on.
 * Any other spelling and this file wouldn't see the pins, so that's a build
 * error instead of an LED that silently does nothing. */
#if defined(LED_R_Pin) && defined(LED_G_Pin) && defined(LED_B_Pin)
#define LED_IS_RGB 1
#elif defined(LED_GREEN_Pin)
#define LED_IS_RGB 0
#else
#error "No status LED pins in main.h: label them LED_R, LED_G, LED_B in CubeMX (PCB) or LED_GREEN (Nucleo)"
#endif

/* Each colour on or off. The Nucleo has only green: it shows "any colour". */
void plat_led_rgb(bool r, bool g, bool b)
{
#if LED_IS_RGB
    HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, r ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, g ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, b ? GPIO_PIN_RESET : GPIO_PIN_SET);
#else
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, (r || g || b) ? GPIO_PIN_SET : GPIO_PIN_RESET);
#endif
}

/* "led pins": what the LED pins really are, read back from the GPIO
 * registers, to tell a software problem from a board problem. Per pin: its
 * port and number, its mode, what the MCU drives, and what the pin reads.
 * Driven low, reads low, still dark: the LED, its resistor or its 3V3 side.
 * A port whose clock is off reads all zeros: "input, set low, reads low". */
static void led_pin_line(char *p, size_t n, const char *name, GPIO_TypeDef *port, uint16_t pin)
{
    static const char *const modes[] = { "input", "output", "alternate function", "analog" };
    unsigned i    = (unsigned)__builtin_ctz(pin);
    unsigned mode = (port->MODER >> (2u * i)) & 3u;
    char     pl   = (char)('A' + ((uintptr_t)port - (uintptr_t)GPIOA) / ((uintptr_t)GPIOB - (uintptr_t)GPIOA));
    snprintf(p, n, "  %-9s P%c%-2u %s%s, set %s, reads %s%s\r\n", name, pl, i, modes[mode],
             mode == 1 && ((port->OTYPER >> i) & 1u) ? " (open drain)" : "",
             ((port->ODR >> i) & 1u) ? "high" : "low",
             ((port->IDR >> i) & 1u) ? "high" : "low",
             port->MODER == 0 ? "  (port clock off? all registers read 0)" : "");
}

void plat_led_pins(char *buf, size_t n)
{
    size_t k;
#if LED_IS_RGB
    snprintf(buf, n, "RGB LED, common anode: a colour is on when its pin is LOW\r\n");
    k = strlen(buf); led_pin_line(buf + k, n - k, "LED_R", LED_R_GPIO_Port, LED_R_Pin);
    k = strlen(buf); led_pin_line(buf + k, n - k, "LED_G", LED_G_GPIO_Port, LED_G_Pin);
    k = strlen(buf); led_pin_line(buf + k, n - k, "LED_B", LED_B_GPIO_Port, LED_B_Pin);
#else
    snprintf(buf, n, "Nucleo green LED: on when its pin is HIGH\r\n");
    k = strlen(buf); led_pin_line(buf + k, n - k, "LED_GREEN", LED_GREEN_GPIO_Port, LED_GREEN_Pin);
#endif
}

/* ---- FPGA lines ------------------------------------------------------------ */
bool plat_fpga_ready_pin(void)
{
    return HAL_GPIO_ReadPin(FPGA_READY_GPIO_Port, FPGA_READY_Pin) == GPIO_PIN_SET;
}

/* DONE: high once the FPGA has loaded its design. Without the pin (Nucleo):
 * assume it has. */
bool plat_fpga_done(void)
{
#ifdef FPGA_DONE_Pin
    return HAL_GPIO_ReadPin(FPGA_DONE_GPIO_Port, FPGA_DONE_Pin) == GPIO_PIN_SET;
#else
    return true;
#endif
}

/* PROGRAM_B (open drain): hold = pull it low, which wipes the FPGA's design;
 * release = let go, and it loads the design from its flash again. */
void plat_fpga_program(bool hold)
{
#ifdef FPGA_PROGRAM_B_Pin
    HAL_GPIO_WritePin(FPGA_PROGRAM_B_GPIO_Port, FPGA_PROGRAM_B_Pin, hold ? GPIO_PIN_RESET : GPIO_PIN_SET);
#else
    if (hold) LOG_WARN("no FPGA_PROGRAM_B pin on this board");
#endif
}

void plat_fpga_reset(bool hold)
{
#ifdef FPGA_RESET_Pin
    HAL_GPIO_WritePin(FPGA_RESET_GPIO_Port, FPGA_RESET_Pin, hold ? GPIO_PIN_RESET : GPIO_PIN_SET);
#else
    if (hold) LOG_WARN("no FPGA_RESET pin yet: the FPGA is NOT reset (only the MCU side starts over)");
#endif
}

/* ---- interrupts: FPGA_READY, FPGA_SYNC, the button --------------------------
 * HAL declares these __weak, so these strong definitions win at link time
 * and Core/Src/stm32u5xx_it.c stays untouched by us (it is regenerated).
 * U5 splits the old HAL_GPIO_EXTI_Callback into _Rising_ and _Falling_.
 *
 * READY and SYNC must be on different pin NUMBERS: EXTI line n is shared by
 * pin n of every port, so PA3 and PB3 can't both have an interrupt.
 *
 * The button: Nucleo B1 is active high (press = rising edge), the PCB's
 * USER_BUTTON goes to GND (press = falling edge, pull-up). A press bounces
 * for a few ms, so edges within BUTTON_DEBOUNCE_MS of the last one taken
 * are ignored. No logging in here: that's a blocking UART send. */
#define BUTTON_DEBOUNCE_MS 50u

static volatile bool     button_pressed;
static volatile uint32_t button_edges;      /* every interrupt, bounces too */
static volatile uint32_t button_presses;    /* after the debounce           */

static void button_edge(void)
{
    static uint32_t last;
    uint32_t now = HAL_GetTick();
    button_edges++;
    if (now - last < BUTTON_DEBOUNCE_MS) return;
    last = now;
    button_presses++;
    button_pressed = true;
}

void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin)
{
    if (pin == FPGA_READY_Pin) transport_ready_isr();
#ifdef FPGA_SYNC_Pin
    if (pin == FPGA_SYNC_Pin)  frame_sync_isr();
#endif
#ifdef B1_Pin
    if (pin == B1_Pin) button_edge();                   /* Nucleo: active high */
#endif
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t pin)
{
#ifdef USER_BUTTON_Pin
    if (pin == USER_BUTTON_Pin) button_edge();          /* PCB: to GND, pressed = falling */
#else
    (void)pin;
#endif
}

/* call from the main loop: true once per press */
bool plat_button_pressed(void)
{
    if (!button_pressed) return false;
    button_pressed = false;
    return true;
}

/* For the console's "button" test: the pin right now (true = held down),
 * read straight from the pin, and how many interrupts and presses there
 * have been. A pin that changes with no interrupts = the EXTI side is
 * wrong; a pin that never changes = the button or its wiring. */
bool plat_button_down(void)
{
#if defined(USER_BUTTON_Pin)                /* PCB: to GND, pull-up */
    return HAL_GPIO_ReadPin(USER_BUTTON_GPIO_Port, USER_BUTTON_Pin) == GPIO_PIN_RESET;
#elif defined(B1_Pin)                       /* Nucleo: active high */
    return HAL_GPIO_ReadPin(B1_GPIO_Port, B1_Pin) == GPIO_PIN_SET;
#else
    return false;
#endif
}

void plat_button_counts(uint32_t *edges, uint32_t *presses)
{
    *edges   = button_edges;
    *presses = button_presses;
}

/* ---- misc ------------------------------------------------------------------ */
bool plat_exit(int code)
{
    (void)code;
    return false;
}

void plat_mcu_reset(void) { NVIC_SystemReset(); }