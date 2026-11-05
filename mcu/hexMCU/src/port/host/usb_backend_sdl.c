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
 * Controllers: any controller SDL knows (DualShock, DualSense, Xbox,
 * Switch Pro, ...) shows up as pad 0, 1 in plug-in order, with the same
 * PAD_* layout as the MCU gives a DualShock 4. They are read as state every
 * poll rather than through events, so display_poll() flushing the event
 * queue doesn't matter for them.
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
static float       rem_x, rem_y, rem_wheel, rem_pan;   /* sub-unit motion carried over */
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
/* Same path as a report-protocol mouse on the MCU: full-size values, wheel
 * and horizontal wheel (pan). */
static void mouse_send(int dx, int dy, int wheel, int pan)
{
    usb_input_mouse_motion(buttons, dx, dy, wheel, pan);
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

/* ---- controllers --------------------------------------------------------- */
#define PAD_SCAN_MS     500u    /* how often to look for new controllers */
#define TRIGGER_PRESSED 32u     /* lt/rt above this count as PAD_L2/PAD_R2 down */

static struct {
    SDL_Gamepad   *gp;          /* NULL: slot free */
    SDL_JoystickID id;
} pads[INPUT_PADS];
static bool pads_ok;            /* SDL gamepad subsystem is up */

/* Look for controllers plugged in and out. */
static void pads_scan(void)
{
    for (unsigned p = 0; p < INPUT_PADS; p++) {
        if (!pads[p].gp || SDL_GamepadConnected(pads[p].gp)) continue;
        SDL_CloseGamepad(pads[p].gp);
        pads[p].gp = NULL;
        usb_input_gamepad_connected((uint8_t)p, false);
        LOG_INFO("pad %u: controller gone", p);
    }

    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    if (!ids) return;
    for (int i = 0; i < n; i++) {
        int free_slot = -1;
        bool open = false;
        for (unsigned p = 0; p < INPUT_PADS; p++) {
            if (pads[p].gp && pads[p].id == ids[i]) open = true;
            if (!pads[p].gp && free_slot < 0) free_slot = (int)p;
        }
        if (open) continue;
        if (free_slot < 0) break;                       /* all slots taken */

        SDL_Gamepad *gp = SDL_OpenGamepad(ids[i]);
        if (!gp) {
            LOG_WARN("can't open controller: %s", SDL_GetError());
            continue;
        }
        pads[free_slot].gp = gp;
        pads[free_slot].id = ids[i];
        usb_input_gamepad_connected((uint8_t)free_slot, true);
        const char *name = SDL_GetGamepadName(gp);
        LOG_INFO("pad %d: %s", free_slot, name ? name : "controller");
    }
    SDL_free(ids);
}

static int8_t stick(Sint16 v)  { return (int8_t)((int)v / 256); }      /* -32768..32767 -> -128..127 */
static uint8_t trigger(Sint16 v) { return v <= 0 ? 0 : (uint8_t)((int)v / 128); } /* 0..32767 -> 0..255 */

/* Read each controller's state and hand it over. Without keyboard focus
 * the game gets "nothing pressed", like the keyboard and mouse. */
static void pads_poll(bool focus)
{
    static const struct { SDL_GamepadButton b; uint32_t bit; } map[] = {
        { SDL_GAMEPAD_BUTTON_SOUTH,          PAD_SOUTH  },
        { SDL_GAMEPAD_BUTTON_EAST,           PAD_EAST   },
        { SDL_GAMEPAD_BUTTON_WEST,           PAD_WEST   },
        { SDL_GAMEPAD_BUTTON_NORTH,          PAD_NORTH  },
        { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,  PAD_L1     },
        { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_R1     },
        { SDL_GAMEPAD_BUTTON_BACK,           PAD_SELECT },
        { SDL_GAMEPAD_BUTTON_START,          PAD_START  },
        { SDL_GAMEPAD_BUTTON_LEFT_STICK,     PAD_L3     },
        { SDL_GAMEPAD_BUTTON_RIGHT_STICK,    PAD_R3     },
        { SDL_GAMEPAD_BUTTON_GUIDE,          PAD_HOME   },
        { SDL_GAMEPAD_BUTTON_TOUCHPAD,       PAD_TOUCH  },
        { SDL_GAMEPAD_BUTTON_DPAD_UP,        PAD_UP     },
        { SDL_GAMEPAD_BUTTON_DPAD_DOWN,      PAD_DOWN   },
        { SDL_GAMEPAD_BUTTON_DPAD_LEFT,      PAD_LEFT   },
        { SDL_GAMEPAD_BUTTON_DPAD_RIGHT,     PAD_RIGHT  },
    };

    for (unsigned p = 0; p < INPUT_PADS; p++) {
        SDL_Gamepad *gp = pads[p].gp;
        if (!gp) continue;

        usb_gamepad_t g = { .touch_valid = true };      /* all released, no fingers */
        if (focus) {
            for (unsigned k = 0; k < sizeof map / sizeof map[0]; k++)
                if (SDL_GetGamepadButton(gp, map[k].b)) g.buttons |= map[k].bit;

            g.lx = stick(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX));
            g.ly = stick(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY));
            g.rx = stick(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTX));
            g.ry = stick(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTY));
            g.lt = trigger(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
            g.rt = trigger(SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
            if (g.lt > TRIGGER_PRESSED) g.buttons |= PAD_L2;  /* SDL has no L2/R2 buttons */
            if (g.rt > TRIGGER_PRESSED) g.buttons |= PAD_R2;

            /* SDL gives 0..1 from the top left, same corner as the MCU */
            if (SDL_GetNumGamepadTouchpads(gp) > 0) {
                for (int f = 0; f < 2; f++) {
                    bool down = false;
                    float x = 0, y = 0;
                    if (!SDL_GetGamepadTouchpadFinger(gp, 0, f, &down, &x, &y, NULL)) continue;
                    g.touch[f].down = down;
                    g.touch[f].x = (uint16_t)(x * (PAD_TOUCH_W - 1) + 0.5f);
                    g.touch[f].y = (uint16_t)(y * (PAD_TOUCH_H - 1) + 0.5f);
                }
            }
        }
        usb_input_gamepad((uint8_t)p, &g);
    }
}

