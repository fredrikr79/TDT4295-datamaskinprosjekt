#define LOG_TAG "platform"

#include "log.h"
#include "platform.h"
#include "transport.h"
#include "frame_sync.h"
#include "main.h"

uint32_t plat_millis(void)
{
    return HAL_GetTick();
}

/* ---- LED ------------------------------------------------------------------
 * Nucleo: the green LED (high = on). PCB: an RGB LED with a common anode on
 * 3V3, so each colour lights when its pin is LOW (LED_R / LED_G / LED_B,
 * initial level High = off in CubeMX). */
void plat_led_toggle(void)
{
#if defined(LED_GREEN_Pin)                  /* Nucleo */
    HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
#elif defined(LED_R_Pin)                    /* PCB */
    HAL_GPIO_TogglePin(LED_R_GPIO_Port, LED_R_Pin);
#endif
}

/* Each colour on or off. The Nucleo has only green: it shows "any colour". */
void plat_led_rgb(bool r, bool g, bool b)
{
#if defined(LED_R_Pin)
    HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, r ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, g ? GPIO_PIN_RESET : GPIO_PIN_SET);
    HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, b ? GPIO_PIN_RESET : GPIO_PIN_SET);
#elif defined(LED_GREEN_Pin)
    HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, (r || g || b) ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
    (void)r; (void)g; (void)b;
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

static volatile bool button_pressed;

static void button_edge(void)
{
    static uint32_t last;
    uint32_t now = HAL_GetTick();
    if (now - last < BUTTON_DEBOUNCE_MS) return;
    last = now;
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

/* ---- misc ------------------------------------------------------------------ */
bool plat_exit(int code)
{
    (void)code;
    return false;
}

void plat_mcu_reset(void) { NVIC_SystemReset(); }