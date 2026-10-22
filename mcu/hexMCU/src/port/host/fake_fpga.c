/* ======================================================================
 * fake_fpga.c - what the FPGA does with the commands, for the host build.
 * Follows docs/mcu_fpga_spi_api.md; see fpga.h for the command table.
 *
 * Model:
 *   - The grid is the SIM layer, the HUD is sprites. Both layers live in
 *     display_host.c (display_layer()), which shows the HUD over the SIM
 *     layer with DISPLAY_HUD_CLEAR pixels see-through. Pixels travel MSB
 *     first, 2 bytes each.
 *   - WRITE/READ_CONS and _BOX go to the grid. CONS is contiguous memory:
 *     N bytes from (X, Y) on, row after row.
 *   - Sprites: WRITE_SPRITE appends its payload to sprite memory and
 *     answers a memID (1, 2, ...; 0 means none). Memory is append-only and
 *     contiguous, so an image uploaded as several WRITE_SPRITEs in a row is
 *     one block from its first memID on. SPRITE_WRITE places an instance
 *     (first memID, W, H) and answers the lowest free spriteID. Instances
 *     are drawn into the HUD layer once per frame, in spriteID order
 *     (higher on top), with DISPLAY_HUD_CLEAR pixels left see-through.
 *   - Commands arrive through an input FIFO of MAX_CMD bytes (N in the
 *     INFO answer), header included. Bytes beyond that are dropped with a
 *     warning -- fpga.c should never let that happen once it knows N.
 *   - Commands with an answer fill the output FIFO when they are
 *     processed. READ transactions (READ_OPCODE is a dummy the FPGA
 *     ignores) drain it; reading past the end gives 0x00.
 *   - Every command is answered with READY after READY_US. A command that
 *     can't be done (unknown ID, wrong parameter count, out of memIDs...)
 *     answers NACK on the ACK line, flushes the FIFOs and puts the one-byte
 *     error code in the answer FIFO, like the real FPGA.
 *   - SYNC is pulsed FRAME_HZ times a second, every frame, whatever the
 *     MCU does -- like the real FPGA finishing a frame for VGA.
 *   - Screen size, N and the session id live only here; the MCU side
 *     learns them with INFO, like it will from the real FPGA.
 *   - Out-of-bounds pixels are skipped with a warning -- keeping writes
 *     in bounds is the MCU's job, so this is where you find out.
 *
 * Choices marked PROTOCOL are guesses where the spec isn't settled yet.
 * ====================================================================== */
#define LOG_TAG "ffpga"

#include "fake_fpga.h"
#include "fpga.h"
#include "display_host.h"
#include "frame_sync.h"
#include "log.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- configuration ---------------------------------------------------- */
#define FAKE_W        DISPLAY_W     /* FPGA screen = the window, via INFO  */
#define FAKE_H        DISPLAY_H
#define MAX_CMD       1024u         /* input FIFO = N, reported by INFO    */
#define OUT_FIFO      1024u         /* answer FIFO                         */
#define PIXEL_BYTES   2u            /* RGB565, MSB first                   */
#define READY_US      200           /* command -> READY pulse              */
#define FRAME_HZ      30u           /* SYNC pulses: one per finished frame */
#define FRAME_NS      (1000000000ull / FRAME_HZ)

#define SPRITE_MEM    (256u * 1024u)/* PROTOCOL: bytes of sprite memory    */
#define MAX_IMAGES    64u           /* memID LUT: memIDs 1 .. MAX_IMAGES-1 */
#define MAX_SPRITES   256u          /* PROTOCOL: spriteIDs 0 .. 255        */

/* ---- state ------------------------------------------------------------ */
typedef struct {
    bool     used;
    uint8_t  mem_id;
    bool     active;
    uint16_t x, y, w, h;
} instance_t;

static struct {
    uint8_t  out[OUT_FIFO];     /* answer FIFO                              */
    uint16_t out_len, out_pos;
    bool     sim_running;
    uint32_t session;           /* new on every reset, reported by INFO     */
    uint64_t next_sync_ns;      /* 0 = not started                          */

    uint8_t    mem[SPRITE_MEM]; /* sprite images, one after the other       */
    uint32_t   mem_used;
    struct { uint32_t off, len; } image[MAX_IMAGES];   /* [0] = none       */
    uint8_t    n_images;        /* memIDs handed out: 1 .. n_images         */
    instance_t sprite[MAX_SPRITES];
} ff;

