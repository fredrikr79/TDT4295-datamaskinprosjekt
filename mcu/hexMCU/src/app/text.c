/* text.c - text on the HUD as FPGA sprites (see text.h). */

#define LOG_TAG "text"

#include <string.h>

#include "text.h"
#include "fpga.h"
#include "log.h"

/* ---- letter images, shared by all texts ---------------------------------- */
static struct {
    hud_image_t  img;
    const font_t *font;
    uint32_t     cp;
    uint16_t     colour;
    uint8_t      scale;
} glyphs[TEXT_GLYPH_MAX];
static unsigned n_glyphs;

static uint8_t  arena[TEXT_ARENA_BYTES];    /* their pixels, never freed */
static uint32_t arena_used;

static bool blank(const font_t *f, const font_glyph_t *g)
{
    for (int y = 0; y < f->height; y++)
        for (int x = 0; x < g->width; x++)
            if (font_pixel(f, g, x, y)) return false;
    return true;
}

/* The image for this letter, made on first use. NULL: nothing to draw
 * (a space) or no room left. */
static hud_image_t *glyph_image(const font_t *f, uint32_t cp, uint16_t colour, uint8_t scale)
{
    for (unsigned i = 0; i < n_glyphs; i++)
        if (glyphs[i].font == f && glyphs[i].cp == cp &&
            glyphs[i].colour == colour && glyphs[i].scale == scale)
            return &glyphs[i].img;

    const font_glyph_t *g = font_glyph(f, cp);
    if (g->width == 0 || blank(f, g)) return NULL;

    uint16_t w = (uint16_t)(g->width * scale), h = (uint16_t)(f->height * scale);
    uint32_t bytes = (uint32_t)w * h * PX_BYTES;
    if (n_glyphs >= TEXT_GLYPH_MAX || arena_used + bytes > sizeof arena) {
        LOG_WARN("no room for more letter images (%u, %lu bytes)",
                 n_glyphs, (unsigned long)arena_used);
        return NULL;
    }

    uint8_t *px = &arena[arena_used];
    arena_used += bytes;
    hud_fill(px, w, h, HUD_CLEAR);                       /* see-through ... */
    for (int y = 0; y < f->height; y++)
        for (int x = 0; x < g->width; x++) {
            if (!font_pixel(f, g, x, y)) continue;       /* ... but the letter */
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++)
                    hud_set_px(px, w, (uint16_t)(x * scale + sx),
                               (uint16_t)(y * scale + sy), colour);
        }

    glyphs[n_glyphs].font   = f;
    glyphs[n_glyphs].cp     = g->cp;
    glyphs[n_glyphs].colour = colour;
    glyphs[n_glyphs].scale  = scale;
    hud_image_init(&glyphs[n_glyphs].img, px, w, h);
    return &glyphs[n_glyphs++].img;
}

/* ---- texts ----------------------------------------------------------------- */
bool text_ready(const text_t *t)
{
    for (unsigned i = 0; i < t->n; i++)
        if (!hud_sprite_gone(&t->spr[i])) return false;
    return true;
}

bool text_draw(text_t *t, int16_t x, int16_t y, const font_t *f, const char *s,
               uint16_t colour, uint8_t scale)
{
    if (!text_ready(t)) return false;
    if (scale == 0) scale = 1;
    t->n = 0;

    const fpga_info_t *scr = fpga_link_info();
    int32_t pen = x;
    uint32_t cp;
    while ((cp = font_utf8_next(&s)) != 0) {
        const font_glyph_t *g = font_glyph(f, cp);
        int32_t gw = g->width * scale, gh = f->height * scale;
        bool on_screen = pen >= 0 && y >= 0 && pen + gw <= scr->width &&
                         y + gh <= scr->height;
        hud_image_t *img = on_screen ? glyph_image(f, cp, colour, scale) : NULL;
        if (img) {
            if (t->n >= TEXT_MAX_CHARS) {
                LOG_WARN("text longer than %u letters, cut", TEXT_MAX_CHARS);
                break;
            }
            hud_sprite_init(&t->spr[t->n++], img, (int16_t)pen, y, true);
        }
        pen += gw;
    }
    t->w = (int16_t)(pen - x);
    t->h = (int16_t)(f->height * scale);
    return true;
}

void text_show(text_t *t, bool visible)
{
    for (unsigned i = 0; i < t->n; i++) hud_sprite_show(&t->spr[i], visible);
}

void text_clear(text_t *t)
{
    for (unsigned i = 0; i < t->n; i++) hud_sprite_remove(&t->spr[i]);
}