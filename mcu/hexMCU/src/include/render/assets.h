#ifndef ASSETS_H
#define ASSETS_H

/* The game's images: which files on the SD card, which texts, which
 * generated ones. Made once by assets_init(), after sprite_init();
 * sprite_poll() then uploads them all in the background.
 *
 * Every entry can be NULL (no card, file missing, pool full -- logged);
 * the sprite functions take NULL and do nothing, so the game runs on. */

#include "sprite.h"
#include "fpga.h"                           /* material_t, MAT_COUNT (the FPGA's IDs) */
#include "draw.h"                           /* DRAW_SIZES_MAX */

typedef struct {
    sprite_img_t *cursor;                   /* sprites/cursor.spr, or a white square */
    sprite_img_t *title;                    /* test sentence                         */
    sprite_img_t *player;                   /* PLAYER_SIZE_X x _Y, the hitbox (player.c) */
    /* the pause menu (menu.c) */
    sprite_img_t *menu_title;               /* "Sim paused", not a button            */
    sprite_img_t *menu_save, *menu_load, *menu_restart, *menu_draw;
    sprite_img_t *material[MAT_COUNT];      /* names, for the element picker, by material_t */
    sprite_img_t *draw_size[DRAW_SIZES_MAX]; /* "1x1", "2x2" ... by draw_sizes()           */
    sprite_img_t *draw_paused, *draw_running;  /* the simulation, while drawing          */
} assets_t;

extern assets_t assets;

void assets_init(void);

#endif