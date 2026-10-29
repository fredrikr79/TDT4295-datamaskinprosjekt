/* sprite.c - images, sprites and animations on the FPGA (see sprite.h).
 *
 * Images:   NONE -> (WRITE_SPRITE x chunks) -> SENDING -> UPLOADED, memID known
 *           -> NO_ROOM if the FPGA's memIDs run out (not retried this session)
 * Sprites:  NONE -> (SPRITE_WRITE) -> CREATING -> LIVE, spriteID known
 *           LIVE -> (SPRITE_UPDATE when x/y/visible differ) -> UPDATING -> LIVE
 *           LIVE -> (SPRITE_DELETE after sprite_delete) -> DELETING -> free
 *
 * An image bigger than one command is uploaded as several WRITE_SPRITEs
 * in a row. Sprite memory is append-only, so they land back to back and
 * the first memID names the whole image. That only works if no other
 * upload gets in between, so one image's chunks are all submitted before
 * the next image starts -- and if one fails, the whole image goes again
 * (the memory it used is lost until the FPGA resets).
 *
 * Chunks are made into a staging buffer right before they go out (file
 * read, letters drawn, colour filled) and the buffer is free again in
 * the command's done callback. RAM images go straight from their buffer.
 *
 * At most one command per sprite is in flight; changes made meanwhile are
 * picked up by the next one, so a fast-moving sprite costs one update per
 * round trip, never a backlog.
 *
 * After a failed or aborted command it isn't known whether it happened.
 * An upload or a create is simply done again (if the lost one did happen,
 * its memory or its instance is left over on the FPGA); an update is
 * resent. */

#define LOG_TAG "sprite"

#include <stddef.h>
#include <string.h>

#include "sprite.h"
#include "fpga.h"
#include "storage.h"
#include "log.h"

/* Sprite file: "SP", W, H, frames (16 bits, MSB first), then the frames'
 * pixels one after the other, RGB565 MSB first (tools/png2spr.py). */
#define FILE_HDR 8u

enum { IMG_FREE, IMG_NONE, IMG_SENDING, IMG_UPLOADED, IMG_NO_ROOM };
enum { SRC_FILE, SRC_TEXT, SRC_SOLID, SRC_RAM, SRC_FILL };
enum { SPR_FREE, SPR_NONE, SPR_CREATING, SPR_LIVE, SPR_UPDATING, SPR_DELETING, SPR_NO_ROOM };

struct sprite_img {
    uint8_t  state;
    uint8_t  src;
    bool     make_err;              /* a chunk couldn't be made (logged once) */
    uint16_t w, h;
    uint16_t mem_id;                /* first memID, valid when uploaded       */
    uint16_t chunks, sent, acked;   /* WRITE_SPRITEs: needed / submitted / OK */
    uint16_t chunk_bytes;
    union {
        struct { char path[SPRITE_NAME_MAX]; uint32_t off; } file;
        struct { const font_t *font; uint16_t colour; uint8_t scale;
                 char s[SPRITE_NAME_MAX]; } text;
        uint16_t colour;
        const uint8_t *px;
        struct { sprite_fill_fn fn; void *ctx; } fill;
    } u;
};

struct sprite {
    uint8_t       state;
    bool          remove;           /* sprite_delete() was called              */
    /* what the game wants */
    sprite_img_t *img;
    int16_t       x, y;
    bool          visible;
    /* what the FPGA has, as far as we know */
    uint16_t      id;               /* spriteID, valid when live               */
    int16_t       s_x, s_y;
    bool          s_visible;
    bool          dirty;            /* an update failed: resend it             */
    int16_t       p_x, p_y;         /* the command in flight sends these       */
    bool          p_visible;
};

struct sprite_ani {
    bool      used, visible, playing;
    uint8_t   n, cur, ticks;
    uint32_t  count;                /* game frames into the current frame      */
    int16_t   x, y;
    sprite_t *frame[SPRITE_ANI_FRAMES];
};

static sprite_img_t  imgs[SPRITE_IMG_MAX];
static sprite_t      sprs[SPRITE_MAX];
static sprite_ani_t  anis[SPRITE_ANI_MAX];

static uint8_t       stage[SPRITE_STAGE_BUFS][SPRITE_STAGE_BYTES] __attribute__((aligned(4)));
static bool          stage_busy[SPRITE_STAGE_BUFS];
static uint8_t      *ready_buf;     /* made for uploading->sent, not submitted yet */

