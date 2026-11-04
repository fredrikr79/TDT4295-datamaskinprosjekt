#ifndef USB_DWC2_DEBUG_H
#define USB_DWC2_DEBUG_H

/* Bring-up help for the dwc2 (Synopsys OTG) host controller, the same on
 * the N6 test board and the U595: counts what the controller reports per
 * device, and dumps that plus the host channel registers on request (B1 on
 * the Nucleo, usb_debug_dump() anywhere). Only reads registers; TinyUSB
 * doesn't know it's there.
 *
 *   OTG_HS_IRQHandler:  dwc2_debug_irq(base);  then tusb_int_handler(0, true);
 *   usb_port_dump():    dwc2_debug_dump(base);
 *
 * base: the controller's register address (USB_OTG_HS_BASE on the U5,
 * USB1_OTG_HS_BASE on the N6). */

#include <stdint.h>

void dwc2_debug_irq(uintptr_t base);
void dwc2_debug_dump(uintptr_t base);

#endif
