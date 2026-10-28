/* hud.c - the HUD layer as FPGA sprites (see hud.h).
 *
 * Images:   NONE -> (WRITE_SPRITE x chunks) -> SENDING -> UPLOADED, memID known
 *           -> NO_ROOM if the FPGA's memIDs run out (not retried)
 * Sprites:  NONE -> (SPRITE_WRITE) -> CREATING -> LIVE, spriteID known
 *           LIVE -> (SPRITE_UPDATE when x/y/visible differ) -> LIVE
 *           LIVE -> (SPRITE_DELETE after hud_sprite_remove) -> GONE
 *
 * An image bigger than one command is uploaded as several WRITE_SPRITEs
 * in a row. Sprite memory is append-only, so they land back to back and
 * the first memID names the whole image. That only works if no other
 * upload gets in between, so one image's chunks are all submitted before
 * the next image starts -- and if one fails, the whole image goes again
 * (the memory it used is lost until the FPGA resets).
 *
 * At most one command per sprite is in flight; changes made meanwhile are
 * picked up by the next one, so a fast-moving sprite costs one update per
 * round trip, never a backlog.
 *
 * After a failed or aborted command it isn't known whether it happened.
 * An upload or a create is simply done again (if the lost one did happen,
 * its memory or its instance is left over on the FPGA); an update is
 * resent. */

#define LOG_TAG "hud"

#include <stddef.h>

#include "hud.h"
#include "log.h"

enum { IMG_NONE, IMG_SENDING, IMG_UPLOADED, IMG_NO_ROOM };
enum { SPR_NONE, SPR_CREATING, SPR_LIVE, SPR_UPDATING, SPR_DELETING, SPR_GONE,
       SPR_NO_ROOM };

static hud_image_t  *images;
static hud_sprite_t *sprites;
static hud_image_t  *uploading;     /* image whose chunks are being submitted */
/* memID budget. memIDs are handed out in order (1, 2, 3...), so each OK
 * answer tells exactly how many are in use. In between, a chunk whose
 * fate is unknown (lost answer) counts as used. */
static unsigned      memids_known;  /* in use after the chunks resolved so far */
static unsigned      memids_pending;/* claimed by chunks not resolved yet      */
static bool          failed;

/* ---- pixels ------------------------------------------------------------ */
void hud_set_px(uint8_t *px, uint16_t w, uint16_t x, uint16_t y, uint16_t colour)
{
    uint8_t *p = &px[((uint32_t)y * w + x) * PX_BYTES];
    p[0] = (uint8_t)(colour >> 8);
    p[1] = (uint8_t)colour;
}

void hud_fill(uint8_t *px, uint16_t w, uint16_t h, uint16_t colour)
{
    for (uint16_t y = 0; y < h; y++)
        for (uint16_t x = 0; x < w; x++) hud_set_px(px, w, x, y, colour);
}

unsigned hud_memids_used(void) { return memids_known + memids_pending; }

/* ---- registering ------------------------------------------------------- */
void hud_image_init(hud_image_t *img, const uint8_t *px, uint16_t w, uint16_t h)
{
    for (hud_image_t *i = images; i; i = i->next)
        if (i == img) return;                   /* already registered */
    *img = (hud_image_t){ .px = px, .w = w, .h = h, .state = IMG_NONE, .next = images };
    images = img;
}

void hud_sprite_init(hud_sprite_t *s, hud_image_t *img, int16_t x, int16_t y, bool visible)
{
    for (hud_sprite_t *i = sprites; i; i = i->next)
        if (i == s) return;
    *s = (hud_sprite_t){ .img = img, .x = x, .y = y, .visible = visible,
                         .state = SPR_NONE, .next = sprites };
    sprites = s;
}

static void unlink_sprite(hud_sprite_t *s)
{
    for (hud_sprite_t **pp = &sprites; *pp; pp = &(*pp)->next)
        if (*pp == s) { *pp = s->next; break; }
    s->state = SPR_GONE;
}

void hud_sprite_move(hud_sprite_t *s, int16_t x, int16_t y) { s->x = x; s->y = y; }
void hud_sprite_show(hud_sprite_t *s, bool visible)         { s->visible = visible; }

void hud_sprite_remove(hud_sprite_t *s)
{
    s->remove = true;
    if (s->state == SPR_NONE || s->state == SPR_NO_ROOM)
        unlink_sprite(s);                       /* never made it to the FPGA */
}

bool hud_sprite_gone(const hud_sprite_t *s) { return s->state == SPR_GONE; }

/* ---- callbacks ---------------------------------------------------------- */
static bool out_of_room(fpga_err_t err)
{
    return err == FPGA_ERR_STATUS && fpga_fail_code() == FPGA_FAIL_RESOURCE_EXHAUSTED;
}