static sprite_img_t *uploading;     /* image whose chunks are being submitted  */
/* memID budget. memIDs are handed out in order (1, 2, 3...), so each OK
 * answer tells exactly how many are in use. In between, a chunk whose
 * fate is unknown (lost answer) counts as used. */
static unsigned      memids_known;  /* in use after the chunks resolved so far */
static unsigned      memids_pending;/* claimed by chunks not resolved yet      */
static bool          failed;

/* ---- pixels -------------------------------------------------------------- */
static void put_px(uint8_t *p, uint16_t colour)
{
    p[0] = (uint8_t)(colour >> 8);
    p[1] = (uint8_t)colour;
}

void sprite_px_set(uint8_t *px, uint16_t w, uint16_t x, uint16_t y, uint16_t colour)
{
    put_px(&px[((uint32_t)y * w + x) * PX_BYTES], colour);
}

void sprite_px_fill(uint8_t *px, uint16_t w, uint16_t h, uint16_t colour)
{
    for (uint32_t i = 0; i < (uint32_t)w * h; i++) put_px(&px[i * PX_BYTES], colour);
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

static uint32_t img_bytes(const sprite_img_t *img)
{
    return (uint32_t)img->w * img->h * PX_BYTES;
}

/* for log lines */
static const char *img_name(const sprite_img_t *img)
{
    switch (img->src) {
    case SRC_FILE:  return img->u.file.path;
    case SRC_TEXT:  return img->u.text.s;
    case SRC_SOLID: return "(solid)";
    case SRC_RAM:   return "(ram)";
    default:        return "(fill)";
    }
}

/* ---- making images ------------------------------------------------------- */
static sprite_img_t *img_new(uint8_t src, uint16_t w, uint16_t h)
{
    if (w == 0 || h == 0) {
        LOG_ERR("image of %ux%u px", w, h);
        return NULL;
    }
    for (unsigned i = 0; i < SPRITE_IMG_MAX; i++) {
        if (imgs[i].state != IMG_FREE) continue;
        memset(&imgs[i], 0, sizeof imgs[i]);
        imgs[i].state = IMG_NONE;
        imgs[i].src   = src;
        imgs[i].w     = w;
        imgs[i].h     = h;
        return &imgs[i];
    }
    LOG_ERR("no room for more images (SPRITE_IMG_MAX %u)", SPRITE_IMG_MAX);
    return NULL;
}

static bool file_header(const char *path, uint16_t *w, uint16_t *h, uint16_t *frames)
{
    uint8_t  hd[FILE_HDR];
    uint32_t got = 0, size = 0;
    storage_status_t st = storage_read(path, 0, hd, FILE_HDR, &got);
    if (st != STORAGE_OK) {
        LOG_ERR("%s: %s", path, storage_status_str(st));
        return false;
    }
    if (got != FILE_HDR || hd[0] != 'S' || hd[1] != 'P') {
        LOG_ERR("%s: not a sprite file (tools/png2spr.py makes them)", path);
        return false;
    }
    *w = be16(&hd[2]);
    *h = be16(&hd[4]);
    *frames = be16(&hd[6]);
    uint64_t need = FILE_HDR + (uint64_t)*frames * *w * *h * PX_BYTES;
    if (storage_size(path, &size) != STORAGE_OK || *w == 0 || *h == 0 || *frames == 0 ||
        size < need) {
        LOG_ERR("%s: broken (%ux%u x %u frames, %lu bytes)", path, *w, *h, *frames,
                (unsigned long)size);
        return false;
    }
    return true;
}

uint16_t sprite_file_frames(const char *path)
{
    uint16_t w, h, frames;
    return path && file_header(path, &w, &h, &frames) ? frames : 0;
}

sprite_img_t *sprite_img_file(const char *path, uint16_t frame)
{
    uint16_t w, h, frames;
    if (!path) return NULL;
    if (strlen(path) >= SPRITE_NAME_MAX) {
        LOG_ERR("%s: path longer than SPRITE_NAME_MAX", path);
        return NULL;
    }
    if (!file_header(path, &w, &h, &frames)) return NULL;
    if (frame >= frames) {
        LOG_ERR("%s: no frame %u (has %u)", path, frame, frames);
        return NULL;
    }
    sprite_img_t *img = img_new(SRC_FILE, w, h);
    if (!img) return NULL;
    strcpy(img->u.file.path, path);
    img->u.file.off = FILE_HDR + (uint32_t)frame * img_bytes(img);
    return img;
}

sprite_img_t *sprite_img_text(const font_t *f, const char *s, uint16_t colour, uint8_t scale)
{
    if (!f || !s) return NULL;
    if (strlen(s) >= SPRITE_NAME_MAX) {
        LOG_ERR("text \"%s\" longer than SPRITE_NAME_MAX", s);
        return NULL;
    }
    if (scale == 0) scale = 1;
    int w = font_text_width(f, s) * scale;
    if (w > 0xFFFF) {
        LOG_ERR("text \"%s\" too wide", s);
        return NULL;
    }
    sprite_img_t *img = img_new(SRC_TEXT, (uint16_t)w, (uint16_t)(f->height * scale));
    if (!img) return NULL;
    img->u.text.font   = f;
    img->u.text.colour = colour;
    img->u.text.scale  = scale;
    strcpy(img->u.text.s, s);
    return img;
}

sprite_img_t *sprite_img_solid(uint16_t w, uint16_t h, uint16_t colour)
{
    sprite_img_t *img = img_new(SRC_SOLID, w, h);
    if (img) img->u.colour = colour;
    return img;
}

sprite_img_t *sprite_img_ram(const uint8_t *px, uint16_t w, uint16_t h)
{
    if (!px) return NULL;
    sprite_img_t *img = img_new(SRC_RAM, w, h);
    if (img) img->u.px = px;
    return img;
}

sprite_img_t *sprite_img_fill(uint16_t w, uint16_t h, sprite_fill_fn fn, void *ctx)
{
    if (!fn) return NULL;
    sprite_img_t *img = img_new(SRC_FILL, w, h);
    if (img) { img->u.fill.fn = fn; img->u.fill.ctx = ctx; }
    return img;
}

uint16_t sprite_img_w(const sprite_img_t *img)     { return img ? img->w : 0; }
uint16_t sprite_img_h(const sprite_img_t *img)     { return img ? img->h : 0; }
bool     sprite_img_ready(const sprite_img_t *img) { return img && img->state == IMG_UPLOADED; }

/* ---- making chunks --------------------------------------------------------- */
static const font_glyph_t *next_glyph(const font_t *f, const char **s, uint32_t *gw, uint8_t sc)
{
    uint32_t cp = font_utf8_next(s);
    if (!cp) { *gw = 0; return NULL; }
    const font_glyph_t *g = font_glyph(f, cp);
    *gw = (uint32_t)g->width * sc;
    return g;
}

/* Pixels [off/2, (off+len)/2) of a text: walk along the row's letters. */
static void make_text(const sprite_img_t *img, uint32_t off, uint8_t *buf, uint32_t len)
{
    const font_t *f  = img->u.text.font;
    uint8_t       sc = img->u.text.scale;
    uint32_t      p  = off / PX_BYTES, n = len / PX_BYTES;
    const char   *s  = img->u.text.s;
    const font_glyph_t *g = NULL;
    uint32_t pen = 0, gw = 0;

    for (uint32_t i = 0; i < n; i++, p++) {
        uint32_t x = p % img->w, y = p / img->w;
        if (i == 0 || x == 0) {                     /* start of a row */
            s   = img->u.text.s;
            pen = 0;
            g   = next_glyph(f, &s, &gw, sc);
        }
        while (g && x >= pen + gw) {                /* next letter */
            pen += gw;
            g = next_glyph(f, &s, &gw, sc);
        }
        bool on = g && font_pixel(f, g, (int)((x - pen) / sc), (int)(y / sc));
        put_px(&buf[i * PX_BYTES], on ? img->u.text.colour : SPRITE_CLEAR);
    }
}

static void make_chunk(sprite_img_t *img, uint32_t off, uint8_t *buf, uint32_t len)
{
    switch (img->src) {
    case SRC_FILE: {
        uint32_t got = 0;
        storage_status_t st = storage_read(img->u.file.path, img->u.file.off + off, buf, len, &got);
        if (st != STORAGE_OK || got < len) {
            /* Too late to stop: earlier chunks are on the FPGA. Finish the
             * image see-through, so it's at least the right size. */
            if (!img->make_err)
                LOG_ERR("%s: read failed (%s) -- part of it left see-through",
                        img->u.file.path, storage_status_str(st));
            img->make_err = true;
            if (st != STORAGE_OK) got = 0;
            for (uint32_t i = got & ~1u; i < len; i += PX_BYTES) put_px(&buf[i], SPRITE_CLEAR);
        }
        break;
    }
    case SRC_TEXT:
        make_text(img, off, buf, len);
        break;
    case SRC_SOLID:
        for (uint32_t i = 0; i < len; i += PX_BYTES) put_px(&buf[i], img->u.colour);
        break;
    case SRC_FILL:
        img->u.fill.fn(img->u.fill.ctx, off, buf, len);
        break;
    default:
        break;
    }
}

static uint8_t *stage_get(void)
{
    for (unsigned i = 0; i < SPRITE_STAGE_BUFS; i++)
        if (!stage_busy[i]) { stage_busy[i] = true; return stage[i]; }
    return NULL;
}

static void stage_put(const uint8_t *p)
{
    for (unsigned i = 0; i < SPRITE_STAGE_BUFS; i++)
        if (p == stage[i]) stage_busy[i] = false;
}

static void stage_reset(void)
{
    memset(stage_busy, 0, sizeof stage_busy);
    ready_buf = NULL;
}

/* ---- sprites ----------------------------------------------------------------- */
sprite_t *sprite_new(sprite_img_t *img, int16_t x, int16_t y, bool visible)
{
    if (!img) return NULL;
    for (unsigned i = 0; i < SPRITE_MAX; i++) {
        if (sprs[i].state != SPR_FREE) continue;
        sprs[i] = (sprite_t){ .state = SPR_NONE, .img = img, .x = x, .y = y,
                              .visible = visible };
        return &sprs[i];
    }
    LOG_ERR("no room for more sprites (SPRITE_MAX %u)", SPRITE_MAX);
    return NULL;
}

void sprite_move(sprite_t *s, int16_t x, int16_t y) { if (s) { s->x = x; s->y = y; } }
void sprite_show(sprite_t *s, bool visible)         { if (s) s->visible = visible; }

void sprite_set(sprite_t *s, int16_t x, int16_t y, bool visible)
{
    if (s) { s->x = x; s->y = y; s->visible = visible; }
}

void sprite_delete(sprite_t *s)
{
    if (!s || s->state == SPR_FREE) return;
    s->remove = true;
    if (s->state == SPR_NONE || s->state == SPR_NO_ROOM)
        s->state = SPR_FREE;                    /* never made it to the FPGA */
}

int16_t       sprite_x(const sprite_t *s)     { return s ? s->x : 0; }
int16_t       sprite_y(const sprite_t *s)     { return s ? s->y : 0; }
sprite_img_t *sprite_image(const sprite_t *s) { return s ? s->img : NULL; }

/* ---- animations ---------------------------------------------------------------- */
sprite_ani_t *sprite_ani_new(sprite_img_t *const *frames, uint8_t n, uint8_t ticks,
                             int16_t x, int16_t y, bool visible)
{
    if (!frames || n == 0) return NULL;
    if (n > SPRITE_ANI_FRAMES) {
        LOG_WARN("animation of %u frames: keeping %u (SPRITE_ANI_FRAMES)", n, SPRITE_ANI_FRAMES);
        n = SPRITE_ANI_FRAMES;
    }
    for (uint8_t i = 0; i < n; i++) {
        if (!frames[i]) { LOG_ERR("animation: frame %u missing", i); return NULL; }
        if (frames[i]->w != frames[0]->w || frames[i]->h != frames[0]->h)
            LOG_WARN("animation: frame %u is %ux%u, frame 0 %ux%u", i,
                     frames[i]->w, frames[i]->h, frames[0]->w, frames[0]->h);
    }

    sprite_ani_t *a = NULL;
    for (unsigned i = 0; i < SPRITE_ANI_MAX && !a; i++)
        if (!anis[i].used) a = &anis[i];
    if (!a) {
        LOG_ERR("no room for more animations (SPRITE_ANI_MAX %u)", SPRITE_ANI_MAX);
        return NULL;
    }
    *a = (sprite_ani_t){ .visible = visible, .playing = true, .n = n,
                         .ticks = ticks ? ticks : 1, .x = x, .y = y };
    for (uint8_t i = 0; i < n; i++) {
        a->frame[i] = sprite_new(frames[i], x, y, visible && i == 0);
        if (!a->frame[i]) {
            while (i--) sprite_delete(a->frame[i]);
            return NULL;
        }
    }
    a->used = true;
    return a;
}

sprite_ani_t *sprite_ani_file(const char *path, uint8_t ticks, int16_t x, int16_t y,
                              bool visible)
{
    sprite_img_t *frames[SPRITE_ANI_FRAMES];
    uint16_t n = sprite_file_frames(path);
    if (n == 0) return NULL;
    if (n > SPRITE_ANI_FRAMES) {
        LOG_WARN("%s: %u frames, keeping %u (SPRITE_ANI_FRAMES)", path, n, SPRITE_ANI_FRAMES);
        n = SPRITE_ANI_FRAMES;
    }
    for (uint16_t i = 0; i < n; i++)
        if (!(frames[i] = sprite_img_file(path, i))) return NULL;
    return sprite_ani_new(frames, (uint8_t)n, ticks, x, y, visible);
}

/* Hide the frame shown, show frame f where the animation is. */
static void ani_to(sprite_ani_t *a, uint8_t f)
{
    if (f == a->cur) return;
    sprite_show(a->frame[a->cur], false);
    a->cur = f;
    sprite_set(a->frame[a->cur], a->x, a->y, a->visible);
}

void sprite_ani_move(sprite_ani_t *a, int16_t x, int16_t y)
{
    if (!a) return;
    a->x = x;
    a->y = y;
    sprite_move(a->frame[a->cur], x, y);        /* the hidden ones move when shown */
}

void sprite_ani_show(sprite_ani_t *a, bool visible)
{
    if (!a) return;
    a->visible = visible;
    sprite_show(a->frame[a->cur], visible);
}

void sprite_ani_play(sprite_ani_t *a, bool playing) { if (a) a->playing = playing; }
void sprite_ani_speed(sprite_ani_t *a, uint8_t ticks) { if (a) a->ticks = ticks ? ticks : 1; }

void sprite_ani_goto(sprite_ani_t *a, uint8_t frame)
{
    if (!a) return;
    ani_to(a, (uint8_t)(frame % a->n));
    a->count = 0;
}

void sprite_ani_delete(sprite_ani_t *a)
{
    if (!a || !a->used) return;
    for (uint8_t i = 0; i < a->n; i++) sprite_delete(a->frame[i]);
    a->used = false;
}

void sprite_frame(uint32_t n)
{
    for (unsigned i = 0; i < SPRITE_ANI_MAX; i++) {
        sprite_ani_t *a = &anis[i];
        if (!a->used || !a->playing || a->n < 2 || n == 0) continue;
        a->count += n;
        uint32_t steps = a->count / a->ticks;
        a->count %= a->ticks;
        if (steps) ani_to(a, (uint8_t)((a->cur + steps) % a->n));
    }
}

/* ---- callbacks ------------------------------------------------------------------ */
static bool out_of_room(fpga_err_t err)
{
    return err == FPGA_ERR_STATUS && fpga_fail_code() == FPGA_FAIL_RESOURCE_EXHAUSTED;
}

/* An image gives up the chunks it claimed but never submitted. */
static void release_unsent(sprite_img_t *img)
{
    unsigned n = (unsigned)(img->chunks - img->sent);
    memids_pending = memids_pending > n ? memids_pending - n : 0u;
    img->sent = img->chunks;
}

static void image_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    sprite_img_t *img = cmd->ctx;
    stage_put(cmd->tx);                         /* (no-op for RAM images) */

    /* the budget, for every chunk -- also those of an attempt already failed */
    if (memids_pending) memids_pending--;
    if (err == FPGA_ERR_NONE)
        memids_known = fpga_answer_id(cmd);     /* exact */
    else if (err != FPGA_ERR_CANCELLED && err != FPGA_ERR_STATUS)
        memids_known++;                         /* may have happened */
    /* (CANCELLED never ran; a NACK means the FPGA refused it) */

    if (img->state != IMG_SENDING) return;      /* an attempt that already failed */

    if (err == FPGA_ERR_NONE) {
        uint16_t id = fpga_answer_id(cmd);
        if (img->acked++ == 0) img->mem_id = id;            /* first chunk names it */
        if (img->acked == img->chunks) img->state = IMG_UPLOADED;
        return;
    }

    /* Its memory isn't one block any more: upload the whole image again --
     * unless the FPGA is out of room, then that would only fail again. */
    if (out_of_room(err)) {
        LOG_ERR("%s: FPGA out of sprite memory / memIDs, left out", img_name(img));
        img->state = IMG_NO_ROOM;
    } else {
        img->state = IMG_NONE;
    }
    if (uploading == img) {
        uploading = NULL;
        if (ready_buf) { stage_put(ready_buf); ready_buf = NULL; }
    }
    release_unsent(img);
    failed = true;
}

