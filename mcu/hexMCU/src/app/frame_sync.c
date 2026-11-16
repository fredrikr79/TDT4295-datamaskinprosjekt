/* ======================================================================
 * frame_sync.c - the FPGA's SYNC pulses, and the game's timing around them
 * (see frame_sync.h).
 * ====================================================================== */
#include "frame_sync.h"
#include "platform.h"                   /* plat_millis() */

/* ---- SYNC pulses ----------------------------------------------------------------- */
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

/* ---- heartbeat --------------------------------------------------------------------- */
static struct {
    uint32_t count;             /* sync_count when it was last seen moving */
    uint32_t at;                /* ...and when                             */
} beat;

void frame_sync_heartbeat_reset(void)
{
    beat.count = sync_count;
    beat.at    = plat_millis();
}

bool frame_sync_alive(void)
{
    uint32_t c = sync_count, now = plat_millis();
    if (c != beat.count) {
        beat.count = c;
        beat.at    = now;
        return true;
    }
    return !FRAME_SYNC_HEARTBEAT || now - beat.at < FRAME_SYNC_HEARTBEAT_MS;
}

/* ---- fixed steps on real time --------------------------------------------------------- */
#define STEPS_MAX_GAP_MS 1000u      /* also keeps dt * hz from overflowing */

void frame_steps_start(frame_steps_t *c, uint16_t hz)
{
    c->last_ms = plat_millis();
    c->acc     = 0;
    c->hz      = hz ? hz : 1u;
}

uint32_t frame_steps_take(frame_steps_t *c)
{
    uint32_t now = plat_millis(), dt = now - c->last_ms;
    c->last_ms = now;
    if (dt > STEPS_MAX_GAP_MS) dt = STEPS_MAX_GAP_MS;
    c->acc += dt * c->hz;
    uint32_t n = c->acc / 1000u;
    c->acc %= 1000u;
    return n;
}

/* ---- time since the last frame ------------------------------------------------------------ */
uint32_t frame_elapsed_ms(uint32_t *last_ms, uint32_t max_ms)
{
    uint32_t now = plat_millis(), dt = now - *last_ms;
    *last_ms = now;
    return dt < max_ms ? dt : max_ms;
}