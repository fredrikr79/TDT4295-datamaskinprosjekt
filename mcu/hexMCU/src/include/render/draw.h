#ifndef DRAW_H
#define DRAW_H

/* Drawing mode: put material into the running simulation with the mouse or
 * a controller. Each placement is one WRITE_BOX of the brush size, centred
 * on the cursor and clipped at the screen's edges. Its cells are textured
 * like a loaded room's (texture.h), so drawn sand and stone don't look flat.
 *
 *                  keyboard / mouse          controller
 *   place          left button (held)        cross or R2 (held)
 *   erase (air)    right button (held)       circle or L2 (held)
 *   element        0-8, scroll wheel         L1 / R1, d-pad left / right
 *                  (draw_elements[] below: Air, Stone, Igneous rock, Sand...)
 *   brush size     W / S, up / down          d-pad up / down
 *   pause / run    Enter, middle button      square
 *
 * Holding a button keeps placing every frame, so sand pours and lines draw.
 * After draw_enter() nothing is placed until those buttons have all been let
 * go once, so the click or cross that chose Draw in the menu doesn't place.
 * Brush sizes count grid CELLS: 1, 2, 4, 8, 16 and then the biggest square
 * one WRITE_BOX can carry (31 x 31 at N = 1024), so every placement is one
 * command. The cursor moves in screen pixels; the brush is centred on the
 * cell under it (fpga_px_to_cell()), so size 1 is one byte of payload and
 * shows as one FPGA_SIM_SCALE x FPGA_SIM_SCALE (4x4) block.
 * The bottom-left corner shows the material and the size, the bottom-right
 * corner whether the simulation is paused or running, and an outline
 * around the cells under the cursor shows where the box goes; it snaps
 * from cell to cell while the cursor moves smoothly.
 *
 * Drawing starts paused. draw.c only shows the state and asks for changes
 * (DRAW_PAUSE / DRAW_RUN); game.c sends STOP_SIM / START_SIM.
 *
 *   draw_init(w, h);       once, after assets_init(), before the cursor's
 *                          sprite (so the cursor is drawn on top)
 *   draw_enter(); draw_leave();
 *   each game frame while drawing:
 *       switch (draw_frame(&in, cursor_x, cursor_y)) { ... } */

#include <stdbool.h>
#include <stdint.h>

#include "usb_input.h"

#define DRAW_SIZES_MAX 8

/* What can be drawn: a name, and the cell byte a new cell of it starts
 * as. Not just the materials: stone and igneous rock are one material
 * told apart by a low bit (fpga.h), and life and explosive need theirs
 * set. In picker order; the number keys pick 0 .. 8. */
typedef struct {
    const char *name;
    uint8_t     cell;
} draw_element_t;

#define DRAW_ELEMENTS 9
extern const draw_element_t draw_elements[DRAW_ELEMENTS];

typedef enum {
    DRAW_OK,            /* nothing for the game to do             */
    DRAW_FAILED,        /* a WRITE_BOX failed: resync             */
    DRAW_PAUSE,         /* the player paused the simulation: STOP_SIM  */
    DRAW_RUN,           /* the player started it: START_SIM       */
} draw_event_t;

/* The brush sizes for this FPGA (from N), in cells, smallest first.
 * Returns how many. */
unsigned draw_sizes(uint16_t out[DRAW_SIZES_MAX]);

void draw_init(uint16_t screen_w, uint16_t screen_h);  /* pixels; the grid is / FPGA_SIM_SCALE */
void draw_enter(void);                  /* shows "Paused": the sim must be stopped */
void draw_leave(void);
bool draw_active(void);

/* cx, cy: the cursor in screen pixels */
draw_event_t draw_frame(const input_t *in, int32_t cx, int32_t cy);

#endif