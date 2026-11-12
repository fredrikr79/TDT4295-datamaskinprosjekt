#ifndef DRAW_H
#define DRAW_H

/* Drawing mode: put material into the running simulation with the mouse or
 * a controller. Each placement is one solid WRITE_BOX of the brush size,
 * centred on the cursor and clipped at the screen's edges.
 *
 *                  keyboard / mouse          controller
 *   place          left button (held)        cross or R2 (held)
 *   erase (air)    right button (held)       circle or L2 (held)
 *   material       0-7, scroll wheel         L1 / R1, d-pad left / right
 *   brush size     W / S, up / down          d-pad up / down
 *
 * Holding a button keeps placing every frame, so sand pours and lines draw.
 * After draw_enter() nothing is placed until those buttons have all been let
 * go once, so the click or cross that chose Draw in the menu doesn't place.
 * Brush sizes are 1, 2, 4, 8, 16 and then the biggest square one WRITE_BOX
 * can carry (31 x 31 at N = 1024), so every placement is one command.
 * The bottom-left corner shows the material and the size, and an outline
 * at the cursor shows where the box goes.
 *
 *   draw_init(w, h);       once, after assets_init(), before the cursor's
 *                          sprite (so the cursor is drawn on top)
 *   draw_enter(); draw_leave();
 *   each game frame while drawing:
 *       if (!draw_frame(&in, cursor_x, cursor_y)) -> resync */

#include <stdbool.h>
#include <stdint.h>

#include "usb_input.h"

#define DRAW_SIZES_MAX 8

/* The brush sizes for this FPGA (from N), smallest first. Returns how many. */
unsigned draw_sizes(uint16_t out[DRAW_SIZES_MAX]);

void draw_init(uint16_t screen_w, uint16_t screen_h);
void draw_enter(void);
void draw_leave(void);
bool draw_active(void);

/* false: a WRITE_BOX failed (the game should resync) */
bool draw_frame(const input_t *in, int32_t cx, int32_t cy);

#endif