/* Focus lost: SDL won't tell us about key-ups any more, so let go of all. */
static void release_all(void)
{
    memset(kb, 0, sizeof kb);
    kb_send();
    buttons = 0;
    rem_x = rem_y = rem_wheel = rem_pan = 0.0f;
    mouse_send(0, 0, 0, 0);
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
        if (dx || dy) mouse_send(dx, dy, 0, 0);
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
        mouse_send(0, 0, 0, 0);
        break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (!(buttons & button_bit(e->button.button))) break;   /* the grab click */
        buttons &= (uint8_t)~button_bit(e->button.button);
        mouse_send(0, 0, 0, 0);
        break;

    case SDL_EVENT_MOUSE_WHEEL: {
        float y = e->wheel.y, x = e->wheel.x;   /* +y = away from you, +x = right */
        if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) { y = -y; x = -x; }
        int w = take_whole(&rem_wheel, y);
        int p = take_whole(&rem_pan, x);
        if (w || p) mouse_send(0, 0, w, p);
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

    /* Controllers are optional: keyboard and mouse work without them. */
    pads_ok = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
    if (!pads_ok) LOG_WARN("no controller support: %s", SDL_GetError());
    else          pads_scan();
}

void usb_backend_poll(void)
{
    static Uint64 last_scan;
    SDL_Event ev[32];
    int n;

    if (!SDL_WasInit(SDL_INIT_VIDEO)) return;
    SDL_PumpEvents();           /* also updates the controllers' state */

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

    if (pads_ok) {
        Uint64 now = SDL_GetTicks();
        if (now - last_scan >= PAD_SCAN_MS) {
            pads_scan();
            last_scan = now;
        }
        pads_poll(focus);
    }
}