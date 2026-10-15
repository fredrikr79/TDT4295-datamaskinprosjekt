/* Translation layer: HID boot reports in, per-frame input_t out.
 *
 * Both backends hand over raw boot reports (the SDL backend fakes them), so
 * this file is what actually runs on the MCU even when testing on the host.
 *
 * Everything here runs in the main loop: the backend calls the report
 * functions from inside usb_poll() (TinyUSB callbacks run in tuh_task(),
 * not in the interrupt), so no locking is needed.
 *
 * Limitation: all keyboards share one key state. Two keyboards typing at
 * once would fight; one keyboard and one mouse is the expected setup. */

#define LOG_TAG "input"

#include <stdio.h>
#include <string.h>

#include "usb_input.h"
#include "usb_backend.h"
#include "log.h"
#include "platform.h"

#define KEYMAP_BYTES 32u
#define BIT_SET(map, k) ((map)[(k) >> 3] |= (uint8_t)(1u << ((k) & 7u)))

static struct {                 /* what the devices say right now */
    uint8_t held[KEYMAP_BYTES];
    uint8_t mouse_held;
    uint8_t keyboards, mice;    /* how many are connected */
} cur;

static struct {                 /* collected since the last input_take() */
    uint8_t pressed[KEYMAP_BYTES], released[KEYMAP_BYTES];
    uint8_t mouse_pressed, mouse_released;
    int32_t dx, dy, wheel, pan;
} acc;

void usb_init(void)
{
    memset(&cur, 0, sizeof cur);
    memset(&acc, 0, sizeof acc);
    usb_backend_init();
}

void usb_poll(void)
{
    usb_backend_poll();         /* reports arrive through the functions below */
}

void input_take(input_t *out)
{
    memcpy(out->held,     cur.held,     KEYMAP_BYTES);
    memcpy(out->pressed,  acc.pressed,  KEYMAP_BYTES);
    memcpy(out->released, acc.released, KEYMAP_BYTES);
    out->mouse_held     = cur.mouse_held;
    out->mouse_pressed  = acc.mouse_pressed;
    out->mouse_released = acc.mouse_released;
    out->mouse_dx       = acc.dx;
    out->mouse_dy       = acc.dy;
    out->wheel          = acc.wheel;
    out->pan            = acc.pan;
    out->keyboard       = cur.keyboards > 0;
    out->mouse          = cur.mice > 0;
    memset(&acc, 0, sizeof acc);
}

/* ---- keyboard ------------------------------------------------------------ */
static void keyboard_set(const uint8_t now[KEYMAP_BYTES])
{
    for (unsigned i = 0; i < KEYMAP_BYTES; i++) {
        acc.pressed[i]  |= (uint8_t)(now[i] & ~cur.held[i]);
        acc.released[i] |= (uint8_t)(cur.held[i] & ~now[i]);
        cur.held[i] = now[i];
    }
}

void usb_input_keyboard_report(const uint8_t *r, uint16_t len)
{
    uint8_t now[KEYMAP_BYTES] = { 0 };
    if (len < 8) return;

    /* 0x01..0x03 in the key slots: rollover (too many keys) or an error.
     * The report says nothing useful then, so keep the last state. */
    for (unsigned i = 2; i < 8; i++)
        if (r[i] >= 0x01 && r[i] <= 0x03) return;

    /* modifier bit n is key 0xE0 + n */
    for (unsigned b = 0; b < 8; b++)
        if (r[0] & (1u << b)) BIT_SET(now, KEY_LCTRL + b);

    for (unsigned i = 2; i < 8; i++)
        if (r[i]) BIT_SET(now, r[i]);

    keyboard_set(now);
}

/* ---- mouse --------------------------------------------------------------- */
static void mouse_set_buttons(uint8_t b)
{
    acc.mouse_pressed  |= (uint8_t)(b & ~cur.mouse_held);
    acc.mouse_released |= (uint8_t)(cur.mouse_held & ~b);
    cur.mouse_held = b;
}

void usb_input_mouse_report(const uint8_t *r, uint16_t len)
{
    if (len < 3) return;
    mouse_set_buttons(r[0]);
    acc.dx += (int8_t)r[1];
    acc.dy += (int8_t)r[2];
    if (len >= 4) acc.wheel += (int8_t)r[3];
    if (len >= 5) acc.pan   += (int8_t)r[4];
}

/* ---- plug / unplug ---------------------------------------------------------
 * Unplugging releases everything, so no key stays stuck down. */
