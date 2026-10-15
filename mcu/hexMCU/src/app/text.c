/* text.c - text on the HUD layer (see text.h).
 *
 * The whole string is laid out into one pixel box first, then sent: whole
 * rows per command if a row fits fpga_max_payload(), else pieces of a row
 * (a full-width line of 640 px is 1280 bytes, more than one command). */

#define LOG_TAG "text"

#include <string.h>

#include "text.h"
#include "fpga.h"
#include "log.h"

static uint16_t min16(uint32_t a, uint32_t b) { return (uint16_t)(a < b ? a : b); }

static void put_px(uint8_t *dst, uint16_t v)     /* MSB first */
{
    dst[0] = (uint8_t)(v >> 8);
    dst[1] = (uint8_t)v;
}

/* ---- layout ----------------------------------------------------------- */
bool text_draw(text_t *t, int16_t x, int16_t y, const font_t *f, const char *s,
               uint16_t fg, uint16_t bg, uint8_t scale)
{
    if (t->active) return false;
    if (scale == 0) scale = 1;

    /* box = the string, cut to the screen and to the buffer */
    const fpga_info_t *scr = fpga_link_info();
    int32_t w = (int32_t)font_text_width(f, s) * scale;
    int32_t h = (int32_t)f->height * scale;
    if (x < 0 || y < 0 || x >= scr->width || y >= scr->height) return false;
    if (x + w > scr->width)  w = scr->width  - x;
    if (y + h > scr->height) h = scr->height - y;
    if (w > (int32_t)TEXT_MAX_W) w = TEXT_MAX_W;
    if (h > (int32_t)TEXT_MAX_H) h = TEXT_MAX_H;
    if (w <= 0 || h <= 0) return false;

    /* background, then the letters on top */
    for (int32_t i = 0; i < w * h; i++) put_px(&t->buf[i * PX_BYTES], bg);

    int32_t pen = 0;                            /* x of the current glyph */
    uint32_t cp;
    while ((cp = font_utf8_next(&s)) != 0 && pen < w) {
        const font_glyph_t *g = font_glyph(f, cp);
        for (int gy = 0; gy < f->height; gy++)
            for (int gx = 0; gx < g->width; gx++) {
                if (!font_pixel(f, g, gx, gy)) continue;
                for (int sy = 0; sy < scale; sy++)          /* one font pixel = */
                    for (int sx = 0; sx < scale; sx++) {    /* scale x scale    */
                        int32_t px = pen + gx * scale + sx, py = gy * scale + sy;
                        if (px < w && py < h)
                            put_px(&t->buf[(py * w + px) * PX_BYTES], fg);
                    }
            }
        pen += g->width * scale;
    }

    t->x = x;
    t->y = y;
    t->w = (uint16_t)w;
    t->h = (uint16_t)h;
    t->row = t->col = 0;
    t->pending = 0;
    t->failed = false;
    t->active = true;
    return true;
}

/* ---- sending ---------------------------------------------------------- */
static void text_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    text_t *t = cmd->ctx;
    if (t->pending) t->pending--;
    if (err != FPGA_ERR_NONE) t->failed = true;
}

bool text_poll(text_t *t)
{
    if (t->failed) return false;
    if (!t->active) return true;

    uint32_t max_px = fpga_max_payload(SEND_HUD_BOX_OPCODE) / PX_BYTES;
    if (max_px == 0) max_px = 1;                /* submit will say it's too big */

    while (t->row < t->h) {
        uint16_t rows, cols, x = (uint16_t)t->x, y = (uint16_t)(t->y + t->row);
        const uint8_t *data;
        if (max_px >= t->w) {                   /* whole rows */
            rows = min16(max_px / t->w, t->h - t->row);
            cols = t->w;
            data = &t->buf[(uint32_t)t->row * t->w * PX_BYTES];
        } else {                                /* a piece of one row */
            rows = 1;
            cols = min16(max_px, t->w - t->col);
            x    = (uint16_t)(x + t->col);
            data = &t->buf[((uint32_t)t->row * t->w + t->col) * PX_BYTES];
        }

        fpga_cmd_t c = fpga_cmd_send_hud_box(fpga_box(x, y, cols, rows), data,
                                             (uint16_t)(cols * rows * PX_BYTES));
        c.done = text_done;
        c.ctx  = t;
        fpga_status_t st = fpga_submit(&c);
        if (st == FPGA_EFULL) return true;      /* same command next poll */
        if (st != FPGA_OK) {
            LOG_WARN("submit: %s", fpga_status_str(st));
            t->failed = true;
            return false;
        }
        t->pending++;

        if (cols < t->w) {                      /* move on: piece, then row */
            t->col = (uint16_t)(t->col + cols);
            if (t->col < t->w) continue;
            t->col = 0;
        }
        t->row = (uint16_t)(t->row + rows);
    }

    if (t->pending == 0) t->active = false;     /* all sent and all back OK */
    return true;
}

bool text_busy(const text_t *t)
{
    return t->active;
}

void text_lost(text_t *t)
{
    t->active  = false;
    t->failed  = false;
    t->pending = 0;
}