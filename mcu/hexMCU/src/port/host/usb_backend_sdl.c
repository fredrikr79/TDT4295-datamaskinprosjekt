/* Host backend: SDL2 events -> fake HID boot reports -> usb_input.c
 *
 * SDL scancodes are the USB HID usage IDs, so a key's scancode is already
 * its KEY_* code. This file keeps a boot keyboard report up to date and
 * sends it on every change, the same way a real keyboard does.
 *
 * Mouse: click the window to grab the mouse (relative mode), F12 lets go.
 * F12 is not passed on to the game.
 *
 * Build with:  `sdl2-config --cflags --libs`
 * Define USB_SDL_NO_WINDOW if the host build already has an SDL window:
 * keyboard events come from whichever of the program's windows has focus.
 * Only one place should call SDL_PollEvent: if the host display code does
 * too, move its event handling in here. */

#define LOG_TAG "usb-sdl"

#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "log.h"
#include "usb_backend.h"
#include "usb_input.h"

static uint8_t kb[8];           /* { modifier, reserved, key[6] } */
static uint8_t buttons;

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

/* Big SDL movements are split into several reports, like a fast real mouse. */
static void mouse_send(int dx, int dy, int wheel)
{
    do {
        int8_t x = clamp8(dx), y = clamp8(dy), w = clamp8(wheel);
        uint8_t r[4] = { buttons, (uint8_t)x, (uint8_t)y, (uint8_t)w };
        usb_input_mouse_report(r, sizeof r);
        dx -= x; dy -= y; wheel -= w;
    } while (dx || dy || wheel);
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

/* Focus lost: SDL won't tell us about key-ups any more, so let go of all. */
static void release_all(void)
{
    memset(kb, 0, sizeof kb);
    kb_send();
    buttons = 0;
    mouse_send(0, 0, 0);
    SDL_SetRelativeMouseMode(SDL_FALSE);
}

/* ---- backend ------------------------------------------------------------- */
void usb_backend_init(void)
{
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        LOG_ERR("SDL init: %s", SDL_GetError());
        exit(1);
    }
#ifndef USB_SDL_NO_WINDOW
    if (!SDL_CreateWindow("input", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                          320, 240, 0)) {
        LOG_ERR("SDL window: %s", SDL_GetError());
        exit(1);
    }
#endif
    usb_input_connected(USB_KEYBOARD, true);
    usb_input_connected(USB_MOUSE, true);
    LOG_INFO("input from SDL: click the window to grab the mouse, F12 to let go");
}

void usb_backend_poll(void)
{
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            exit(0);

        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (e.key.repeat) break;    /* real keyboards don't repeat */
            if (e.key.keysym.scancode == SDL_SCANCODE_F12) {
                if (e.type == SDL_KEYDOWN) SDL_SetRelativeMouseMode(SDL_FALSE);
                break;
            }
            kb_key(e.key.keysym.scancode, e.type == SDL_KEYDOWN);
            break;

        case SDL_MOUSEMOTION:
            mouse_send(e.motion.xrel, e.motion.yrel, 0);
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (!SDL_GetRelativeMouseMode()) SDL_SetRelativeMouseMode(SDL_TRUE);
            buttons |= button_bit(e.button.button);
            mouse_send(0, 0, 0);
            break;

        case SDL_MOUSEBUTTONUP:
            buttons &= (uint8_t)~button_bit(e.button.button);
            mouse_send(0, 0, 0);
            break;

        case SDL_MOUSEWHEEL: {
            int w = e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) w = -w;
            mouse_send(0, 0, w);
            break;
        }

        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) release_all();
            break;
        }
    }
}
