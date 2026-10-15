#ifndef USB_PORT_H
#define USB_PORT_H
 
/* Chip/board side of the USB host: everything TinyUSB doesn't do itself
 * (clocks, power, pins, the IRQ handler, VBUS). usb_backend_tinyusb.c calls
 * these; exactly one usb_port_<chip>.c is linked in per board. */
 
void usb_port_init(void);   /* before tusb_init(): clocks, power domain, pins */
void usb_port_poll(void);   /* every loop: VBUS on/off, attach and fault handling */
 
#endif
