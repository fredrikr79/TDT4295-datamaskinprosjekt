#ifndef FONT_H
#define FONT_H

/* 1-bit bitmap fonts, made from pixel TTFs by tools/ttf2c.py. Portable.
 *
 * Every glyph is `height` rows of `row_bytes` bytes; bit x of a row (LSB
 * of the first byte = x 0) is pixel x. `width` is the advance: where the
 * next glyph starts. Strings are UTF-8, so "Lagre spill" and "ø" just work.
 *
 *   const font_glyph_t *g = font_glyph(&font_kiwisoda, 'A');
 *   if (font_pixel(&font_kiwisoda, g, x, y)) ...       */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t cp;            /* Unicode code point                    */
    uint8_t  width;         /* advance in pixels                     */
} font_glyph_t;

typedef struct {
    uint8_t  height;        /* rows per glyph = line height          */
    uint8_t  baseline;      /* rows from the top down to the baseline */
    uint8_t  row_bytes;     /* bytes per glyph row in bits[]         */
    uint16_t count;
    const font_glyph_t *glyphs;   /* 32..126 first and in order, then the rest sorted */
    const uint8_t      *bits;     /* glyph i starts at i * height * row_bytes */
} font_t;

/* The glyph for a code point; '?' if the font doesn't have it. */
const font_glyph_t *font_glyph(const font_t *f, uint32_t cp);

/* Is pixel (x, y) of the glyph set? Outside the glyph: false. */
bool font_pixel(const font_t *f, const font_glyph_t *g, int x, int y);

/* Next code point of a UTF-8 string, moving *s past it; 0 at the end.
 * Broken sequences come back as '?'. */
uint32_t font_utf8_next(const char **s);

/* Width in pixels of a UTF-8 string set in this font (one line). */
int font_text_width(const font_t *f, const char *s);

extern const font_t font_kiwisoda;

#endif