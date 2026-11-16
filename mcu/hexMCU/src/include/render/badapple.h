#ifndef BADAPPLE_H
#define BADAPPLE_H

/* badapple.c - plays a 120x90, 1 bpp, 25 fps video from the SD card on the
 * SIM grid, 0 pixels as the dark material, 1 pixels as the light one.
 * Console: "badapple [-l] [-s <dark><light>] [<file>]" / "badapple stop".
 * Needs a valid INFO (for N). Leaves the simulation as it is. */

#include <stdbool.h>
#include "fpga.h"                   /* material_t */

/* false: not started (logged why) */
bool badapple_start(const char *path, bool loop, material_t dark, material_t light);
void badapple_stop(void);
void badapple_poll(void);               /* main loop, every pass           */
bool badapple_busy(void);

#endif