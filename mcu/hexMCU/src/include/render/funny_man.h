#ifndef PLAYER_H
#define PLAYER_H

/* The stand-in character: a cube with a smiley face, as a HUD sprite.
 * Portable: MCU and host build.
 *
 *   room_enter:  player_init(x, y);
 *   run_frame:   player_frame(&keys, n);    WASD / arrow keys move it
 *
 * hud_poll() does the sending, like for the cursor. */

#include <stdint.h>

#include "usb_input.h"

#define PLAYER_ART      16u     /* the drawing is 16x16 art pixels         */
#ifndef PLAYER_SCALE
#define PLAYER_SCALE    2u      /* screen pixels per art pixel             */
#endif
#define PLAYER_SIZE     (PLAYER_ART * PLAYER_SCALE)   /* on screen, px     */
#ifndef PLAYER_SPEED
#define PLAYER_SPEED    3       /* px per frame                            */
#endif

/* Put it at x, y (top-left). The first call also makes its image. */
void player_init(int16_t x, int16_t y);

/* Move by the held keys. n = frames since the last call, so it keeps its
 * speed when the game falls behind. Stays on the screen. */
void player_frame(const input_t *keys, uint32_t n);

int16_t player_x(void);
int16_t player_y(void);

#endif