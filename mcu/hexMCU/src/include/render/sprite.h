#ifndef SPRITE_H
#define SPRITE_H

/* Sprites: images in the FPGA's sprite memory and the sprites that show
 * them on the HUD layer. Portable: MCU and host build. Replaces hud.c and
 * text.c.
 *
 * An IMAGE is pixels the FPGA keeps (WRITE_SPRITE -> memID). It comes from
 *   a file on the SD card       sprite_img_file("sprites/cursor.spr", 0)
 *   a text, one image per text  sprite_img_text(&font, "Game of Life", colour, 2)
 *   one colour                  sprite_img_solid(16, 16, RGB565(255, 0, 0))
 *   pixels in RAM               sprite_img_ram(px, w, h)
 *   a function you write        sprite_img_fill(w, h, fn, ctx)
 * None of them keeps its pixels in RAM: sprite_poll() makes them chunk by
 * chunk while uploading (reads the file, draws the letters...), and does
 * it again after a new FPGA session. RAM images are sent straight from
 * their buffer, which must stay valid.
 *
 * Images are uploaded in the background as soon as the FPGA is up,
 * whether a sprite uses them yet or not -- those a sprite waits for go
 * first. FPGA sprite memory can't be freed, so neither can images: make
 * them once (at start, see assets.c), not every frame.
 *
 * A SPRITE is an instance of an image on the screen (SPRITE_WRITE ->
 * spriteID): moved, shown or hidden (SPRITE_UPDATE), deleted. Its image
 * and size are fixed. Many sprites can share one image.
 *
 * An ANIMATION is a sprite per frame, all made up front; only the current
 * frame is shown, and sprite_frame() steps it every `ticks` game frames.
 *
 *   sprite_init();                              once, before anything else
 *   sprite_img_t *img = sprite_img_file("sprites/cursor.spr", 0);
 *   sprite_t *cur = sprite_new(img, x, y, true);
 *   ...
 *   sprite_move(cur, x, y);                     whenever, as often as you like
 *   every game frame:  sprite_frame(n);
 *   every poll:        if (!sprite_poll()) -> resync
 *   on resync, after fpga_abort_all():  sprite_lost();
 *   new FPGA session:                   sprite_forget();
 *
 * Handles are NULL when something couldn't be made (pool full, no card,
 * no file -- logged), and every function takes NULL and does nothing, so
 * a missing sprite never crashes the game. */

#include <stdbool.h>
#include <stdint.h>

#include "font.h"

/* ---- what the MCU and the FPGA agree on --------------------------------- */
#define PX_BYTES        2u          /* RGB565, MSB first on the wire     */
#define SPRITE_CLEAR    0xF81Fu     /* see-through (magenta)             */
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

/* ---- sizes (override with -D) ---------------------------------------------- */
#ifndef SPRITE_IMG_MAX
#define SPRITE_IMG_MAX      256u    /* images                            */
#endif
#ifndef SPRITE_MAX
#define SPRITE_MAX          256u    /* sprites                           */
#endif
#ifndef SPRITE_ANI_MAX
#define SPRITE_ANI_MAX      16u     /* animations                        */
#endif
#ifndef SPRITE_ANI_FRAMES
#define SPRITE_ANI_FRAMES   16u     /* frames per animation              */
#endif
#ifndef SPRITE_NAME_MAX
#define SPRITE_NAME_MAX     40u     /* file path / text, bytes incl. NUL */
#endif
/* Pixels are made into these buffers before they go out. A chunk (one
 * WRITE_SPRITE, one memID) is at most this big, so it should be at least
 * the FPGA's payload limit, N-1 (1023 for N = 1024). */
#ifndef SPRITE_STAGE_BYTES
#define SPRITE_STAGE_BYTES  1024u
#endif
#ifndef SPRITE_STAGE_BUFS
#define SPRITE_STAGE_BUFS   4u      /* chunks in flight at once          */
#endif
/* memIDs the FPGA has per session (LUT of 1024, assumed 0 = none). */
#ifndef SPRITE_MEMIDS
#define SPRITE_MEMIDS       1023u
#endif

