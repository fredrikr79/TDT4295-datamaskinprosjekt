#ifndef FRAME_SYNC_H
#define FRAME_SYNC_H

/* ======================================================================
 * frame_sync.h - the FPGA's SYNC pulse: "done with a frame, starting the
 * next one". Game logic, not part of the FPGA link.
 *
 * Same idea as the READY counter: the ISR only counts pulses, the main
 * loop compares counts, so no pulse is ever missed and nothing has to be
 * caught live.
 *
 * Typical use, once per game loop pass:
 *
 *   static uint32_t seen;                     // start: seen = frame_sync_count()
 *   uint32_t n = frame_sync_take(&seen);
 *   if (n == 0)  -> no new frame yet: keep working / idle
 *   if (n == 1)  -> exactly one new frame: on time, start the next one
 *   if (n  > 1)  -> n - 1 frames went by while we were busy: behind
 * ====================================================================== */

#include <stdint.h>

void     frame_sync_isr(void);      /* SYNC rising edge: EXTI / fake FPGA */
uint32_t frame_sync_count(void);    /* pulses since boot                  */

/* Frames since *seen (0 = none yet), and moves *seen up to now. */
uint32_t frame_sync_take(uint32_t *seen);

#endif /* FRAME_SYNC_H */