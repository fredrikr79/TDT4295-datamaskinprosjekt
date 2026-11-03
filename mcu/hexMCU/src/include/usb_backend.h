#ifndef USB_BACKEND_H
#define USB_BACKEND_H

/* The contract between usb_input.c and a backend. Only usb_input.c and the
 * backend files include this; the game uses usb_input.h. */

#include <stdbool.h>
#include <stdint.h>

typedef enum { USB_KEYBOARD, USB_MOUSE, USB_GAMEPAD } usb_device_kind_t;

/* A controller's state, already in a common layout: the backend translates
 * from whatever the controller sends. */
typedef struct {
    uint32_t buttons;           /* PAD_* bits (usb_input.h) */
    int8_t   lx, ly, rx, ry;    /* sticks, 0 = centre, +x = right, +y = down */
    uint8_t  lt, rt;            /* analog triggers, 0 = released */
} usb_gamepad_t;

/* ---- backend -> usb_input.c ---------------------------------------------
 * Keyboard and mouse reports are HID boot protocol, exactly as a real
 * device sends them:
 *   keyboard: 8 bytes  { modifier, reserved, key[6] }
 *   mouse:    3-5 bytes { buttons, x, y, [wheel], [pan] }  (x..pan signed)
 * A mouse in report protocol (needed for the wheel) is decoded by the
 * backend and handed over with usb_input_mouse_motion() instead.
 * Controllers are numbered 0 .. INPUT_PADS-1 by the backend. */
void usb_input_keyboard_report(const uint8_t *report, uint16_t len);
void usb_input_mouse_report(const uint8_t *report, uint16_t len);
void usb_input_mouse_motion(uint8_t buttons, int32_t dx, int32_t dy, int32_t wheel, int32_t pan);
void usb_input_connected(usb_device_kind_t kind, bool connected);   /* keyboards and mice */
void usb_input_gamepad(uint8_t pad, const usb_gamepad_t *state);
void usb_input_gamepad_connected(uint8_t pad, bool connected);

/* ---- usb_input.c -> backend ---------------------------------------------
 * Implemented by exactly one backend:
 *   usb_backend_tinyusb.c  (MCU)
 *   usb_backend_sdl.c      (host) */
void usb_backend_init(void);
void usb_backend_poll(void);

#endif