static void sprite_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    sprite_t *s = cmd->ctx;
    if (err != FPGA_ERR_NONE) {
        failed = true;
        switch (s->state) {
        case SPR_CREATING:
            if (out_of_room(err)) {
                LOG_ERR("FPGA out of spriteIDs, sprite left out");
                s->state = SPR_NO_ROOM;
            } else {
                s->state = SPR_NONE;                        /* create again */
            }
            break;
        case SPR_UPDATING:
        case SPR_DELETING: s->state = SPR_LIVE; s->dirty = true; break;  /* resend */
        default: break;
        }
        return;
    }
    switch (s->state) {
    case SPR_CREATING:
        s->id = fpga_answer_id(cmd);
        /* fall through */
    case SPR_UPDATING:
        s->state = SPR_LIVE;
        s->s_x = s->p_x; s->s_y = s->p_y; s->s_visible = s->p_visible;
        s->dirty = false;
        break;
    case SPR_DELETING:
        s->state = SPR_FREE;
        break;
    default:
        break;
    }
}

/* ---- sending ----------------------------------------------------------------------- */
/* false: the queue is full, stop for this poll */
static bool submit(fpga_cmd_t *c, fpga_done_fn done, void *ctx)
{
    c->done = done;
    c->ctx  = ctx;
    fpga_status_t st = fpga_submit(c);
    if (st == FPGA_OK) return true;
    if (st != FPGA_EFULL) {
        LOG_WARN("submit: %s", fpga_status_str(st));
        failed = true;
    }
    return false;
}

