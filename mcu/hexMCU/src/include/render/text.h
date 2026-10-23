#ifndef TEXT_H
#define TEXT_H

/* Text on the HUD, as FPGA sprites. Portable: MCU and host build.
 *
 * Each letter is a sprite. A letter's image (font, character, colour,
 * scale) is uploaded once and shared by every text that uses it, so
 * "Hei verden" uploads 'e' once. Spaces and other empty glyphs cost
 * nothing.
 *
 *   static text_t title;
 *   text_draw(&title, x, y, &font_pressstart, "Hei verden!", RGB565(255, 220, 0), 2);
 *   text_show(&title, false);       hide / show again
 *   text_clear(&title);             delete; text_draw() works again once
 *                                   text_ready() says the old letters are gone
 *
 * The letters are sent by hud_poll(), which also redraws them after a
 * resync or a new FPGA session -- nothing to do here for that.
 *
 * Budget: every different letter image takes FPGA memIDs, which are few
 * (HUD_MEMIDS, about 63 per session): one for a letter up to about 1 KB
 * of pixels (22x22 px), two up to 2 KB, and so on. A sentence uses a
 * memID per DIFFERENT letter, not per letter. Plan the letters a screen
 * shows; a whole alphabet in two sizes won't fit. */

#include <stdbool.h>
#include <stdint.h>

#include "font.h"
#include "hud.h"

#define TEXT_MAX_CHARS    48u       /* letters per text_t             */
#define TEXT_GLYPH_MAX    96u       /* different letter images in all */
#define TEXT_ARENA_BYTES  (64u * 1024u)   /* their pixels             */

typedef struct {
    hud_sprite_t spr[TEXT_MAX_CHARS];
    uint8_t      n;                 /* sprites in use                 */
    int16_t      w, h;              /* size of the laid-out text      */
} text_t;

/* Lay the string out with its top-left corner at x, y and create its
 * letters. Returns false if this text_t still has letters (text_clear()
 * first, then wait for text_ready()). Letters off the screen are left out. */
bool text_draw(text_t *t, int16_t x, int16_t y, const font_t *f, const char *s,
               uint16_t colour, uint8_t scale);

void text_show(text_t *t, bool visible);
void text_clear(text_t *t);
bool text_ready(const text_t *t);   /* no letters left: text_draw() works */

#endif