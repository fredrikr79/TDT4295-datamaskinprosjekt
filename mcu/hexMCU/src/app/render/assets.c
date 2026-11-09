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

/* One per material_t (fpga.h). A new material there without a name here
 * fails to compile (see the check below). */
static const char *const material_names[] = {
    [MAT_AIR]       = "Air",
    [MAT_STONE]     = "Stone",
    [MAT_SAND]      = "Sand",
    [MAT_WATER]     = "Water",
    [MAT_LAVA]      = "Lava",
    [MAT_ACID]      = "Acid",
    [MAT_LIFE]      = "Game of Life",
    [MAT_EXPLOSIVE] = "Explosive",
};
_Static_assert(sizeof material_names / sizeof material_names[0] == MAT_COUNT,
               "a material in fpga.h has no name in material_names");

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