static bool image_needed(const sprite_img_t *img)
{
    for (unsigned i = 0; i < SPRITE_MAX; i++) {
        const sprite_t *s = &sprs[i];
        if (s->img == img && s->state != SPR_FREE && s->state != SPR_NO_ROOM && !s->remove)
            return true;
    }
    return false;
}

/* The next image to upload -- one a sprite waits for first -- with its
 * chunks worked out. */
static sprite_img_t *next_upload(void)
{
    uint16_t max = fpga_max_payload(WRITE_SPRITE_OPCODE);
    if (max == UINT16_MAX) return NULL;         /* N not known yet */
    if (max > SPRITE_STAGE_BYTES) max = SPRITE_STAGE_BYTES;
    max = (uint16_t)(max & ~1u);                /* whole pixels per chunk */
    if (max == 0) return NULL;

    for (int pass = 0; pass < 2; pass++) {
        for (unsigned i = 0; i < SPRITE_IMG_MAX; i++) {
            sprite_img_t *img = &imgs[i];
            if (img->state != IMG_NONE || (pass == 0 && !image_needed(img))) continue;
            uint32_t chunks = (img_bytes(img) + max - 1u) / max;
            unsigned used   = sprite_memids_used();     /* in flight counts too */
            if (used + chunks > SPRITE_MEMIDS) {
                LOG_ERR("%s (%ux%u) needs %lu memID(s), %u of %u left -- left out",
                        img_name(img), img->w, img->h, (unsigned long)chunks,
                        SPRITE_MEMIDS - used, SPRITE_MEMIDS);
                img->state = IMG_NO_ROOM;
                continue;
            }
            img->chunks      = (uint16_t)chunks;
            img->chunk_bytes = max;
            img->sent = img->acked = 0;
            img->make_err    = false;
            img->state       = IMG_SENDING;
            memids_pending  += chunks;
            return img;
        }
    }
    return NULL;
}

