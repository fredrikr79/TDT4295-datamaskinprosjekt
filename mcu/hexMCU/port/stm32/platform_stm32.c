#include "platform.h"
#include "transport.h"
#include "main.h"

uint32_t plat_millis(void)
{
    return HAL_GetTick();
}

void plat_led_toggle(void)
{
    HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
}

bool plat_fpga_ready_pin(void)
{
    return HAL_GPIO_ReadPin(FPGA_READY_GPIO_Port, FPGA_READY_Pin) == GPIO_PIN_SET;
}

/* FPGA_READY rising edge.
 *
 * HAL declares this __weak, so this strong definition wins at link time and
 * Core/Src/stm32u5xx_it.c stays untouched by us (it is regenerated).
 *
 * U5 splits the old HAL_GPIO_EXTI_Callback into _Rising_ and _Falling_
 * variants. A misspelled name still compiles and just never fires, so if
 * `readrdy` shows the pin going high while the irq count stays at 0, check
 * this name against stm32u5xx_hal_gpio.h first.
 *
 * Counting only -- no logging from an ISR. */
void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin)
{
    if (pin == FPGA_READY_Pin) transport_ready_isr();
}

bool plat_exit(int code)
{
    (void)code;
    return false;
}