/* An image gives up the chunks it claimed but never submitted. */
static void release_unsent(hud_image_t *img)
{
    unsigned n = (unsigned)(img->chunks - img->sent);
    memids_pending = memids_pending > n ? memids_pending - n : 0u;
    img->sent = img->chunks;
}

static void image_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    hud_image_t *img = cmd->ctx;

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
        LOG_ERR("image %ux%u: FPGA out of sprite memory / memIDs, left out", img->w, img->h);
        img->state = IMG_NO_ROOM;
    } else {
        img->state = IMG_NONE;
    }
    if (uploading == img) uploading = NULL;
    release_unsent(img);
    failed = true;
}

static void sprite_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    hud_sprite_t *s = cmd->ctx;
    if (err != FPGA_ERR_NONE) {
        failed = true;
        switch (s->state) {
        case SPR_CREATING:
            if (out_of_room(err)) {
                LOG_ERR("sprite: FPGA out of spriteIDs, left out");
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
        unlink_sprite(s);
        break;
    default:
        break;
    }
}

/* ---- sending ------------------------------------------------------------ */
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

static bool image_needed(const hud_image_t *img)
{
    for (const hud_sprite_t *s = sprites; s; s = s->next)
        if (s->img == img && !s->remove && s->state != SPR_NO_ROOM) return true;
    return false;
}

/* Pick the next image to upload and work out its chunks. */
static hud_image_t *next_upload(void)
{
    uint16_t max = fpga_max_payload(WRITE_SPRITE_OPCODE);
    if (max == UINT16_MAX) return NULL;         /* N not known yet */
    max = (uint16_t)(max & ~1u);                /* whole pixels per chunk */

    for (hud_image_t *img = images; img; img = img->next) {
        if (img->state != IMG_NONE || !image_needed(img)) continue;
        uint32_t bytes  = (uint32_t)img->w * img->h * PX_BYTES;
        uint32_t chunks = bytes ? (bytes + max - 1u) / max : 0u;
        unsigned used = hud_memids_used();     /* in flight counts too */
        if (chunks == 0 || used + chunks > HUD_MEMIDS) {
            LOG_ERR("image %ux%u needs %lu memID(s), %u of %u left -- left out",
                    img->w, img->h, (unsigned long)chunks, HUD_MEMIDS - used, HUD_MEMIDS);
            img->state = IMG_NO_ROOM;
            continue;
        }
        img->chunks      = (uint16_t)chunks;
        img->chunk_bytes = max;
        img->sent = img->acked = 0;
        img->state = IMG_SENDING;
        memids_pending += chunks;
        return img;
    }
    return NULL;
}

static uint16_t clamp_u16(int16_t v) { return v < 0 ? 0u : (uint16_t)v; }

bool hud_poll(void)
{
    if (failed) return false;

    /* Images a sprite waits for, one at a time: an image's chunks must
     * follow each other with no other upload in between. */
    for (;;) {
        if (!uploading && !(uploading = next_upload())) break;
        hud_image_t *img = uploading;
        uint32_t total = (uint32_t)img->w * img->h * PX_BYTES;
        while (img->sent < img->chunks) {
            uint32_t off = (uint32_t)img->sent * img->chunk_bytes;
            uint32_t len = total - off < img->chunk_bytes ? total - off : img->chunk_bytes;
            fpga_cmd_t c = fpga_cmd_write_sprite(img->px + off, (uint16_t)len);
            if (!submit(&c, image_done, img)) return !failed;
            img->sent++;
        }
        uploading = NULL;                       /* all submitted: the next may start */
    }

    /* sprites */
    for (hud_sprite_t *s = sprites; s; s = s->next) {
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

/* ---- errors -------------------------------------------------------------- */
void hud_lost(void)
{
    /* The aborted commands' callbacks already put most objects back. An
     * image whose first chunk never got into the queue had no callback:
     * start it over too. */
    for (hud_image_t *img = images; img; img = img->next)
        if (img->state == IMG_SENDING) { release_unsent(img); img->state = IMG_NONE; }
    uploading = NULL;
    failed    = false;
}

void hud_forget(void)
{
    /* New session: empty sprite memory, every memID free again. */
    for (hud_image_t *img = images; img; img = img->next) img->state = IMG_NONE;
    for (hud_sprite_t *s = sprites, *next; s; s = next) {
        next = s->next;
        if (s->remove) unlink_sprite(s);        /* gone with the old session */
        else           s->state = SPR_NONE;
    }
    uploading      = NULL;
    memids_known   = 0;
    memids_pending = 0;
    failed      = false;
}