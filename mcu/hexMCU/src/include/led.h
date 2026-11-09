#ifndef LED_H
#define LED_H

/* led.c - the status LED: the most important thing going on, as a colour
 * and a blink pattern. Modules switch states on and off; led_poll() (main
 * loop) shows the first active one in this list:
 *
 *   state                 colour    pattern
 *   LED_FPGA_UNLOADED     red       steady          FPGA_DONE low: no design loaded
 *   LED_READY_STORM       magenta   fast blink      stray READY edges < 10 ms apart
 *   LED_FPGA_NO_ANSWER    red       slow blink      the handshake keeps failing
 *   LED_RESYNC            yellow    fast blink, 1 s a command failed, resyncing
 *   LED_NO_SD             yellow    double blink    no SD card mounted
 *   LED_NO_INPUT          blue      double blink    no keyboard or mouse (in RUN)
 *   LED_HOLD              cyan      steady          loading off: the console has the FPGA
 *   LED_RUNNING           green     heartbeat       the game runs
 *   (none)                off
 *
 * The order is the priority: change it by moving lines in this enum and in
 * the table in led.c. The board LED is an RGB one (plat_led_rgb), so each
 * colour is a mix of red, green and blue switched fully on or off. On the
 * Nucleo every colour shows as its green LED. */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    LED_FPGA_UNLOADED,      /* set by led_poll itself, from plat_fpga_done() */
    LED_READY_STORM,
    LED_FPGA_NO_ANSWER,
    LED_RESYNC,
    LED_NO_SD,              /* set by led_poll itself, from storage_ready()  */
    LED_NO_INPUT,
    LED_HOLD,
    LED_RUNNING,
    LED_COUNT
} led_state_t;

/* colours: R | G | B */
#define LED_OFF      0u
#define LED_RED      1u
#define LED_GREEN    2u
#define LED_BLUE     4u
#define LED_YELLOW   (LED_RED | LED_GREEN)
#define LED_CYAN     (LED_GREEN | LED_BLUE)
#define LED_MAGENTA  (LED_RED | LED_BLUE)
#define LED_WHITE    (LED_RED | LED_GREEN | LED_BLUE)

void led_set(led_state_t s, bool on);
void led_flash(led_state_t s, uint32_t ms);   /* on now, off by itself after ms */
void led_poll(void);                          /* main loop: updates the LED    */

/* ---- console tests ----------------------------------------------------------
 * These take the LED over from the states until led_auto(); they work with
 * no FPGA at all (no DONE, no READY, loading off), since led_poll() runs
 * every main-loop pass whatever the game is doing.
 *   led_test(rgb)   a fixed colour, to check which pin is which
 *   led_show(s)     one state's colour and pattern, as if it were on top
 *   led_cycle()     every state in turn, LED_CYCLE_MS each, named on the
 *                   console as it comes up
 * led_now() names what's shown; led_id()/led_name() are for listing the
 * states ("led list"); led_find() is the state with that id, -1 if none. */
#define LED_CYCLE_MS 4000u      /* two rounds of a 2 s pattern */

void        led_test(uint8_t rgb);
void        led_show(led_state_t s);
void        led_cycle(void);
void        led_auto(void);
const char *led_now(void);

const char *led_id(led_state_t s);      /* short: "storm", "nosd", ...     */
const char *led_name(led_state_t s);    /* long:  "READY storm", ...      */
int         led_find(const char *id);

#endif