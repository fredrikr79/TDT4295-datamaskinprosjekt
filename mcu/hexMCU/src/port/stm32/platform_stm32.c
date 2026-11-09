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

void plat_led_toggle(void)
{
#if defined(LED_GREEN_Pin)                  /* Nucleo */
    HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
#elif defined(LED_R_Pin)                    /* PCB: RGB LED, common anode, low = on */
    HAL_GPIO_TogglePin(LED_R_GPIO_Port, LED_R_Pin);
#endif
}

bool plat_fpga_ready_pin(void)
{
    return HAL_GPIO_ReadPin(FPGA_READY_GPIO_Port, FPGA_READY_Pin) == GPIO_PIN_SET;
}

/* FPGA_READY and FPGA_SYNC rising edges.
 *
 * HAL declares this __weak, so this strong definition wins at link time and
 * Core/Src/stm32u5xx_it.c stays untouched by us (it is regenerated).
 *
 * U5 splits the old HAL_GPIO_EXTI_Callback into _Rising_ and _Falling_
 * variants.
 *
 * FPGA_SYNC_Pin comes from CubeMX once the pin is set up there (GPIO_EXTI,
 * rising edge, user label FPGA_SYNC). Until then this compiles without it.
 * READY and SYNC must be on different pin NUMBERS: EXTI line n is shared by
 * pin n of every port, so PA3 and PB3 can't both have an interrupt.
 */
static volatile bool button_pressed;

void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin)
{
    if (pin == FPGA_READY_Pin) transport_ready_isr();
#ifdef FPGA_SYNC_Pin
    if (pin == FPGA_SYNC_Pin)  frame_sync_isr();
#endif
#ifdef B1_Pin
    if (pin == B1_Pin) button_pressed = true;           /* Nucleo: active high */
#endif
}

void HAL_GPIO_EXTI_Falling_Callback(uint16_t pin)
{
#ifdef USER_BUTTON_Pin
    if (pin == USER_BUTTON_Pin) button_pressed = true;  /* PCB: to GND, pressed = falling */
#endif
}

/* call from the main loop */
bool plat_button_pressed(void)
{
    if (!button_pressed) return false;
    button_pressed = false;
    return true;
}

bool plat_exit(int code)
{
    (void)code;
    return false;
}

void plat_fpga_reset(bool hold)
{
#ifdef FPGA_RESET_Pin
    HAL_GPIO_WritePin(FPGA_RESET_GPIO_Port, FPGA_RESET_Pin, hold ? GPIO_PIN_RESET : GPIO_PIN_SET);
#else
    if (hold) LOG_WARN("no FPGA_RESET pin yet: the FPGA is NOT reset (only the MCU side starts over)");
#endif
}
void plat_mcu_reset(void) { NVIC_SystemReset(); }