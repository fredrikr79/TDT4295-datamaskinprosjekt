/* ======================================================================
 * frame_sync.c - counts the FPGA's SYNC pulses (see frame_sync.h).
 * ====================================================================== */
#include "frame_sync.h"

/* Written from interrupt context (or the fake FPGA), read from the main
 * loop. A single word, so the read is atomic on Cortex-M and on x86. */
static volatile uint32_t sync_count;

void frame_sync_isr(void)
{
    sync_count++;
}

uint32_t frame_sync_count(void)
{
    return sync_count;
}

uint32_t frame_sync_take(uint32_t *seen)
{
    uint32_t now = sync_count;
    uint32_t n   = now - *seen;     /* wrap-safe */
    *seen = now;
    return n;
}