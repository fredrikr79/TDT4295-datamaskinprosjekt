/* clock_gettime / termios are POSIX, not ISO C -- ask for them explicitly
 * so this file builds under -std=c11 as well as -std=gnu11. */
#define _POSIX_C_SOURCE 200809L

#include "platform.h"
#include "host_hooks.h"
#include "fake_fpga.h"   /* fake_fpga_reset() */
#include "cli.h"         /* cli_quit()        */

#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

/* Driven by the fake FPGA in transport_host.c. */
bool host_ready_pin;

/* The fake FPGA's DONE: false between plat_fpga_program(true) and (false),
 * like a real FPGA whose design has been wiped and not loaded again yet. */
static bool fpga_loaded = true;

static uint64_t t0_ns;

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint32_t plat_millis(void)
{
    if (t0_ns == 0) t0_ns = now_ns();          /* first call = time zero */
    return (uint32_t)((now_ns() - t0_ns) / 1000000ull);
}

/* ---- LED: nothing to light on the PC (yet); the state is kept so the SDL
 * window could show it some day. No LOG_* here -- log_emit calls
 * plat_millis, keep these dependency-free. */
static bool led_r, led_g, led_b;

void plat_led_rgb(bool r, bool g, bool b)
{
    led_r = r;
    led_g = g;
    led_b = b;
}

void plat_led_pins(char *buf, size_t n)
{
    snprintf(buf, n, "no LED on the PC; it would show: red %s, green %s, blue %s\r\n",
             led_r ? "on" : "off", led_g ? "on" : "off", led_b ? "on" : "off");
}

/* ---- FPGA lines ----------------------------------------------------------- */
bool plat_fpga_ready_pin(void)
{
    return host_ready_pin;
}

bool plat_fpga_done(void)
{
    return fpga_loaded;
}

/* PROGRAM_B: hold = the design is wiped (READY low, DONE low); release = it
 * "loads" again at once: a new FPGA, new session, memory empty. */
void plat_fpga_program(bool hold)
{
    if (hold) {
        fpga_loaded    = false;
        host_ready_pin = false;
    } else {
        fake_fpga_reset();
        fpga_loaded    = true;
        host_ready_pin = true;
    }
}

void plat_fpga_reset(bool hold)
{
    if (hold) host_ready_pin = false;
    else { fake_fpga_reset(); host_ready_pin = true; }   /* a new FPGA: new session, memory empty */
}

/* ---- the rest --------------------------------------------------------------- */
bool plat_button_pressed(void)
{
    return false;                   /* no board button on the PC */
}

bool plat_exit(int code)
{
    exit(code);      /* does not return */
    return true;
}

void plat_mcu_reset(void) { cli_quit("MCU reset: start the program again"); }