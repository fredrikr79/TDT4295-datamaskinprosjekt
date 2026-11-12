#ifndef MENU_H
#define MENU_H

/* The pause menu: a panel centred on the screen, built from sprites.
 *
 *     +----------------------+
 *     |      Sim paused      |    info, not a button
 *     |  ------------------  |
 *     |  [      Save      ]  |
 *     |  [      Load      ]  |    load the room again, without saving
 *     |  [  Restart room  ]  |    the room as designed (the save is deleted)
 *     |  [      Draw      ]  |    drawing mode
 *     +----------------------+
 *
 * The button under the mouse cursor is highlighted, or the one picked with
 * the arrow keys or the d-pad. A left click, Enter or cross chooses it;
 * Esc, the PS button or circle closes the menu. menu_frame() only
 * says what was chosen; game.c does it (and pauses / starts the simulation).
 *
 *   menu_init(w, h);          once, after assets_init(), before the cursor's
 *                             sprite (sprites made later are drawn on top)
 *   menu_open(); menu_close();
 *   each game frame while open:
 *       switch (menu_frame(&in, cursor_x, cursor_y)) { ... }
 *
 * Its images can't be freed (FPGA sprite memory), so it's made once. */

#include <stdbool.h>
#include <stdint.h>

#include "usb_input.h"

typedef enum {
    MENU_NONE,          /* nothing chosen this frame              */
    MENU_CLOSE,         /* Esc: back to the game                  */
    MENU_SAVE,
    MENU_LOAD,
    MENU_RESTART,
    MENU_DRAW,
} menu_action_t;

/* Builds the panel for a screen_w x screen_h screen, all hidden. */
void menu_init(uint16_t screen_w, uint16_t screen_h);

void menu_open(void);
void menu_close(void);
bool menu_is_open(void);

/* Once per game frame while open. (cx, cy) is the cursor's tip on the
 * screen. Returns what was chosen, MENU_NONE if nothing yet. */
menu_action_t menu_frame(const input_t *in, int32_t cx, int32_t cy);

const char *menu_action_name(menu_action_t a);

#endif