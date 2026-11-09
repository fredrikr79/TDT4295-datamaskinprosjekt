/* Platform layer for the N6 test build: just the millisecond clock, which
 * is all log.c, usb_input.c and the USB code use. HAL's tick comes from
 * SysTick (CubeMX: SYS -> Timebase = SysTick). */

#include "main.h"
#include "platform.h"

uint32_t plat_millis(void)
{
    return HAL_GetTick();
}
