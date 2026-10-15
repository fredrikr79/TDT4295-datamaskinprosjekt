#ifndef USB_BACKEND_H
#define USB_BACKEND_H

/* The contract between usb_input.c and a backend. Only usb_input.c and the
 * backend files include this; the game uses usb_input.h. */

#include <stdbool.h>
#include <stdint.h>

typedef enum { USB_KEYBOARD, USB_MOUSE } usb_device_kind_t;

/* ---- backend -> usb_input.c ---------------------------------------------
 * Reports are HID boot protocol, exactly as a real device sends them:
 *   keyboard: 8 bytes  { modifier, reserved, key[6] }
 *   mouse:    3-5 bytes { buttons, x, y, [wheel], [pan] }  (x..pan signed) */
void usb_input_keyboard_report(const uint8_t *report, uint16_t len);
void usb_input_mouse_report(const uint8_t *report, uint16_t len);
void usb_input_connected(usb_device_kind_t kind, bool connected);

/* ---- usb_input.c -> backend ---------------------------------------------
 * Implemented by exactly one backend:
 *   usb_backend_tinyusb.c  (MCU)
 *   usb_backend_sdl.c      (host) */
void usb_backend_init(void);
void usb_backend_poll(void);

#endif
