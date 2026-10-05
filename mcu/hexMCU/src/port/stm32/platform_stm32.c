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
    HAL_GPIO_TogglePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin);
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
void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin)
{
    if (pin == FPGA_READY_Pin) transport_ready_isr();
#ifdef FPGA_SYNC_Pin
    if (pin == FPGA_SYNC_Pin)  frame_sync_isr();
#endif
    if (pin == B1_Pin) LOG_INFO("Button pressed!");
}

bool plat_exit(int code)
{
    (void)code;
    return false;
}