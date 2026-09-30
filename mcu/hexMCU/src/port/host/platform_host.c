/* clock_gettime / termios are POSIX, not ISO C -- ask for them explicitly
 * so this file builds under -std=c11 as well as -std=gnu11. */
#define _POSIX_C_SOURCE 200809L

#include "platform.h"
#include "host_hooks.h"

#include <time.h>
#include <stdbool.h>
#include <stdlib.h>

/* Driven by the fake FPGA in transport_host.c. */
bool host_ready_pin;

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

void plat_led_toggle(void)
{
    static bool on;
    on = !on;
    /* no LOG_* here -- log_emit calls plat_millis, keep this dependency-free */
}

bool plat_fpga_ready_pin(void)
{
    return host_ready_pin;
}

bool plat_exit(int code)
{
    exit(code);      /* does not return */
    return true;
}