void usb_input_connected(usb_device_kind_t kind, bool connected)
{
    static const uint8_t none[KEYMAP_BYTES];

    if (kind == USB_KEYBOARD) {
        if (connected) {
            cur.keyboards++;
        } else if (cur.keyboards) {
            cur.keyboards--;
            keyboard_set(none);
        }
    } else {
        if (connected) {
            cur.mice++;
        } else if (cur.mice) {
            cur.mice--;
            mouse_set_buttons(0);
        }
    }
}

/* ---- debug logging ---------------------------------------------------------- */
#define MOVE_LOG_MS 100u

const char *key_name(uint8_t key)
{
    static const char *const special[] = {     /* 0x28 .. 0x39 */
        "ENTER", "ESC", "BACKSPACE", "TAB", "SPACE", "-", "=", "[", "]", "\\",
        "#", ";", "'", "`", ",", ".", "/", "CAPSLOCK",
    };
    static const char *const arrows[] = { "RIGHT", "LEFT", "DOWN", "UP" };
    static const char *const mods[] = {
        "LCTRL", "LSHIFT", "LALT", "LGUI", "RCTRL", "RSHIFT", "RALT", "RGUI",
    };
    static char buf[8];

    if (key >= KEY_A && key <= KEY_Z)  { buf[0] = (char)('A' + key - KEY_A); buf[1] = 0; return buf; }
    if (key >= KEY_1 && key <= KEY_9)  { buf[0] = (char)('1' + key - KEY_1); buf[1] = 0; return buf; }
    if (key == KEY_0)                  return "0";
    if (key >= KEY_ENTER && key <= KEY_CAPSLOCK) return special[key - KEY_ENTER];
    if (key >= KEY_F1 && key <= KEY_F12) { snprintf(buf, sizeof buf, "F%d", key - KEY_F1 + 1); return buf; }
    if (key >= KEY_RIGHT && key <= KEY_UP) return arrows[key - KEY_RIGHT];
    if (key >= KEY_LCTRL && key <= KEY_RGUI) return mods[key - KEY_LCTRL];
    snprintf(buf, sizeof buf, "0x%02X", key);
    return buf;
}

static const char *button_name(uint8_t bit)
{
    switch (bit) {
    case MOUSE_LEFT:    return "LEFT";
    case MOUSE_RIGHT:   return "RIGHT";
    case MOUSE_MIDDLE:  return "MIDDLE";
    case MOUSE_BACK:    return "BACK";
    case MOUSE_FORWARD: return "FORWARD";
    default:            return "?";
    }
}

/* "W LSHIFT" for every bit set in a key map; returns the length */
static int list_keys(const uint8_t map[32], char *out, size_t size)
{
    int n = 0;
    out[0] = 0;
    for (unsigned k = 0; k < 256 && (size_t)n < size; k++)
        if (map[k >> 3] & (1u << (k & 7)))
            n += snprintf(out + n, size - (size_t)n, "%s%s", n ? " " : "", key_name((uint8_t)k));
    return n;
}

void input_log(const input_t *in)
{
    static bool     was_kb, was_mouse;
    static int32_t  dx, dy;
    static uint32_t last_move;
    char list[128];

    if (in->keyboard != was_kb)   LOG_INFO("keyboard %s", in->keyboard ? "connected" : "gone");
    if (in->mouse != was_mouse)   LOG_INFO("mouse %s",    in->mouse    ? "connected" : "gone");
    was_kb    = in->keyboard;
    was_mouse = in->mouse;

    if (list_keys(in->pressed, list, sizeof list)) {
        char held[128];
        list_keys(in->held, held, sizeof held);
        LOG_INFO("down: %-12s held: %s", list, held);
    }
    if (list_keys(in->released, list, sizeof list))
        LOG_INFO("up:   %s", list);

    for (uint8_t b = 1; b && b <= MOUSE_FORWARD; b = (uint8_t)(b << 1)) {
        if (in->mouse_pressed & b)  LOG_INFO("mouse %s down", button_name(b));
        if (in->mouse_released & b) LOG_INFO("mouse %s up",   button_name(b));
    }
    if (in->wheel) LOG_INFO("wheel %+ld", (long)in->wheel);
    if (in->pan)   LOG_INFO("pan %+ld",   (long)in->pan);

    dx += in->mouse_dx;
    dy += in->mouse_dy;
    uint32_t now = plat_millis();
    if ((dx || dy) && now - last_move >= MOVE_LOG_MS) {
        LOG_INFO("move %+ld,%+ld", (long)dx, (long)dy);
        dx = dy = 0;
        last_move = now;
    }
}