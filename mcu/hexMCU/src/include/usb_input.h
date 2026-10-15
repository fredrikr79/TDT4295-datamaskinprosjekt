#ifndef USB_INPUT_H
#define USB_INPUT_H

/* Keyboard + mouse input for the game.
 *
 *   main:       usb_init() once, then usb_poll() every loop iteration
 *   run_frame:  input_take(&in) once per frame, then key_held(&in, KEY_W) ...
 *
 * The backend (TinyUSB on the MCU, SDL on the host) is picked by which
 * usb_backend_*.c file is linked in. Everything here is the same for both. */

#include <stdbool.h>
#include <stdint.h>

/* Key codes are USB HID usage IDs (keyboard page 0x07). They name physical
 * key positions with US labels: on a Norwegian keyboard, Å is KEY_LBRACKET,
 * Ø is KEY_SEMICOLON and Æ is KEY_APOSTROPHE. */
enum {
    KEY_A = 0x04, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
    KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S,
    KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,                /* 0x1D */
    KEY_1 = 0x1E, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_0,                                                          /* 0x27 */
    KEY_ENTER = 0x28, KEY_ESCAPE, KEY_BACKSPACE, KEY_TAB, KEY_SPACE,
    KEY_MINUS = 0x2D, KEY_EQUAL, KEY_LBRACKET, KEY_RBRACKET, KEY_BACKSLASH,
    KEY_NONUS_HASH, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_GRAVE, KEY_COMMA,
    KEY_PERIOD, KEY_SLASH, KEY_CAPSLOCK,                            /* 0x39 */
    KEY_F1 = 0x3A, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8,
    KEY_F9, KEY_F10, KEY_F11, KEY_F12,                              /* 0x45 */
    KEY_RIGHT = 0x4F, KEY_LEFT, KEY_DOWN, KEY_UP,
    /* modifiers are keys too, so key_held(&in, KEY_LSHIFT) works */
    KEY_LCTRL = 0xE0, KEY_LSHIFT, KEY_LALT, KEY_LGUI,
    KEY_RCTRL, KEY_RSHIFT, KEY_RALT, KEY_RGUI,                      /* 0xE7 */
};

/* mouse button bits, as in the HID boot report */
enum {
    MOUSE_LEFT    = 0x01,
    MOUSE_RIGHT   = 0x02,
    MOUSE_MIDDLE  = 0x04,
    MOUSE_BACK    = 0x08,
    MOUSE_FORWARD = 0x10,
};

/* One frame's worth of input. "pressed"/"released" collect everything since
 * the previous input_take(), so a tap shorter than a frame still shows up
 * as pressed (and released) even though it is no longer held. */
typedef struct {
    uint8_t held[32];           /* bit per key code: down right now   */
    uint8_t pressed[32];        /* went down since the last take      */
    uint8_t released[32];       /* went up since the last take        */

    uint8_t mouse_held;         /* MOUSE_* bits                       */
    uint8_t mouse_pressed;
    uint8_t mouse_released;
    int32_t mouse_dx, mouse_dy; /* movement since the last take; +y = down */
    int32_t wheel, pan;         /* +wheel = scrolled up/away          */

    bool    keyboard, mouse;    /* at least one is connected          */
} input_t;

void usb_init(void);
void usb_poll(void);            /* runs the backend; call it often    */
void input_take(input_t *out);  /* snapshot + reset the per-frame parts */

/* Debug: log what changed in a snapshot (keys and buttons down/up, wheel,
 * connect/disconnect). Mouse movement is added up and logged at most
 * 10 times a second. Call it with each snapshot you take. */
void input_log(const input_t *in);
const char *key_name(uint8_t key);  /* "W", "SPACE", "LSHIFT", "0x47" ... */

static inline bool input_bit_(const uint8_t *map, uint8_t key)
{
    return (map[key >> 3] >> (key & 7)) & 1u;
}
static inline bool key_held(const input_t *in, uint8_t key)     { return input_bit_(in->held, key); }
static inline bool key_pressed(const input_t *in, uint8_t key)  { return input_bit_(in->pressed, key); }
static inline bool key_released(const input_t *in, uint8_t key) { return input_bit_(in->released, key); }

static inline bool mouse_held(const input_t *in, uint8_t b)     { return (in->mouse_held & b) != 0; }
static inline bool mouse_pressed(const input_t *in, uint8_t b)  { return (in->mouse_pressed & b) != 0; }
static inline bool mouse_released(const input_t *in, uint8_t b) { return (in->mouse_released & b) != 0; }

#endif