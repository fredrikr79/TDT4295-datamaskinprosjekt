/* Host backend: SDL3 events -> fake HID boot reports -> usb_input.c
 *
 * SDL scancodes are the USB HID usage IDs, so a key's scancode is already
 * its KEY_* code. This file keeps a boot keyboard report up to date and
 * sends it on every change, the same way a real keyboard does.
 *
 * Input comes from display_host.c's window (no window of its own here).
 * Mouse: click the window to grab it (relative mode), F12 lets go. The
 * grabbing click and F12 are not passed on to the game.
 *
 * Sharing the SDL event queue: this file takes only keyboard and mouse
 * events out of it (SDL_PeepEvents by type range). display_poll() flushes
 * everything else and must leave those two ranges alone. That way it
 * doesn't matter which of the two polls first. */

#define LOG_TAG "usb-sdl"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <SDL3/SDL.h>

#include "log.h"
#include "usb_backend.h"
#include "usb_input.h"

static uint8_t     kb[8];       /* { modifier, reserved, key[6] } */
static uint8_t     buttons;
static float       rem_x, rem_y, rem_wheel;   /* sub-unit motion carried over */
static SDL_Window *grabbed;     /* window in relative mouse mode, or NULL */
static bool        grab_failed; /* relative mode not supported: don't retry */
static bool        had_focus;

/* ---- keyboard ------------------------------------------------------------ */
static void kb_send(void)
{
    usb_input_keyboard_report(kb, sizeof kb);
}

static void kb_key(SDL_Scancode scancode, bool down)
{
    int sc = (int)scancode;     /* same number as the HID key code */
    if (sc >= KEY_LCTRL && sc <= KEY_RGUI) {
        uint8_t bit = (uint8_t)(1u << (sc - KEY_LCTRL));
        if (down) kb[0] |= bit;
        else      kb[0] &= (uint8_t)~bit;
    } else if (sc >= KEY_A && sc < KEY_LCTRL) {
        /* down: take a free slot. up: clear the key's slot. */
        int slot = -1;
        for (int i = 2; i < 8; i++)
            if (kb[i] == (down ? 0 : sc)) { slot = i; break; }
        if (slot < 0) return;   /* up: wasn't held. down: 7th key, dropped */
        kb[slot] = down ? (uint8_t)sc : 0;
    } else {
        return;                 /* SDL-only keys (media keys etc.) */
    }
    kb_send();
}

/* ---- mouse --------------------------------------------------------------- */
static int8_t clamp8(int v)
{
    return (int8_t)(v < -127 ? -127 : v > 127 ? 127 : v);
}

/* Big movements are split into several reports, like a fast real mouse. */
static void mouse_send(int dx, int dy, int wheel)
{
    do {
        int8_t x = clamp8(dx), y = clamp8(dy), w = clamp8(wheel);
        uint8_t r[4] = { buttons, (uint8_t)x, (uint8_t)y, (uint8_t)w };
        usb_input_mouse_report(r, sizeof r);
        dx -= x; dy -= y; wheel -= w;
    } while (dx || dy || wheel);
}

/* SDL3 motion and wheel are floats (high-resolution mice): send the whole
 * part, keep the fraction for next time so slow movement isn't lost. */
static int take_whole(float *acc, float add)
{
    *acc += add;
    int whole = (int)*acc;      /* rounds toward zero */
    *acc -= (float)whole;
    return whole;
}

static uint8_t button_bit(uint8_t b)
{
    switch (b) {
    case SDL_BUTTON_LEFT:   return MOUSE_LEFT;
    case SDL_BUTTON_RIGHT:  return MOUSE_RIGHT;
    case SDL_BUTTON_MIDDLE: return MOUSE_MIDDLE;
    case SDL_BUTTON_X1:     return MOUSE_BACK;
    case SDL_BUTTON_X2:     return MOUSE_FORWARD;
    default:                return 0;
    }
}

static void ungrab(void)
{
    if (!grabbed) return;
    SDL_SetWindowRelativeMouseMode(grabbed, false);
    grabbed = NULL;
}

/* Focus lost: SDL won't tell us about key-ups any more, so let go of all. */
static void release_all(void)
{
    memset(kb, 0, sizeof kb);
    kb_send();
    buttons = 0;
    rem_x = rem_y = rem_wheel = 0.0f;
    mouse_send(0, 0, 0);
    ungrab();
}

/* ---- events -------------------------------------------------------------- */
static void handle(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (e->key.repeat) break;               /* real keyboards don't repeat */
        if (e->key.scancode == SDL_SCANCODE_F12) {
            if (e->key.down) ungrab();
            break;
        }
        kb_key(e->key.scancode, e->key.down);
        break;

    case SDL_EVENT_MOUSE_MOTION: {
        int dx = take_whole(&rem_x, e->motion.xrel);
        int dy = take_whole(&rem_y, e->motion.yrel);
        if (dx || dy) mouse_send(dx, dy, 0);
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (!grabbed && !grab_failed) {         /* first click only grabs */
            SDL_Window *w = SDL_GetWindowFromID(e->button.windowID);
            if (w && SDL_SetWindowRelativeMouseMode(w, true)) {
                grabbed = w;
                break;
            }
            /* no relative mode here: play without the grab */
            LOG_WARN("can't grab the mouse (%s): clicks go straight through", SDL_GetError());
            grab_failed = true;
        }
        buttons |= button_bit(e->button.button);
        mouse_send(0, 0, 0);
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (!(buttons & button_bit(e->button.button))) break;   /* the grab click */
        buttons &= (uint8_t)~button_bit(e->button.button);
        mouse_send(0, 0, 0);
        break;

    case SDL_EVENT_MOUSE_WHEEL: {
        float y = e->wheel.y;
        if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) y = -y;
        int w = take_whole(&rem_wheel, y);
        if (w) mouse_send(0, 0, w);
        break;
    }

    default:
        break;
    }
}

/* ---- backend ------------------------------------------------------------- */
void usb_backend_init(void)
{
    /* Same hint as display_init(), in case input starts first. Init is
     * reference counted, so whichever of the two comes first is fine. */
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        LOG_ERR("SDL init: %s", SDL_GetError());
        return;
    }
    usb_input_connected(USB_KEYBOARD, true);
    usb_input_connected(USB_MOUSE, true);
    LOG_INFO("input from the display window: click to grab the mouse, F12 to let go");
}

void usb_backend_poll(void)
{
    SDL_Event ev[32];
    int n;

    if (!SDL_WasInit(SDL_INIT_VIDEO)) return;
    SDL_PumpEvents();

    /* Keyboard focus gone (alt-tab, window closed): key-ups won't come. */
    bool focus = SDL_GetKeyboardFocus() != NULL;
    if (had_focus && !focus) release_all();
    had_focus = focus;

    while ((n = SDL_PeepEvents(ev, 32, SDL_GETEVENT,
                               SDL_EVENT_KEY_DOWN, SDL_EVENT_KEY_UP)) > 0)
        for (int i = 0; i < n; i++) handle(&ev[i]);

    while ((n = SDL_PeepEvents(ev, 32, SDL_GETEVENT,
                               SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL)) > 0)
        for (int i = 0; i < n; i++) handle(&ev[i]);
}