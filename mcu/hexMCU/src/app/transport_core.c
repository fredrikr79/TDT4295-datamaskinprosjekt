/* ======================================================================
 * transport_core.c -- the platform independent half of the transport
 * layer: backend dispatch and the FPGA_READY edge counter.
 *
 * Compiled by BOTH builds. No stm32 headers.
 * ====================================================================== */
#define LOG_TAG "xport"

#include "transport.h"
#include "log.h"
#include <stddef.h>

/* Written from interrupt context (or the fake FPGA), read from the main
 * loop. A single word, so the read is atomic on Cortex-M and on x86. */
static volatile uint32_t ready_count;

void transport_ready_isr(void)
{
    ready_count++;
}

uint32_t transport_ready_count(void)
{
    return ready_count;
}

transport_status_t transport_init(void)
{
    if (BACKEND == NULL || BACKEND->init == NULL) {
        LOG_ERR("no backend linked");
        return TRANSPORT_ERR;
    }

    transport_status_t st = BACKEND->init();
    if (st != TRANSPORT_OK) {
        LOG_ERR("backend init failed (%d)", (int)st);
    } else {
        LOG_INFO("backend ready");
    }
    return st;
}

void transport_poll(void)
{
    if (BACKEND->poll) BACKEND->poll();
}
