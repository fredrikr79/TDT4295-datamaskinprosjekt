/* ======================================================================
 * frame_sync.c - the FPGA's SYNC pulses, and the game's timing around them
 * (see frame_sync.h).
 * ====================================================================== */
#define LOG_TAG "sync"

#include "frame_sync.h"
#include "platform.h"                   /* plat_millis(), plat_micros() */
#include "log.h"
#include "fpga.h"                       /* FPGA_FRAME_US: the period to start from */

/* ---- SYNC pulses ----------------------------------------------------------------- */
/* Written from interrupt context (or the fake FPGA), read from the main
 * loop. A single word, so the read is atomic on Cortex-M and on x86. */
static volatile uint32_t sync_count;
static volatile uint32_t sync_at_us;            /* the last SYNC's plat_micros()   */
static volatile uint32_t period_x16 = FPGA_FRAME_US * 16u;  /* average, x16      */

static volatile uint8_t  odd_gaps;              /* gaps in a row that didn't fit   */

void frame_sync_isr(void)
{
    uint32_t now = plat_micros(), d = now - sync_at_us, per = period_x16 / 16u;
    /* Gaps within half a period of the average count towards it (over ~16
     * frames); a single missed or doubled pulse doesn't. Four odd gaps in a
     * row: the rate itself changed, so start over from the latest gap. */
    if (sync_count && d < 1000000u) {
        if (d > per / 2u && d < per + per / 2u) {
            period_x16 = (uint32_t)((int32_t)period_x16 + ((int32_t)(d * 16u) - (int32_t)period_x16) / 16);
            odd_gaps = 0;
        } else if (++odd_gaps >= 4u) {
            period_x16 = d * 16u;
            odd_gaps   = 0;
        }
    }
    sync_at_us = now;                   /* the time first: readers check the count around it */
    sync_count++;
}

void frame_sync_last(uint32_t *count, uint32_t *at_us)
{
    uint32_t c, t;
    do {                                /* a SYNC in between: read again */
        c = sync_count;
        t = sync_at_us;
    } while (c != sync_count);
    *count = c;
    *at_us = t;
}

uint32_t frame_sync_period_us(void)
{
    return period_x16 / 16u;
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

/* ---- fixed steps: one per SYNC, or on real time --------------------------------------- */
#define STEPS_MAX_GAP_US 1000000u   /* a second at most, after a stall */

void frame_steps_start(frame_steps_t *c, uint16_t hz)
{
    uint32_t at;
    c->step_us = 1000000u / (hz ? hz : 1u);
    c->last_us = plat_micros();
    c->acc_us  = 0;
    frame_sync_last(&c->sync_seen, &at);
}

/* SYNC is coming (the last one less than two periods ago) and its period
 * is close enough to a step to take one step per SYNC. */
static bool can_lock(const frame_steps_t *c, uint32_t now, uint32_t at)
{
    uint32_t per = frame_sync_period_us();
    uint32_t diff = per > c->step_us ? per - c->step_us : c->step_us - per;
    return sync_count && now - at < 2u * per && diff * 100u <= c->step_us * FRAME_STEPS_LOCK_PCT;
}

uint32_t frame_steps_take(frame_steps_t *c)
{
    uint32_t cnt, at, now = plat_micros(), n;
    frame_sync_last(&cnt, &at);
    bool lock = can_lock(c, now, at);

    if (lock) {
        n = cnt - c->sync_seen;                 /* one step per SYNC */
        c->acc_us = 0;
    } else {
        uint32_t dt = now - c->last_us;
        if (dt > STEPS_MAX_GAP_US) dt = STEPS_MAX_GAP_US;
        c->acc_us += dt;
        n = c->acc_us / c->step_us;
        c->acc_us %= c->step_us;
    }
    c->sync_seen = cnt;
    c->last_us   = now;

    if (lock != c->locked) {
        c->locked = lock;
        if (lock)
            LOG_INFO("steps: one per SYNC (every %lu us; a step is %lu us)",
                     (unsigned long)frame_sync_period_us(), (unsigned long)c->step_us);
        else
            LOG_INFO("steps: on real time, every %lu us (SYNC %s)", (unsigned long)c->step_us,
                     now - at < 2u * frame_sync_period_us() ? "too far from that" : "not coming");
    }
    return n;
}

/* ---- time since the last frame ------------------------------------------------------------ */
uint32_t frame_elapsed_ms(uint32_t *last_ms, uint32_t max_ms)
{
    uint32_t now = plat_millis(), dt = now - *last_ms;
    *last_ms = now;
    return dt < max_ms ? dt : max_ms;
}