/* ---- pixel buffers (for RAM images) --------------------------------------- */
void sprite_px_fill(uint8_t *px, uint16_t w, uint16_t h, uint16_t colour);
void sprite_px_set(uint8_t *px, uint16_t w, uint16_t x, uint16_t y, uint16_t colour);

/* ---- images ----------------------------------------------------------------- */
typedef struct sprite_img sprite_img_t;

/* Writes len bytes of the image's pixel stream, starting at byte offset
 * (row after row, RGB565 MSB first) into buf. offset and len are even. */
typedef void (*sprite_fill_fn)(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len);

/* frame: which frame of the file (0 for a still image). Reads the file's
 * header now (blocking), to learn the size. NULL if there's no card, no
 * file or it isn't a sprite file. File format: tools/png2spr.py. */
sprite_img_t *sprite_img_file(const char *path, uint16_t frame);
sprite_img_t *sprite_img_text(const font_t *f, const char *s, uint16_t colour, uint8_t scale);
sprite_img_t *sprite_img_solid(uint16_t w, uint16_t h, uint16_t colour);
sprite_img_t *sprite_img_ram(const uint8_t *px, uint16_t w, uint16_t h);
sprite_img_t *sprite_img_fill(uint16_t w, uint16_t h, sprite_fill_fn fn, void *ctx);

uint16_t sprite_img_w(const sprite_img_t *img);
uint16_t sprite_img_h(const sprite_img_t *img);
bool     sprite_img_ready(const sprite_img_t *img);    /* on the FPGA */

/* Frames in a sprite file (0: no card / file / not a sprite file). */
uint16_t sprite_file_frames(const char *path);

/* ---- sprites ------------------------------------------------------------------ */
typedef struct sprite sprite_t;

/* Shows up once its image is uploaded. */
sprite_t *sprite_new(sprite_img_t *img, int16_t x, int16_t y, bool visible);
void      sprite_move(sprite_t *s, int16_t x, int16_t y);
void      sprite_show(sprite_t *s, bool visible);
void      sprite_set(sprite_t *s, int16_t x, int16_t y, bool visible);
/* Deleted on the FPGA; the handle is invalid from now on. */
void      sprite_delete(sprite_t *s);

int16_t       sprite_x(const sprite_t *s);
int16_t       sprite_y(const sprite_t *s);
sprite_img_t *sprite_image(const sprite_t *s);

/* ---- animations ----------------------------------------------------------------- */
typedef struct sprite_ani sprite_ani_t;

/* n frames (at most SPRITE_ANI_FRAMES), each shown for `ticks` game frames
 * (60 Hz: ticks 6 = 10 frames a second). All frames must be the same size. */
sprite_ani_t *sprite_ani_new(sprite_img_t *const *frames, uint8_t n, uint8_t ticks,
                             int16_t x, int16_t y, bool visible);
/* Every frame of a sprite file. */
sprite_ani_t *sprite_ani_file(const char *path, uint8_t ticks, int16_t x, int16_t y,
                              bool visible);
void sprite_ani_move(sprite_ani_t *a, int16_t x, int16_t y);
void sprite_ani_show(sprite_ani_t *a, bool visible);
void sprite_ani_play(sprite_ani_t *a, bool playing);   /* paused: stays on its frame */
void sprite_ani_speed(sprite_ani_t *a, uint8_t ticks);
void sprite_ani_goto(sprite_ani_t *a, uint8_t frame);
void sprite_ani_delete(sprite_ani_t *a);

/* ---- driving it ------------------------------------------------------------------ */
void sprite_init(void);                 /* empty everything; once at start */
void sprite_frame(uint32_t n);          /* each game frame; n = SYNCs since last */
bool sprite_poll(void);                 /* each poll; false: a command failed -> resync */
void sprite_lost(void);                 /* after fpga_abort_all() */
void sprite_forget(void);               /* new FPGA session: send everything again */

unsigned sprite_pending(void);          /* images not on the FPGA yet */
unsigned sprite_memids_used(void);

#endif