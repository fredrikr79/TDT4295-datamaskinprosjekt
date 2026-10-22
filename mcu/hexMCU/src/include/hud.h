#ifndef HUD_H
#define HUD_H

/* The HUD layer as FPGA sprites. Portable: MCU and host build.
 *
 * An image is pixels the FPGA keeps in its sprite memory (WRITE_SPRITE,
 * gives a memID). A sprite is an instance of an image on the screen
 * (SPRITE_WRITE, gives a spriteID), which can be moved, hidden or given
 * another image of the same size (SPRITE_UPDATE).
 *
 * The game only says what it wants; hud_poll() sends whatever it takes
 * to get the FPGA there -- upload, create, update, delete -- one command
 * per object at a time, and does it all again after a resync or a new
 * FPGA session:
 *
 *   static uint8_t     px[8 * 8 * PX_BYTES];
 *   static hud_image_t img;
 *   static hud_sprite_t spr;
 *
 *   hud_fill(px, 8, 8, RGB565(255, 255, 255));
 *   hud_image_init(&img, px, 8, 8);
 *   hud_sprite_init(&spr, &img, x, y, true);
 *   ...
 *   hud_sprite_move(&spr, x, y);              whenever
 *   every poll:     if (!hud_poll()) -> resync
 *   on resync, after fpga_abort_all():  hud_lost()
 *   new FPGA session:                   hud_forget()
 *
 * Pixel buffers are sent straight from memory by DMA: keep them valid
 * (static) and unchanged.
 *
 * Budget: the FPGA's sprite memory is append-only and memIDs are few
 * (HUD_MEMIDS). An image takes one memID per command it needs --
 * fpga_max_payload(WRITE_SPRITE_OPCODE) bytes each, about 1 KB, so a
 * 16x16 image takes one and a 32x32 one takes two. Uploads that fail
 * cost memIDs too (memory can't be rewritten). An image that doesn't fit
 * any more is left out with an error, not retried. */

#include <stdbool.h>
#include <stdint.h>

#include "fpga.h"

/* ---- what the MCU and the FPGA agree on --------------------------------- */
#define PX_BYTES        2u          /* RGB565, MSB first on the wire     */
#define HUD_CLEAR       0xF81Fu     /* see-through (magenta)             */
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

/* memIDs the FPGA has per session (its LUT has 64 entries, 0 = none). */
#ifndef HUD_MEMIDS
#define HUD_MEMIDS      63u
#endif

typedef struct hud_image {
    const uint8_t *px;              /* w*h pixels, RGB565 MSB first      */
    uint16_t       w, h;
    uint8_t        mem_id;          /* first memID, valid when uploaded  */
    uint8_t        state;           /* (hud.c)                           */
    uint8_t        chunks;          /* WRITE_SPRITEs it takes            */
    uint8_t        sent, acked;     /* ...submitted / come back OK       */
    uint16_t       chunk_bytes;
    struct hud_image *next;
} hud_image_t;

typedef struct hud_sprite {
    /* what the game wants */
    hud_image_t *img;
    int16_t      x, y;
    bool         visible;
    bool         remove;            /* hud_sprite_remove() was called    */

    /* what the FPGA has, as far as we know (hud.c) */
    uint8_t      state;
    uint8_t      id;                /* spriteID, valid when live         */
    hud_image_t *s_img;
    int16_t      s_x, s_y;
    bool         s_visible;
    bool         dirty;             /* an update failed: resend it       */
    hud_image_t *p_img;             /* the command in flight sends these */
    int16_t      p_x, p_y;
    bool         p_visible;
    struct hud_sprite *next;
} hud_sprite_t;

/* Fill w*h pixels of px with one colour (HUD_CLEAR: see-through). */
void hud_fill(uint8_t *px, uint16_t w, uint16_t h, uint16_t colour);
void hud_set_px(uint8_t *px, uint16_t w, uint16_t x, uint16_t y, uint16_t colour);

/* Register an image: uploaded by hud_poll() when a sprite needs it. */
void hud_image_init(hud_image_t *img, const uint8_t *px, uint16_t w, uint16_t h);

/* Register a sprite: created by hud_poll() once its image is uploaded. */
void hud_sprite_init(hud_sprite_t *s, hud_image_t *img, int16_t x, int16_t y, bool visible);
void hud_sprite_move(hud_sprite_t *s, int16_t x, int16_t y);
void hud_sprite_show(hud_sprite_t *s, bool visible);
/* Another image of the SAME size (the FPGA can't change W,H). */
bool hud_sprite_set_image(hud_sprite_t *s, hud_image_t *img);
/* Delete it on the FPGA and forget it; the struct is free once
 * hud_sprite_gone() says so. */
void hud_sprite_remove(hud_sprite_t *s);
bool hud_sprite_gone(const hud_sprite_t *s);

/* Sends what's needed, as far as the FPGA queue takes it. Call it every
 * poll. Returns false when a command failed: resync, then hud_lost(). */
bool hud_poll(void);

/* After fpga_abort_all(): whatever was in flight may or may not have
 * happened; hud_poll() redoes it. */
void hud_lost(void);

/* New FPGA session: sprite memory and instances are gone; hud_poll()
 * uploads and creates everything again. */
void hud_forget(void);

/* memIDs used this session as far as the MCU can tell, uploads in flight
 * included. Exact after an OK answer; a lost one counts as used. */
unsigned hud_memids_used(void);

#endif