static uint16_t clamp_u16(int16_t v) { return v < 0 ? 0u : (uint16_t)v; }

bool sprite_poll(void)
{
    if (failed) return false;

    /* Images, one at a time: an image's chunks must follow each other
     * with no other upload in between. */
    while(1) {
        if (!uploading && !(uploading = next_upload())) break;
        sprite_img_t *img = uploading;
        uint32_t total = img_bytes(img);
        while (img->sent < img->chunks) {
            uint32_t off = (uint32_t)img->sent * img->chunk_bytes;
            uint32_t len = total - off < img->chunk_bytes ? total - off : img->chunk_bytes;
            const uint8_t *tx;
            if (img->src == SRC_RAM) {
                tx = img->u.px + off;
            } else {
                if (!ready_buf) {                   /* make it once, even if the queue is full */
                    if (!(ready_buf = stage_get())) return !failed;   /* all in flight */
                    make_chunk(img, off, ready_buf, len);
                }
                tx = ready_buf;
            }
            fpga_cmd_t c = fpga_cmd_write_sprite(tx, (uint16_t)len);
            if (!submit(&c, image_done, img)) return !failed;
            if (img->src != SRC_RAM) ready_buf = NULL;  /* image_done frees it */
            img->sent++;
        }
        uploading = NULL;                       /* all submitted: the next may start */
    }

    /* sprites */
    for (unsigned i = 0; i < SPRITE_MAX; i++) {
        sprite_t *s = &sprs[i];
        fpga_cmd_t c;
        if (s->state == SPR_NONE && !s->remove) {
            if (s->img->state != IMG_UPLOADED) continue;
            s->p_x = s->x; s->p_y = s->y; s->p_visible = s->visible;
            c = fpga_cmd_sprite_write(s->img->mem_id, s->visible, clamp_u16(s->x),
                                      clamp_u16(s->y), s->img->w, s->img->h);
            if (!submit(&c, sprite_done, s)) return !failed;
            s->state = SPR_CREATING;
        } else if (s->state == SPR_LIVE && s->remove) {
            c = fpga_cmd_sprite_delete(s->id);
            if (!submit(&c, sprite_done, s)) return !failed;
            s->state = SPR_DELETING;
        } else if (s->state == SPR_LIVE &&
                   (s->dirty || s->x != s->s_x || s->y != s->s_y ||
                    s->visible != s->s_visible)) {
            s->p_x = s->x; s->p_y = s->y; s->p_visible = s->visible;
            c = fpga_cmd_sprite_update(s->id, s->visible, clamp_u16(s->x), clamp_u16(s->y));
            if (!submit(&c, sprite_done, s)) return !failed;
            s->state = SPR_UPDATING;
        }
    }
    return !failed;
}

