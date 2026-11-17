#ifndef FRAME_SYNC_H
#define FRAME_SYNC_H

/* ======================================================================
 * frame_sync.h - the FPGA's SYNC pulse, and the game's timing around it.
 * Game logic, not part of the FPGA link.
 *
 * SYNC: the FPGA pulses it once per screen frame (FPGA_FRAME_US in fpga.h:
 * 16.8 ms, 59.5 Hz), whatever the MCU does. Same idea as the READY
 * counter: the ISR only counts pulses, the main loop compares counts, so
 * no pulse is ever missed and nothing has to be caught live.
 *
 * What it's used for:
 *   - when to draw: a game frame per new SYNC          frame_sync_take()
 *   - the FPGA's heartbeat: no SYNC for a while means
 *     the FPGA is gone                                 frame_sync_alive()
 *   - when the NEXT SYNC will come, to the
 *     microsecond: sprite changes go out just before
 *     it, in the screen's blanking (sprite.c)         frame_sync_last(),
 *                                                      frame_sync_period_us()
 * What it's NOT used for: timing. Nothing counts SYNCs to measure time, so
 * the game plays the same at any frame rate. Things that move do it on
 * real time instead:
 *   - fixed steps, N a second (the player's physics)  frame_steps_*()
 *   - time since the last frame (the stick cursor,
 *     the animations)                                  frame_elapsed_ms()
 * ====================================================================== */

#include <stdint.h>
#include <stdbool.h>

/* ---- SYNC pulses --------------------------------------------------------------
 * Typical use, once per game loop pass:
 *
 *   static uint32_t seen;                     // start: seen = frame_sync_count()
 *   uint32_t n = frame_sync_take(&seen);
 *   if (n == 0)  -> no new frame yet: keep working / idle
 *   if (n == 1)  -> exactly one new frame: on time, start the next one
 *   if (n  > 1)  -> n - 1 frames went by while we were busy: behind */
void     frame_sync_isr(void);      /* SYNC rising edge: EXTI / fake FPGA */
uint32_t frame_sync_count(void);    /* pulses since boot                  */

/* Frames since *seen (0 = none yet), and moves *seen up to now. */
uint32_t frame_sync_take(uint32_t *seen);

/* ---- when SYNCs come ------------------------------------------------------------
 * The interrupt notes each SYNC's time (plat_micros()), and keeps a running
 * average of the time between them (measured: 16800 us on the real FPGA).
 * The next SYNC is due at at_us + frame_sync_period_us(). */
void     frame_sync_last(uint32_t *count, uint32_t *at_us);   /* the last SYNC: count, time */
uint32_t frame_sync_period_us(void);

/* ---- heartbeat ------------------------------------------------------------------
 * frame_sync_alive() is false once no SYNC has come for
 * FRAME_SYNC_HEARTBEAT_MS: the FPGA is gone (reset, reprogramming,
 * unplugged). The caller decides when that matters (game.c: while loading,
 * saving or playing) and calls frame_sync_heartbeat_reset() when it starts
 * to, so time before that doesn't count.
 * -DFRAME_SYNC_HEARTBEAT=0: always alive (an FPGA that doesn't send SYNC). */
#ifndef FRAME_SYNC_HEARTBEAT
#define FRAME_SYNC_HEARTBEAT     1
#endif
#ifndef FRAME_SYNC_HEARTBEAT_MS
#define FRAME_SYNC_HEARTBEAT_MS  200u   /* 12 frames at 59.5 Hz */
#endif

void frame_sync_heartbeat_reset(void);
bool frame_sync_alive(void);

/* ---- fixed steps: one per SYNC, or on real time -------------------------------------
 * A clock that says how many steps of about 1/hz s have gone by since it
 * last said. Two ways, picked anew each time it's asked:
 *   - locked: SYNC is coming, and its measured period is within
 *     FRAME_STEPS_LOCK_PCT of 1/hz: exactly one step per SYNC. The real
 *     FPGA's 59.52 Hz against 60 is 0.8 % slow, which nobody sees, and
 *     the steps line up with the frames: no frame ever gets two steps.
 *   - free: otherwise (no SYNC, or a frame rate far from hz), whole steps
 *     of 1/hz s of real time, in microseconds, the leftover carried over.
 *     A gap of more than a second counts as a second.
 * Switching between them is logged.
 *
 *   frame_steps_t clock;
 *   frame_steps_start(&clock, 60);     // and again whenever time shouldn't count
 *   uint32_t n = frame_steps_take(&clock); */
#ifndef FRAME_STEPS_LOCK_PCT
#define FRAME_STEPS_LOCK_PCT 5u
#endif

typedef struct {
    uint32_t last_us;           /* when time was last added (free)            */
    uint32_t acc_us;            /* time not stepped yet (free)                */
    uint32_t step_us;           /* 1/hz                                       */
    uint32_t sync_seen;         /* frame_sync_count() at the last take        */
    bool     locked;            /* the last take went by SYNC                 */
} frame_steps_t;

void     frame_steps_start(frame_steps_t *c, uint16_t hz);
uint32_t frame_steps_take(frame_steps_t *c);

/* ---- time since the last frame ------------------------------------------------------
 * Milliseconds since *last_ms, at most max_ms (a stall or a load doesn't
 * fling things across the screen), and moves *last_ms up to now. Start
 * with *last_ms = 0: the first call then gives max_ms at most. */
uint32_t frame_elapsed_ms(uint32_t *last_ms, uint32_t max_ms);

#endif /* FRAME_SYNC_H */