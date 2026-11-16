#ifndef BADAPPLE_H
#define BADAPPLE_H

/* badapple.c - plays a 120x90, 1 bpp, 25 fps video from the SD card on the
 * SIM grid, 0 pixels as the dark material, 1 pixels as the light one.
 * Console: "badapple [-l] [-s <dark><light>] [<file>]" / "badapple stop".
 * Needs a valid INFO (for N). Leaves the simulation as it is. */

#include <stdbool.h>
#include <stdint.h>
#include "fpga.h"                   /* material_t */

/* false: not started (logged why) */
bool badapple_start(const char *path, bool loop, material_t dark, material_t light);
void badapple_stop(void);
void badapple_poll(void);               /* main loop, every pass           */
bool badapple_busy(void);

/* Wait this long after a SYNC before a frame's writes start (0: at once,
 * the default). Whole ms, by plat_millis(), so the real wait is between
 * ms - 1 and ms. For finding where the writes stop tearing: sweep it and
 * watch the tear move. Can be changed while playing. */
void     badapple_set_sync_delay(uint32_t ms);
uint32_t badapple_sync_delay(void);

#endif