/* assets.c - the game's images (see assets.h). Add new ones here. */

#include "sprite.h"
#include <stdint.h>
#define LOG_TAG "assets"

#include "assets.h"
#include "player.h"                         /* PLAYER_SIZE_X, PLAYER_SIZE_Y */
#include "log.h"

#include <stdio.h>

/* The font for texts. Set it in one place; the font's own .c defines it. */
#ifndef ASSET_FONT
#define ASSET_FONT font_kiwisoda
#endif
extern const font_t ASSET_FONT;

#define TEXT_SCALE   1
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

const char *material_name(material_t m)
{
    return (unsigned)m < MAT_COUNT ? material_names[m] : "?";
}

/* ---- the player ------------------------------------------------------------
 * 4*PLAYER_SIZE_X x 4*PLAYER_SIZE_Y pixels, one letter per pixel, looked up
 * in player_colour() below: the hitbox (player.h) is in grid cells, and a
 * cell is 4x4 screen pixels (FPGA_SIM_SCALE), so the picture covers it
 * exactly. Edit away; a row that is too long warns and is cut off, a short
 * one is see-through at the end. */
static const char player_art[4*PLAYER_SIZE_Y][4*PLAYER_SIZE_X + 1] = {
    "    KKKKKKKK    ",
    "    KKKKKKKK    ",
    "  KKYYYYYYYYKK  ",
    "  KKYYYYYYYYKK  ",
    "KKYYYYYYYYYYYYKK",
    "KKYYYYYYYYYYYYKK",
    "KKYYKKYYYYKKYYKK",     /* eyes   */
    "KKYYKKYYYYKKYYKK",     /* eyes   */
    "KKPPYYYYYYYYPPKK",     /* cheeks */
    "KKPPYYYYYYYYPPKK",     /* cheeks */
    "KKYYKKKKKKKKYYKK",     /* mouth  */
    "KKYYKKKKKKKKYYKK",     /* mouth  */
    "  KKYYYYYYYYKK  ",
    "  KKYYYYYYYYKK  ",
    "    KKKKKKKK    ",
    "    KKKKKKKK    ",
    "  KKBBBBBBBBKK  ",
    "  KKBBBBBBBBKK  ",
    "KKBBBBBBBBBBBBKK",
    "KKBBBBBBBBBBBBKK",
    "KKBBKKBBBBKKBBKK",
    "KKBBKKBBBBKKBBKK",
    "KKYYKKBBBBKKYYKK",     /* hands  */
    "KKYYKKBBBBKKYYKK",     /* hands  */
    "  KKDDDDDDDDKK  ",
    "  KKDDDDDDDDKK  ",
    "  KKDDKKKKDDKK  ",
    "  KKDDKKKKDDKK  ",
    "  KKDDKKKKDDKK  ",
    "  KKDDKKKKDDKK  ",
    "KKKKKK    KKKKKK",     /* shoes  */
    "KKKKKK    KKKKKK",     /* shoes  */
};

static uint16_t player_colour(char c)
{
    switch (c) {
    case 'K': return RGB565( 20,  20,  30);     /* outline, eyes, mouth */
    case 'Y': return RGB565(255, 200,  40);     /* face, hands          */
    case 'P': return RGB565(255, 120, 140);     /* cheeks               */
    case 'B': return RGB565( 60, 120, 220);     /* shirt                */
    case 'D': return RGB565( 40,  40,  90);     /* trousers             */
    default:  return SPRITE_CLEAR;              /* ' ': see-through     */
    }
}

/* Drawn straight from the art while it uploads (no pixel buffer). */
static void player_px(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    for (uint32_t i = 0; i < len; i += PX_BYTES) {
        uint32_t p = (offset + i) / PX_BYTES;
        uint16_t colour = player_colour(player_art[p / (4*PLAYER_SIZE_X)][p % (4*PLAYER_SIZE_X)]);
        buf[i]     = (uint8_t)(colour >> 8);
        buf[i + 1] = (uint8_t)colour;
    }
}

static void explosive_proj_px(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    for (uint32_t i = 0; i < len; i += PX_BYTES) {
        uint16_t colour = RGB565(235, 70, 60);
        buf[i]     = (uint8_t)(colour >> 8);
        buf[i + 1] = (uint8_t)colour;
    }
}

static void gol_proj_px(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    for (uint32_t i = 0; i < len; i += PX_BYTES) {
        uint16_t colour = RGB565(250, 250, 250);
        buf[i]     = (uint8_t)(colour >> 8);
        buf[i + 1] = (uint8_t)colour;
    }
}

static sprite_img_t *text(const char *s)
{
    return sprite_img_text(&ASSET_FONT, s, TEXT_COLOUR, TEXT_SCALE);
}

void assets_init(void)
{
    /* files on the card, with a stand-in if one is missing */
    assets.cursor = sprite_img_file("sprites/cursor.spr", 0);
    if (!assets.cursor) assets.cursor = sprite_img_solid(8, 8, RGB565(255, 255, 255));

    /* generated */
    assets.player = sprite_img_fill(4*PLAYER_SIZE_X, 4*PLAYER_SIZE_Y, player_px, NULL);
    assets.explosive_proj = sprite_img_fill(
        4*EXPLOSION_PROJ_SIZE_X, 4*EXPLOSION_PROJ_SIZE_Y,
        explosive_proj_px, NULL
    );
    assets.gol_proj = sprite_img_fill(
        4*GOL_PROJ_SIZE_X, 4*GOL_PROJ_SIZE_Y,
        gol_proj_px, NULL
    );

    /* texts: one image per text */
    assets.title     = text("Hei verden! Lagre spill? æøå ÆØÅ");
    assets.menu_title   = text("Sim paused");
    assets.menu_save    = text("Save");
    assets.menu_load    = text("Load");
    assets.menu_restart = text("Restart room");
    assets.menu_draw    = text("Draw");
    for (unsigned i = 0; i < DRAW_ELEMENTS; i++) assets.element[i] = text(draw_elements[i].name);

    /* drawing mode: the brush sizes, which depend on the FPGA's N (known by now) */
    uint16_t sizes[DRAW_SIZES_MAX];
    unsigned n = draw_sizes(sizes);
    for (unsigned i = 0; i < n; i++) {
        char s[16];
        snprintf(s, sizeof s, "%ux%u", sizes[i], sizes[i]);
        assets.draw_size[i] = text(s);
    }
    assets.draw_paused  = text("Paused");
    assets.draw_running = text("Running");

    LOG_INFO("%u images queued for the FPGA", sprite_pending());
}