/* ---- failing a command -------------------------------------------------- */
static const char *fail_name(uint8_t code)
{
    switch (code) {
    case FPGA_FAIL_UNKNOWN_COMMAND:     return "unknown command";
    case FPGA_FAIL_MISSING_PARAMETER:   return "missing parameter";
    case FPGA_FAIL_TOO_MANY_PARAMETERS: return "too many parameters";
    case FPGA_FAIL_ILLEGAL_VALUE:       return "illegal value";
    case FPGA_FAIL_UNKNOWN_IDENTIFIER:  return "unknown identifier";
    case FPGA_FAIL_RESOURCE_EXHAUSTED:  return "resource exhausted";
    default:                            return "error";
    }
}

/* NACK: the command failed. The FIFOs are flushed and the error code is
 * the answer, like the real FPGA. */
static int32_t fail(uint8_t *status, uint8_t code, const char *what)
{
    LOG_WARN("%s: NACK, %s (0x%02X)", what, fail_name(code), code);
    *status    = FPGA_STATUS_NACK;
    ff.out[0]  = code;
    ff.out_len = 1;
    ff.out_pos = 0;
    return READY_US;
}

/* Parameters must be exactly n bytes. */
static bool param_count(uint16_t len, uint16_t n, uint8_t *status, const char *what)
{
    if (len == n) return true;
    fail(status, len < n ? FPGA_FAIL_MISSING_PARAMETER : FPGA_FAIL_TOO_MANY_PARAMETERS, what);
    return false;
}

/* ---- pixel access -----------------------------------------------------
 * A region is w pixels wide starting at (x, y); pixel i lands at
 * (x + i % w, y + i / w). */
