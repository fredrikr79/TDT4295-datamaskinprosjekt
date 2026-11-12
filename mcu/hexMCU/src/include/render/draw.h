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
 *   pause / run    Enter, middle button      square
 *
 * Holding a button keeps placing every frame, so sand pours and lines draw.
 * After draw_enter() nothing is placed until those buttons have all been let
 * go once, so the click or cross that chose Draw in the menu doesn't place.
 * Brush sizes are 1, 2, 4, 8, 16 and then the biggest square one WRITE_BOX
 * can carry (31 x 31 at N = 1024), so every placement is one command.
 * The bottom-left corner shows the material and the size, the bottom-right
 * corner whether the simulation is paused or running, and an outline at
 * the cursor shows where the box goes.
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

typedef enum {
    DRAW_OK,            /* nothing for the game to do             */
    DRAW_FAILED,        /* a WRITE_BOX failed: resync             */
    DRAW_PAUSE,         /* the player paused the simulation: STOP_SIM  */
    DRAW_RUN,           /* the player started it: START_SIM       */
} draw_event_t;

/* The brush sizes for this FPGA (from N), smallest first. Returns how many. */
unsigned draw_sizes(uint16_t out[DRAW_SIZES_MAX]);

void draw_init(uint16_t screen_w, uint16_t screen_h);
void draw_enter(void);                  /* shows "Paused": the sim must be stopped */
void draw_leave(void);
bool draw_active(void);

draw_event_t draw_frame(const input_t *in, int32_t cx, int32_t cy);

#endif