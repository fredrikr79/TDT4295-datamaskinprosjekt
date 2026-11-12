/* ======================================================================
 * led.c - the status LED (see led.h).
 *
 * A pattern is 16 slots of LED_SLOT_MS (125 ms), so 2 s long, played
 * from the top bit down and over again: 1 = on in the state's colour.
 *   0xFFFF steady   0xAAAA fast blink (4 a second)   0xFF00 slow blink
 *   0xA000 double blink, then a pause                0xC000 heartbeat
 * Every module can be built without the others knowing: they only switch
 * states. The LED is written only when what it shows changes.
 * ====================================================================== */
#define LOG_TAG "led"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "led.h"
#include "platform.h"
#include "storage.h"
#include "log.h"

#define LED_SLOT_MS 125u

static const struct {
    const char *id;             /* for the console: "led show <id>" */
    const char *name;
    uint8_t     rgb;
    uint16_t    pattern;
} looks[LED_COUNT] = {
    [LED_FPGA_UNLOADED]  = { "unloaded", "FPGA not loaded (DONE low)", LED_RED,     0xFFFF },
    [LED_READY_STORM]    = { "storm",    "READY storm",                LED_MAGENTA, 0xAAAA },
    [LED_FPGA_NO_ANSWER] = { "noanswer", "FPGA not answering",         LED_RED,     0xFF00 },
    [LED_RESYNC]         = { "resync",   "resync",                     LED_YELLOW,  0xAAAA },
    [LED_NO_SD]          = { "nosd",     "no SD card",                 LED_YELLOW,  0xA000 },
    [LED_NO_INPUT]       = { "noinput",  "no keyboard or mouse",       LED_BLUE,    0xA000 },
    [LED_HOLD]           = { "hold",     "hold (loading off)",         LED_CYAN,    0xFFFF },
    [LED_RUNNING]        = { "running",  "running",                    LED_GREEN,   0xC000 },
};

static bool     active[LED_COUNT];
static uint32_t flash_until[LED_COUNT];   /* led_flash(): off again after this */
static bool     flashing[LED_COUNT];

/* the console's tests (led_test / led_show / led_cycle) */
static enum { M_AUTO, M_COLOUR, M_SHOW, M_CYCLE } mode = M_AUTO;
static uint8_t  test_rgb;                 /* M_COLOUR: the colour               */
static int      preview = -1;             /* M_SHOW / M_CYCLE: the state played */
static uint32_t cycle_t0;                 /* M_CYCLE: when it started           */
static int      shown   = -2;             /* the state shown, -1 = none          */
static uint8_t  written = 0xFFu;          /* last colour written to the LED      */

void led_set(led_state_t s, bool on)
{
    if ((unsigned)s >= LED_COUNT) return;
    active[s]   = on;
    flashing[s] = false;
}

void led_flash(led_state_t s, uint32_t ms)
{
    if ((unsigned)s >= LED_COUNT) return;
    active[s]      = true;
    flashing[s]    = true;
    flash_until[s] = plat_millis() + ms;
}

static void led_write(uint8_t rgb)
{
    if (rgb == written) return;
    written = rgb;
    plat_led_rgb(rgb & LED_RED, rgb & LED_GREEN, rgb & LED_BLUE);
}

void led_poll(void)
{
    uint32_t now = plat_millis();

    /* the ones read straight from the hardware */
    active[LED_FPGA_UNLOADED] = !plat_fpga_done();
    active[LED_NO_SD]         = !storage_ready();

    for (unsigned i = 0; i < LED_COUNT; i++)
        if (flashing[i] && (int32_t)(now - flash_until[i]) >= 0)
            active[i] = flashing[i] = false;

    if (mode == M_COLOUR) { led_write(test_rgb); return; }

    int top = -1;
    uint32_t t = now;                   /* pattern clock */
    if (mode == M_CYCLE) {
        uint32_t el = now - cycle_t0;
        top = (int)((el / LED_CYCLE_MS) % LED_COUNT);
        t   = el % LED_CYCLE_MS;        /* each state's pattern from its start */
        if (top != preview) {
            preview = top;
            log_raw("led cycle: %u/%u %s (%s)\r\n", (unsigned)top + 1, (unsigned)LED_COUNT,
                    looks[top].id, looks[top].name);
        }
    } else if (mode == M_SHOW) {
        top = preview;
    } else {
        for (unsigned i = 0; i < LED_COUNT && top < 0; i++)
            if (active[i]) top = (int)i;
        if (top != shown) {
            shown = top;
            LOG_DBG("showing: %s", top < 0 ? "nothing" : looks[top].name);
        }
    }
    if (top < 0) { led_write(LED_OFF); return; }

    unsigned slot = (t / LED_SLOT_MS) % 16u;
    bool on = (looks[top].pattern >> (15u - slot)) & 1u;
    led_write(on ? looks[top].rgb : LED_OFF);
}

void led_test(uint8_t rgb) { mode = M_COLOUR; test_rgb = rgb & LED_WHITE; }
void led_auto(void)        { mode = M_AUTO; preview = -1; }

void led_show(led_state_t s)
{
    if ((unsigned)s >= LED_COUNT) return;
    mode    = M_SHOW;
    preview = (int)s;
}

void led_cycle(void)
{
    mode     = M_CYCLE;
    preview  = -1;                      /* led_poll names the first one */
    cycle_t0 = plat_millis();
}

const char *led_now(void)
{
    static char buf[64];
    switch (mode) {
    case M_COLOUR: return "test colour (led auto: back to the states)";
    case M_SHOW:
        snprintf(buf, sizeof buf, "showing '%s' (led auto: back to the states)", looks[preview].id);
        return buf;
    case M_CYCLE:  return "cycling through the states (led auto: stop)";
    default:       return shown < 0 ? "nothing (off)" : looks[shown].name;
    }
}

const char *led_id(led_state_t s)   { return (unsigned)s < LED_COUNT ? looks[s].id   : "?"; }
const char *led_name(led_state_t s) { return (unsigned)s < LED_COUNT ? looks[s].name : "?"; }

int led_find(const char *id)
{
    for (unsigned i = 0; i < LED_COUNT; i++)
        if (!strcmp(id, looks[i].id)) return (int)i;
    return -1;
}