static uint16_t *pixel(display_layer_t L, uint32_t x, uint32_t y)
{
    if (x >= FAKE_W || y >= FAKE_H) return NULL;
    return &display_layer(L)[(size_t)y * DISPLAY_W + x];
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* Write whole pixels from src into the grid region. */
static void put_region(const char *what, uint16_t x, uint16_t y, uint16_t w,
                       const uint8_t *src, uint32_t nbytes)
{
    uint32_t npix = nbytes / PIXEL_BYTES, oob = 0;

    if (w == 0) { LOG_WARN("%s: zero width, nothing written", what); return; }
    if (nbytes % PIXEL_BYTES)
        LOG_WARN("%s: %lu bytes is not a whole number of %u-byte pixels, "
                 "last %lu dropped", what, (unsigned long)nbytes, PIXEL_BYTES,
                 (unsigned long)(nbytes % PIXEL_BYTES));

    for (uint32_t i = 0; i < npix; i++) {
        uint16_t *p = pixel(DISPLAY_SIM, x + i % w, y + i / w);
        if (p) *p = be16(&src[2 * i]);
        else   oob++;
    }
    if (oob)
        LOG_WARN("%s at (%u,%u): %lu of %lu pixels out of bounds (%ux%u)",
                 what, x, y, (unsigned long)oob, (unsigned long)npix,
                 FAKE_W, FAKE_H);
}

/* Answer with nbytes read from the grid region. Out of bounds reads as 0. */
static void answer_region(const char *what, uint16_t x, uint16_t y, uint16_t w,
                          uint32_t nbytes)
{
    if (nbytes > OUT_FIFO) {
        LOG_WARN("%s: answer of %lu bytes > %u byte FIFO, truncated",
                 what, (unsigned long)nbytes, OUT_FIFO);
        nbytes = OUT_FIFO;
    }
    if (w == 0) nbytes = 0;

    uint32_t oob = 0;
    for (uint32_t b = 0; b < nbytes; b++) {
        uint32_t i = b / PIXEL_BYTES;
        const uint16_t *p = pixel(DISPLAY_SIM, x + i % w, y + i / w);
        uint16_t v = p ? *p : 0x0000u;
        ff.out[b] = (b % PIXEL_BYTES == 0) ? (uint8_t)(v >> 8) : (uint8_t)v;
        if (!p && b % PIXEL_BYTES == 0) oob++;
    }
    ff.out_len = (uint16_t)nbytes;
    if (oob)
        LOG_WARN("%s at (%u,%u): %lu pixels out of bounds, read as 0",
                 what, x, y, (unsigned long)oob);
}

static void answer_bytes(const uint8_t *src, uint16_t n)
{
    ff.out_len = n < OUT_FIFO ? n : (uint16_t)OUT_FIFO;
    memcpy(ff.out, src, ff.out_len);
}

/* Box header fields -> start, width, height: X,Y in the address phase,
 * W,H in the alt bytes (see fpga.h). */
static void box_extent(const transport_hdr_t *h,
                       uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *hh)
{
    *x  = h->x;
    *y  = h->y;
    *w  = (uint16_t)(h->alt >> 16);
    *hh = (uint16_t)h->alt;
}

/* ---- header sanity ---------------------------------------------------- */
static bool expect_fields(const transport_hdr_t *h, uint8_t fields,
                          const char *what, uint8_t *status)
{
    if (h->fields == fields) return true;
    LOG_WARN("%s: header fields 0x%X, expected 0x%X", what, h->fields, fields);
    fail(status, FPGA_FAIL_MISSING_PARAMETER, what);
    return false;
}

/* ---- sprites ------------------------------------------------------------ */
static bool image_ok(uint8_t mem_id) { return mem_id >= 1 && mem_id <= ff.n_images; }

static bool sprite_used(unsigned id) { return id < MAX_SPRITES && ff.sprite[id].used; }

/* Bytes of sprite memory from this memID's start to the end: an image
 * that spans several uploads in a row may use all of them. */
static uint32_t bytes_from(uint8_t mem_id) { return ff.mem_used - ff.image[mem_id].off; }

static int32_t write_sprite(const uint8_t *data, uint16_t len, uint8_t *status)
{
    if (len == 0 || len % PIXEL_BYTES)
        return fail(status, FPGA_FAIL_ILLEGAL_VALUE, "write_sprite");
    if (ff.n_images + 1u >= MAX_IMAGES || ff.mem_used + len > SPRITE_MEM)
        return fail(status, FPGA_FAIL_RESOURCE_EXHAUSTED, "write_sprite");

    uint8_t id = ++ff.n_images;
    ff.image[id].off = ff.mem_used;
    ff.image[id].len = len;
    memcpy(&ff.mem[ff.mem_used], data, len);
    ff.mem_used += len;
    answer_bytes(&id, 1);
    LOG_DBG("write_sprite: memID %u, %u bytes", id, len);
    return READY_US;
}

static int32_t sprite_write(const uint8_t *p, uint16_t len, uint8_t *status)
{
    if (!param_count(len, 10, status, "sprite_write")) return READY_US;
    uint8_t mem_id = p[0];
    uint16_t w = be16(&p[6]), h = be16(&p[8]);
    if (!image_ok(mem_id))
        return fail(status, FPGA_FAIL_UNKNOWN_IDENTIFIER, "sprite_write");
    if (w == 0 || h == 0 || (uint32_t)w * h * PIXEL_BYTES > bytes_from(mem_id))
        return fail(status, FPGA_FAIL_ILLEGAL_VALUE, "sprite_write");

    for (unsigned id = 0; id < MAX_SPRITES; id++) {     /* lowest free ID */
        if (ff.sprite[id].used) continue;
        ff.sprite[id] = (instance_t){ .used = true, .mem_id = mem_id, .active = p[1] & 1u,
                                      .x = be16(&p[2]), .y = be16(&p[4]), .w = w, .h = h };
        uint8_t sid = (uint8_t)id;
        answer_bytes(&sid, 1);
        return READY_US;
    }
    return fail(status, FPGA_FAIL_RESOURCE_EXHAUSTED, "sprite_write");
}

static int32_t sprite_update(const uint8_t *p, uint16_t len, uint8_t *status)
{
    if (!param_count(len, 6, status, "sprite_update")) return READY_US;
    uint8_t id = p[0];
    if (!sprite_used(id))
        return fail(status, FPGA_FAIL_UNKNOWN_IDENTIFIER, "sprite_update");
    instance_t *s = &ff.sprite[id];
    s->active = p[1] & 1u;
    s->x      = be16(&p[2]);
    s->y      = be16(&p[4]);
    return READY_US;
}

static int32_t sprite_delete(const uint8_t *p, uint16_t len, uint8_t *status)
{
    if (!param_count(len, 1, status, "sprite_delete")) return READY_US;
    if (!sprite_used(p[0]))
        return fail(status, FPGA_FAIL_UNKNOWN_IDENTIFIER, "sprite_delete");
    memset(&ff.sprite[p[0]], 0, sizeof ff.sprite[0]);  /* all parameters 0 */
    return READY_US;
}

/* The HUD layer: every active instance, drawn fresh each frame. */
static void draw_sprites(void)
{
    uint16_t *hud = display_layer(DISPLAY_HUD);
    for (size_t i = 0; i < (size_t)DISPLAY_W * DISPLAY_H; i++) hud[i] = DISPLAY_HUD_CLEAR;

    for (unsigned id = 0; id < MAX_SPRITES; id++) {
        const instance_t *s = &ff.sprite[id];
        if (!s->used || !s->active || !image_ok(s->mem_id)) continue;
        const uint8_t *img = &ff.mem[ff.image[s->mem_id].off];
        for (uint32_t r = 0; r < s->h; r++)
            for (uint32_t c = 0; c < s->w; c++) {
                uint16_t v = be16(&img[(r * s->w + c) * PIXEL_BYTES]);
                uint16_t *p = pixel(DISPLAY_HUD, s->x + c, s->y + r);
                if (p && v != DISPLAY_HUD_CLEAR) *p = v;
            }
    }
}

/* ---- commands --------------------------------------------------------- */
int32_t fake_fpga_write(const transport_hdr_t *h, const uint8_t *data,
                        uint8_t *status)
{
    *status = FPGA_STATUS_OK;

    uint16_t len = h->len;
    uint32_t hdr = 1u + ((h->fields & TRANSPORT_F_XY) ? 4u : 0u)
                      + transport_alt_bytes(h);
    if (hdr + len > MAX_CMD) {
        LOG_WARN("op 0x%02X: %lu byte command > N=%u byte FIFO, "
                 "last %lu bytes dropped", h->opcode,
                 (unsigned long)(hdr + len), MAX_CMD,
                 (unsigned long)(hdr + len - MAX_CMD));
        len = (uint16_t)(MAX_CMD - hdr);
    }

    /* A new command replaces whatever answer was left unread. */
    ff.out_len = ff.out_pos = 0;

    uint16_t x, y, w, hh;
    switch (h->opcode) {
    case ECHO_OPCODE:
        answer_bytes(data, len);
        break;

    case WRITE_CONS_OPCODE:
        if (!expect_fields(h, TRANSPORT_F_XY | TRANSPORT_F_ALT16, "write_cons", status))
            break;
        if ((uint16_t)h->alt != len)       /* N = payload bytes */
            LOG_WARN("write_cons: N=%u but %u payload bytes", (unsigned)(uint16_t)h->alt, len);
        put_region("write_cons", h->x, h->y, FAKE_W, data, len);  /* contiguous */
        break;

    case READ_CONS_OPCODE:
        if (!expect_fields(h, TRANSPORT_F_XY | TRANSPORT_F_ALT16, "read_cons", status))
            break;
        answer_region("read_cons", h->x, h->y, FAKE_W, (uint16_t)h->alt);
        break;

    case WRITE_BOX_OPCODE:
        if (!expect_fields(h, TRANSPORT_F_XY | TRANSPORT_F_ALT32, "write_box", status)) break;
        box_extent(h, &x, &y, &w, &hh);
        if ((uint32_t)w * hh * PIXEL_BYTES != len)
            LOG_WARN("write_box: %ux%u box is %lu bytes, got %u", w, hh,
                     (unsigned long)w * hh * PIXEL_BYTES, len);
        put_region("write_box", x, y, w, data, len);
        break;

    case READ_BOX_OPCODE:
        if (!expect_fields(h, TRANSPORT_F_XY | TRANSPORT_F_ALT32, "read_box", status)) break;
        box_extent(h, &x, &y, &w, &hh);
        answer_region("read_box", x, y, w, (uint32_t)w * hh * PIXEL_BYTES);
        break;

    case WRITE_SPRITE_OPCODE:  return write_sprite(data, len, status);
    case SPRITE_WRITE_OPCODE:  return sprite_write(data, len, status);
    case SPRITE_UPDATE_OPCODE: return sprite_update(data, len, status);
    case SPRITE_DELETE_OPCODE: return sprite_delete(data, len, status);

    case STOP_SIM_OPCODE:
    case START_SIM_OPCODE: {
        bool run = h->opcode == START_SIM_OPCODE;
        if (len != 0 || h->fields != 0)
            LOG_WARN("%s: expected the opcode only, got %u payload bytes "
                     "(fields 0x%X)", run ? "start_sim" : "stop_sim", len, h->fields);
        if (ff.sim_running != run)
            LOG_INFO("simulation %s", run ? "started" : "stopped");
        ff.sim_running = run;
        break;
    }

    case INFO_OPCODE: {
        /* [W][H][N] 16 bits each, [SID] 32 bits, MSB first */
        const uint8_t info[FPGA_INFO_BYTES] = {
            (uint8_t)(FAKE_W  >> 8), (uint8_t)FAKE_W,
            (uint8_t)(FAKE_H  >> 8), (uint8_t)FAKE_H,
            (uint8_t)(MAX_CMD >> 8), (uint8_t)MAX_CMD,
            (uint8_t)(ff.session >> 24), (uint8_t)(ff.session >> 16),
            (uint8_t)(ff.session >> 8),  (uint8_t)ff.session
        };
        answer_bytes(info, sizeof info);
        break;
    }

    default:
        return fail(status, FPGA_FAIL_UNKNOWN_COMMAND, "unknown opcode");
    }
    return READY_US;
}

void fake_fpga_read(const transport_hdr_t *h, uint8_t *buf)
{
    if (h->opcode != READ_OPCODE)
        LOG_WARN("read with opcode 0x%02X, FPGA ignores it (expects 0x%02X)",
                 h->opcode, READ_OPCODE);

    uint16_t have = (uint16_t)(ff.out_len - ff.out_pos);
    uint16_t n    = h->len < have ? h->len : have;
    memcpy(buf, ff.out + ff.out_pos, n);
    memset(buf + n, 0x00, (size_t)(h->len - n));
    ff.out_pos = (uint16_t)(ff.out_pos + n);

    if (n < h->len)
        LOG_WARN("read %u bytes, answer FIFO only had %u -- rest is 0x00",
                 h->len, have);
}

/* ---- picture ----------------------------------------------------------
 * The SIM layer is the grid, written directly. The HUD layer is redrawn
 * from the sprite instances once per frame, then the window composites
 * both at its next frame. */
bool fake_fpga_poll(uint64_t now_ns)
{
    /* SYNC: one pulse per frame period. If the host loop stalled, pulse
     * once for every period that went by, like the real FPGA would have
     * while the MCU was busy -- the counter shows the MCU fell behind. */
    if (ff.next_sync_ns == 0) ff.next_sync_ns = now_ns + FRAME_NS;
    bool frame = false;
    while (now_ns >= ff.next_sync_ns) {
        ff.next_sync_ns += FRAME_NS;
        frame_sync_isr();
        frame = true;
    }
    if (frame) draw_sprites();
    return display_poll();
}

void fake_fpga_reset(void)
{
    /* The layers are not cleared here: display_init() already did. */
    memset(&ff, 0, sizeof ff);

    /* A fresh id per "power-up", like an FPGA that just got configured. */
    srand((unsigned)time(NULL));
    ff.session = (uint32_t)(rand() & 0xFFFF);
    ff.session = ff.session | (uint32_t)(rand() & 0xFFFF) << 16;

    LOG_INFO("fake fpga: %ux%u, N=%u, session 0x%08lX, %u KB sprite memory, "
             "%u images, %u sprites, ready %d us, sync %u Hz", FAKE_W, FAKE_H,
             MAX_CMD, (unsigned long)ff.session, SPRITE_MEM / 1024u,
             MAX_IMAGES - 1u, MAX_SPRITES, READY_US, FRAME_HZ);
}

/* For tests and the console: what the FPGA has in its sprite tables. */
void fake_fpga_sprite_stats(unsigned *sprites, unsigned *images, uint32_t *mem_bytes)
{
    unsigned n = 0;
    for (unsigned id = 0; id < MAX_SPRITES; id++) n += ff.sprite[id].used;
    *sprites   = n;
    *images    = ff.n_images;
    *mem_bytes = ff.mem_used;
}