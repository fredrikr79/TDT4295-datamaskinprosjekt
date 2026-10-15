#ifndef TEXT_H
#define TEXT_H

/* Text on the FPGA's HUD layer. Portable: MCU and host build.
 *
 * text_draw() lays a UTF-8 string out in a font into a pixel box, then
 * text_poll() sends that box to the FPGA, a few commands at a time:
 *
 *   static text_t title;
 *   text_draw(&title, x, y, &font_pressstart, "Hei verden!", fg, HUD_CLEAR, 2);
 *   every poll:   if (!text_poll(&title)) -> resync
 *   on resync, after fpga_abort_all():   text_lost(&title), then draw again
 *
 * bg = HUD_CLEAR: the letters float on the SIM layer. Any other colour
 * fills the box behind them. scale 2 draws every font pixel as 2x2.
 * The box is clipped to the screen and to TEXT_MAX_W x TEXT_MAX_H. */

#include <stdbool.h>
#include <stdint.h>

#include "font.h"
#include "render.h"         /* PX_BYTES, HUD_CLEAR, RGB565 */

#define TEXT_MAX_W  640u    /* biggest box, pixels */
#define TEXT_MAX_H  64u

typedef struct {
    int16_t  x, y;
    uint16_t w, h;          /* the box as laid out, after clipping */
    bool     active;        /* being sent                          */
    bool     failed;        /* a command of it came back with an error */
    uint16_t row, col;      /* where in the box sending has got to */
    uint16_t pending;       /* submitted, done not called yet      */
    uint8_t  buf[TEXT_MAX_W * TEXT_MAX_H * PX_BYTES];
} text_t;

/* Lay out and start sending. Returns false if the last one is still being
 * sent, or nothing of it is on the screen. Width: font_text_width() x scale. */
bool text_draw(text_t *t, int16_t x, int16_t y, const font_t *f, const char *s,
               uint16_t fg, uint16_t bg, uint8_t scale);

/* Sends what fits into the FPGA queue. Call it every poll. Returns false
 * when a command failed: resync, then text_lost(). */
bool text_poll(text_t *t);

bool text_busy(const text_t *t);

/* After fpga_abort_all(): the box may be half drawn, draw it again. */
void text_lost(text_t *t);

#endif