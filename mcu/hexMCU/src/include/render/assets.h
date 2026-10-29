#ifndef ASSETS_H
#define ASSETS_H

/* The game's images: which files on the SD card, which texts, which
 * generated ones. Made once by assets_init(), after sprite_init();
 * sprite_poll() then uploads them all in the background.
 *
 * Every entry can be NULL (no card, file missing, pool full -- logged);
 * the sprite functions take NULL and do nothing, so the game runs on. */

#include "sprite.h"

typedef enum { MAT_SAND, MAT_WATER, MAT_LIFE, MAT_COUNT } material_t;

typedef struct {
    sprite_img_t *cursor;                   /* sprites/cursor.spr, or a white square */
    sprite_img_t *title;                    /* test sentence                         */
    sprite_img_t *paused, *save_game, *load_game;
    sprite_img_t *material[MAT_COUNT];      /* names, for the element picker         */
} assets_t;

extern assets_t assets;

void assets_init(void);

#endif