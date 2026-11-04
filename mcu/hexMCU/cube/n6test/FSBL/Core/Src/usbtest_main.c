/* N6 USB host test (Nucleo-N657X0-Q, FSBL, development boot).
 *
 * Called from main.c (USER CODE BEGIN 3, after the CubeMX and BSP init)
 * and never returns. Logs connects, keys, buttons and mouse movement over
 * the ST-LINK virtual COM port (USART1, 115200 8N1). The green LED blinks
 * while the loop runs, so a hang is easy to spot.
 *
 * Also logs the deepest stack use so far whenever it grows: if it gets
 * close to the stack size, raise CubeMX's Minimum Stack Size. A crash
 * prints its own report (fault.c).
 *
 * Only this file and platform_n6.c are N6-only. The USB code itself is the
 * same as in the U545 / U595 builds. */

#define LOG_TAG "usbtest"

#include <stdbool.h>

#include "main.h"               /* HAL + BSP (BSP_LED_*, BSP_PB_*) */
#include "fault.h"
#include "io.h"
#include "log.h"
#include "platform.h"
#include "usb_input.h"

void usbtest_run(void);
void usb_debug_dump(void);      /* usb_backend_tinyusb.c */

#define FRAME_MS 10u            /* how often input is taken and logged */
#define BLINK_MS 500u
#define STACK_MS 1000u          /* how often stack use is checked       */
#define STACK_STEP 64u          /* log when it has grown this much      */

void usbtest_run(void)
{
    stack_paint();
    io_init();
    LOG_INFO("N6 USB host test, CPU %lu MHz, stack %lu bytes",
             (unsigned long)(SystemCoreClock / 1000000u), (unsigned long)stack_size());

    usb_init();

    LOG_INFO("press the blue button (B1) to log the USB state");

    uint32_t frame = 0, blink = 0, stack_check = 0, stack_logged = 0;
    bool     button_was = false;
    for (;;) {
        usb_poll();

        /* B1: dump the USB state. Polled, so it works whatever the button's
         * interrupt is wired to; 10 ms per frame is plenty of debounce. */
        bool button = BSP_PB_GetState(BUTTON_USER) != 0;
        if (button && !button_was) usb_debug_dump();
        button_was = button;

        uint32_t now = plat_millis();
        if (now - frame >= FRAME_MS) {
            input_t in;
            input_take(&in);
            input_log(&in);
            frame = now;
        }
        if (now - blink >= BLINK_MS) {
            BSP_LED_Toggle(LED_GREEN);
            blink = now;
        }
        if (now - stack_check >= STACK_MS) {
            uint32_t used = stack_used();
            if (used >= stack_logged + STACK_STEP) {
                LOG_INFO("stack: deepest %lu of %lu bytes", (unsigned long)used,
                         (unsigned long)stack_size());
                stack_logged = used;
            }
            stack_check = now;
        }
    }
}