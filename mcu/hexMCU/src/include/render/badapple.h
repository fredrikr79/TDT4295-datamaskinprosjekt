#ifndef BADAPPLE_H
#define BADAPPLE_H

/* badapple.c - plays a 120x90, 1 bpp, 25 fps video from the SD card on the
 * SIM grid. Console: "badapple [-l] [<file>]" / "badapple stop".
 * Needs "loading off" (the game must not draw into the grid at the same
 * time) and a valid INFO (for N). */

#include <stdbool.h>

bool badapple_start(const char *path, bool loop);  /* false: not started (logged why) */
void badapple_stop(void);
void badapple_poll(void);               /* main loop, every pass           */
bool badapple_busy(void);

#endif