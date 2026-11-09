/* assets.c - the game's images (see assets.h). Add new ones here. */

#define LOG_TAG "assets"

#include "assets.h"
#include "log.h"

/* The font for texts. Set it in one place; the font's own .c defines it. */
#ifndef ASSET_FONT
#define ASSET_FONT font_kiwisoda
#endif
extern const font_t ASSET_FONT;

#define TEXT_SCALE   2
#define TEXT_COLOUR  RGB565(255, 220, 0)

assets_t assets;

static const char *const material_names[MAT_COUNT] = {
    [MAT_SAND]  = "Sand",
    [MAT_WATER] = "Water",
    [MAT_LIFE]  = "Game of Life",
};

static sprite_img_t *text(const char *s)
{
    return sprite_img_text(&ASSET_FONT, s, TEXT_COLOUR, TEXT_SCALE);
}

void assets_init(void)
{
    /* files on the card, with a stand-in if one is missing */
    assets.cursor = sprite_img_file("sprites/cursor.spr", 0);
    if (!assets.cursor) assets.cursor = sprite_img_solid(8, 8, RGB565(255, 255, 255));

    /* texts: one image per text */
    assets.title     = text("Hei verden! Lagre spill? æøå ÆØÅ");
    assets.paused    = text("Paused");
    assets.save_game = text("Save game");
    assets.load_game = text("Load");
    for (unsigned i = 0; i < MAT_COUNT; i++) assets.material[i] = text(material_names[i]);

    LOG_INFO("%u images queued for the FPGA", sprite_pending());
}