/* ---- errors, sessions ------------------------------------------------------------------- */
void sprite_init(void)
{
    memset(imgs, 0, sizeof imgs);
    memset(sprs, 0, sizeof sprs);
    memset(anis, 0, sizeof anis);
    stage_reset();
    uploading      = NULL;
    memids_known   = 0;
    memids_pending = 0;
    failed         = false;
}

void sprite_lost(void)
{
    /* The aborted commands' callbacks already put most things back. An
     * image whose first chunk never got into the queue had no callback:
     * start it over too. Nothing is in flight now. */
    for (unsigned i = 0; i < SPRITE_IMG_MAX; i++)
        if (imgs[i].state == IMG_SENDING) { release_unsent(&imgs[i]); imgs[i].state = IMG_NONE; }
    stage_reset();
    uploading = NULL;
    failed    = false;
}

void sprite_forget(void)
{
    /* New session: empty sprite memory, every memID free again. */
    for (unsigned i = 0; i < SPRITE_IMG_MAX; i++)
        if (imgs[i].state != IMG_FREE) imgs[i].state = IMG_NONE;
    for (unsigned i = 0; i < SPRITE_MAX; i++) {
        sprite_t *s = &sprs[i];
        if (s->state == SPR_FREE) continue;
        if (s->remove) s->state = SPR_FREE;     /* gone with the old session */
        else           { s->state = SPR_NONE; s->dirty = false; }
    }
    stage_reset();
    uploading      = NULL;
    memids_known   = 0;
    memids_pending = 0;
    failed         = false;
}

unsigned sprite_pending(void)
{
    unsigned n = 0;
    for (unsigned i = 0; i < SPRITE_IMG_MAX; i++)
        n += imgs[i].state == IMG_NONE || imgs[i].state == IMG_SENDING;
    return n;
}

unsigned sprite_memids_used(void) { return memids_known + memids_pending; }