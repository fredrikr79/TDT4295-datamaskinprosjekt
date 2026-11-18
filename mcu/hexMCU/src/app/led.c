/* ======================================================================
 * led.c - the status LED (see led.h).
 *
 * A pattern is 16 slots of LED_SLOT_MS (125 ms), so 2 s long, played
 * from the top bit down and over again: 1 = on in the state's colour.
 *   0xFFFF steady   0xAAAA fast blink (4 a second)   0xFF00 slow blink
 *   0xA000 double blink, then a pause                0xC000 heartbeat
 * A state switched on with led_flash() plays its pattern from the top when
 * it shows, so a short flash is always the same blinks.
 * Every module can be built without the others knowing: they only switch
 * states. The LED is written only when what it shows changes.
 * ====================================================================== */
#define LOG_TAG "led"

#include <stdbool.h>
#include <stdint.h>

#include "led.h"
#include "platform.h"
#include "storage.h"
#include "log.h"

#define LED_SLOT_MS 125u

static const struct {
    const char *name;
    uint8_t     rgb;
    uint16_t    pattern;
} looks[LED_COUNT] = {
    [LED_SAVES_DELETED]  = { "saves deleted",              LED_WHITE,   0xAAAA },
    [LED_FPGA_UNLOADED]  = { "FPGA not loaded (DONE low)", LED_RED,     0xFFFF },
    [LED_READY_STORM]    = { "READY storm",                LED_MAGENTA, 0xAAAA },
    [LED_FPGA_NO_ANSWER] = { "FPGA not answering",         LED_RED,     0xFF00 },
    [LED_RESYNC]         = { "resync",                     LED_YELLOW,  0xAAAA },
    [LED_NO_SD]          = { "no SD card",                 LED_YELLOW,  0xA000 },
    [LED_NO_INPUT]       = { "no keyboard or mouse",       LED_BLUE,    0xA000 },
    [LED_HOLD]           = { "hold (loading off)",         LED_CYAN,    0xFFFF },
    [LED_RUNNING]        = { "running",                    LED_GREEN,   0xC000 },
};

static bool     active[LED_COUNT];
static uint32_t flash_until[LED_COUNT];   /* led_flash(): off again after this   */
static uint32_t flash_from[LED_COUNT];    /* led_flash(): its pattern from here  */
static bool     flashing[LED_COUNT];
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
    flash_from[s]  = plat_millis();
    flash_until[s] = flash_from[s] + ms;
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

    int top = -1;
    for (unsigned i = 0; i < LED_COUNT && top < 0; i++)
        if (active[i]) top = (int)i;
    if (top != shown) {
        shown = top;
        LOG_DBG("showing: %s", top < 0 ? "nothing" : looks[top].name);
    }
    if (top < 0) { led_write(LED_OFF); return; }

    uint32_t t = flashing[top] ? now - flash_from[top] : now;    /* pattern clock */
    unsigned slot = (t / LED_SLOT_MS) % 16u;
    bool on = (looks[top].pattern >> (15u - slot)) & 1u;
    led_write(on ? looks[top].rgb : LED_OFF);
}