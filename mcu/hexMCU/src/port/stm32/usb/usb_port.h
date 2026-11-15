#ifndef USB_PORT_H
#define USB_PORT_H

#include <stdbool.h>
 
/* Chip/board side of the USB host: everything TinyUSB doesn't do itself
 * (clocks, power, pins, the IRQ handler, VBUS). usb_backend_tinyusb.c calls
 * these; exactly one usb_port_<chip>.c is linked in per board. */
 
void usb_port_init(void);   /* before tusb_init(): clocks, power domain, pins */
void usb_port_poll(void);   /* every loop: VBUS on/off, attach and fault handling */
void usb_port_dump(void);   /* debug: log the controller's state (optional, see usb_debug_dump) */

/* Turn VBUS off, as if everything was unplugged; usb_port_poll() turns it
 * on again after the usual minimum off time. For devices that are stuck
 * until replugged. false: VBUS isn't ours to switch (no switch on this
 * board, or it isn't on). Optional: usb_backend_tinyusb.c has a stand-in
 * that returns false, for ports without it. */
bool usb_port_power_cycle(void);
 
#endif