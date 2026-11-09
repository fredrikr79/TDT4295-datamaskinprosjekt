/* ======================================================================
 * fake_fpga.c - what the FPGA does with the commands, for the host build.
 * Follows docs/mcu_fpga_spi_api.md; see fpga.h for the command table.
 *
 * Model:
 *   - The grid is the SIM layer, the HUD is sprites. Both layers live in
 *     display_host.c (display_layer()), which shows the HUD over the SIM
 *     layer with DISPLAY_HUD_CLEAR pixels see-through. Pixels travel MSB
 *     first, 2 bytes each.
 *   - The grid holds a 4-bit material ID per cell (SIM_AIR, SIM_SAND...,
 *     fpga.h), shown through a colour lookup table (sim_lut) like the real
 *     FPGA's. WRITE/READ_CONS move IDs: N bytes from cell (X, Y) on, row
 *     after row, one or two IDs per byte (FPGA_SIM_IDS_PER_BYTE).
 *   - WRITE/READ_BOX move IDs too: W x H cells from (X, Y), one stream row
 *     by row, packed the same way. Only the HUD layer (sprites) is RGB565.
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

/* The real FPGA has 16 MB; memIDs run out long before 2 MB is used
 * (1023 uploads of at most N-1 bytes). */
#define SPRITE_MEM    (2u * 1024u * 1024u)  /* bytes of sprite memory      */
#define MAX_IMAGES    1024u         /* memID LUT: memIDs 1 .. MAX_IMAGES-1 */
#define MAX_SPRITES   1024u         /* PROTOCOL: spriteIDs 0 .. 1023       */

/* ---- state ------------------------------------------------------------ */
typedef struct {
    bool     used;
    uint16_t mem_id;
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
    uint16_t   n_images;        /* memIDs handed out: 1 .. n_images         */
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

/* ---- the grid: material IDs, and the colours they show as ------------- */
#define LUT565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

/* PROTOCOL: the FPGA team's colours aren't known; these are stand-ins. */
static const uint16_t sim_lut[SIM_ID_MAX + 1] = {
    [SIM_AIR]   = LUT565(  0,   0,   0),
    [SIM_SAND]  = LUT565(222, 190, 110),
    [SIM_WATER] = LUT565( 40, 110, 225),
    [SIM_WALL]  = LUT565(125, 125, 135),
    [SIM_LIFE]  = LUT565( 90, 230,  90),
    [5 ... 15]  = LUT565(255,   0, 200),    /* not in use: shows up loud */
};

static uint8_t cells[FAKE_W * FAKE_H];      /* one ID per cell, row after row */

/* ---- pixel access -----------------------------------------------------
 * A region is w pixels wide starting at (x, y); pixel i lands at
 * (x + i % w, y + i / w). */
static uint16_t *pixel(display_layer_t L, uint32_t x, uint32_t y)
{
    if (x >= FAKE_W || y >= FAKE_H) return NULL;
    return &display_layer(L)[(size_t)y * DISPLAY_W + x];
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* Cell i (counted from the top-left, row after row) gets material id, and
 * the SIM picture its colour. */
static void cell_set(uint32_t i, uint8_t id)
{
    cells[i] = id;
    uint16_t *p = pixel(DISPLAY_SIM, i % FAKE_W, i / FAKE_W);
    if (p) *p = sim_lut[id];
}

/* The k-th ID in packed data. */
static uint8_t packed_id(const uint8_t *data, uint32_t k)
{
    uint8_t b = data[k / FPGA_SIM_IDS_PER_BYTE];
    if (FPGA_SIM_IDS_PER_BYTE == 2) return (k & 1u) ? (b & 0x0Fu) : (uint8_t)(b >> 4);
    return b & 0x0Fu;
}

/* WRITE_CONS: IDs from (x, y) on, contiguous. */
static void cons_write(uint16_t x, uint16_t y, const uint8_t *data, uint16_t nbytes)
{
    uint32_t start = (uint32_t)y * FAKE_W + x, n = (uint32_t)nbytes * FPGA_SIM_IDS_PER_BYTE;
    uint32_t oob = 0, big = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint8_t id = packed_id(data, k);
        if (FPGA_SIM_IDS_PER_BYTE == 1) big += data[k] > SIM_ID_MAX;
        if (x >= FAKE_W || start + k >= (uint32_t)FAKE_W * FAKE_H) { oob++; continue; }
        cell_set(start + k, id);
    }
    if (oob) LOG_WARN("write_cons at (%u,%u): %lu of %lu cells out of bounds (%ux%u)",
                      x, y, (unsigned long)oob, (unsigned long)n, FAKE_W, FAKE_H);
    if (big) LOG_WARN("write_cons at (%u,%u): %lu byte(s) above %u -- only 4 bits are used",
                      x, y, (unsigned long)big, SIM_ID_MAX);
}

/* Cell k of a W-wide box at (x, y): its index in cells[], or -1 if it's
 * off the grid. */
static int32_t box_cell(uint16_t x, uint16_t y, uint16_t w, uint32_t k)
{
    uint32_t cx = x + k % w, cy = y + k / w;
    if (cx >= FAKE_W || cy >= FAKE_H) return -1;
    return (int32_t)(cy * FAKE_W + cx);
}

/* WRITE_BOX: w x h IDs, row by row. */
static void box_write(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      const uint8_t *data, uint16_t nbytes)
{
    uint32_t n = (uint32_t)w * h, oob = 0;
    if (fpga_sim_bytes(n) != nbytes) {
        LOG_WARN("write_box: %ux%u box is %lu bytes, got %u", w, h,
                 (unsigned long)fpga_sim_bytes(n), nbytes);
        if ((uint32_t)nbytes * FPGA_SIM_IDS_PER_BYTE < n) n = (uint32_t)nbytes * FPGA_SIM_IDS_PER_BYTE;
    }
    for (uint32_t k = 0; k < n; k++) {
        int32_t c = box_cell(x, y, w, k);
        if (c < 0) { oob++; continue; }
        cell_set((uint32_t)c, packed_id(data, k));
    }
    if (oob) LOG_WARN("write_box at (%u,%u): %lu of %lu cells out of bounds (%ux%u)",
                      x, y, (unsigned long)oob, (unsigned long)n, FAKE_W, FAKE_H);
}

/* READ_BOX: w x h IDs, row by row. Off the grid reads as air. */
static void box_read(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint32_t n = (uint32_t)w * h, nbytes = fpga_sim_bytes(n);
    if (nbytes > OUT_FIFO) {
        LOG_WARN("read_box: answer of %lu bytes > %u byte FIFO, truncated",
                 (unsigned long)nbytes, OUT_FIFO);
        nbytes = OUT_FIFO;
    }
    memset(ff.out, 0, nbytes);
    for (uint32_t k = 0; k < n && k / FPGA_SIM_IDS_PER_BYTE < nbytes; k++) {
        int32_t c = box_cell(x, y, w, k);
        uint8_t id = c < 0 ? SIM_AIR : cells[c];
        if (FPGA_SIM_IDS_PER_BYTE == 2) ff.out[k / 2] |= (k & 1u) ? id : (uint8_t)(id << 4);
        else                            ff.out[k] = id;
    }
    ff.out_len = (uint16_t)nbytes;
}

/* READ_CONS: nbytes of IDs from (x, y) on. Out of bounds reads as air. */
static void cons_read(uint16_t x, uint16_t y, uint32_t nbytes)
{
    if (nbytes > OUT_FIFO) {
        LOG_WARN("read_cons: answer of %lu bytes > %u byte FIFO, truncated",
                 (unsigned long)nbytes, OUT_FIFO);
        nbytes = OUT_FIFO;
    }
    uint32_t start = (uint32_t)y * FAKE_W + x, total = (uint32_t)FAKE_W * FAKE_H;
    for (uint32_t b = 0; b < nbytes; b++) {
        uint8_t v = 0;
        for (uint32_t j = 0; j < FPGA_SIM_IDS_PER_BYTE; j++) {
            uint32_t k = b * FPGA_SIM_IDS_PER_BYTE + j;
            uint8_t id = (x < FAKE_W && start + k < total) ? cells[start + k] : SIM_AIR;
            v = (uint8_t)(FPGA_SIM_IDS_PER_BYTE == 2 ? (v << 4) | id : id);
        }
        ff.out[b] = v;
    }
    ff.out_len = (uint16_t)nbytes;
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
static bool image_ok(uint16_t mem_id) { return mem_id >= 1 && mem_id <= ff.n_images; }

static bool sprite_used(unsigned id) { return id < MAX_SPRITES && ff.sprite[id].used; }

/* Bytes of sprite memory from this memID's start to the end: an image
 * that spans several uploads in a row may use all of them. */
static uint32_t bytes_from(uint16_t mem_id) { return ff.mem_used - ff.image[mem_id].off; }

static int32_t write_sprite(const uint8_t *data, uint16_t len, uint8_t *status)
{
    if (len == 0 || len % PIXEL_BYTES)
        return fail(status, FPGA_FAIL_ILLEGAL_VALUE, "write_sprite");
    if (ff.n_images + 1u >= MAX_IMAGES || ff.mem_used + len > SPRITE_MEM)
        return fail(status, FPGA_FAIL_RESOURCE_EXHAUSTED, "write_sprite");

    uint16_t id = ++ff.n_images;
    ff.image[id].off = ff.mem_used;
    ff.image[id].len = len;
    memcpy(&ff.mem[ff.mem_used], data, len);
    ff.mem_used += len;
    uint8_t ans[2] = { (uint8_t)(id >> 8), (uint8_t)id };     /* MSB first */
    answer_bytes(ans, 2);
    LOG_DBG("write_sprite: memID %u, %u bytes", id, len);
    return READY_US;
}

static int32_t sprite_write(const uint8_t *p, uint16_t len, uint8_t *status)
{
    if (!param_count(len, 11, status, "sprite_write")) return READY_US;
    uint16_t mem_id = be16(&p[0]);
    uint16_t w = be16(&p[7]), h = be16(&p[9]);
    if (!image_ok(mem_id))
        return fail(status, FPGA_FAIL_UNKNOWN_IDENTIFIER, "sprite_write");
    if (w == 0 || h == 0 || (uint32_t)w * h * PIXEL_BYTES > bytes_from(mem_id))
        return fail(status, FPGA_FAIL_ILLEGAL_VALUE, "sprite_write");

    for (unsigned id = 0; id < MAX_SPRITES; id++) {     /* lowest free ID */
        if (ff.sprite[id].used) continue;
        ff.sprite[id] = (instance_t){ .used = true, .mem_id = mem_id, .active = p[2] & 1u,
                                      .x = be16(&p[3]), .y = be16(&p[5]), .w = w, .h = h };
        uint8_t ans[2] = { (uint8_t)(id >> 8), (uint8_t)id };
        answer_bytes(ans, 2);
        return READY_US;
    }
    return fail(status, FPGA_FAIL_RESOURCE_EXHAUSTED, "sprite_write");
}

static int32_t sprite_update(const uint8_t *p, uint16_t len, uint8_t *status)
{
    if (!param_count(len, 7, status, "sprite_update")) return READY_US;
    uint16_t id = be16(&p[0]);
    if (!sprite_used(id))
        return fail(status, FPGA_FAIL_UNKNOWN_IDENTIFIER, "sprite_update");
    instance_t *s = &ff.sprite[id];
    s->active = p[2] & 1u;
    s->x      = be16(&p[3]);
    s->y      = be16(&p[5]);
    return READY_US;
}

static int32_t sprite_delete(const uint8_t *p, uint16_t len, uint8_t *status)
{
    if (!param_count(len, 2, status, "sprite_delete")) return READY_US;
    uint16_t id = be16(&p[0]);
    if (!sprite_used(id))
        return fail(status, FPGA_FAIL_UNKNOWN_IDENTIFIER, "sprite_delete");
    memset(&ff.sprite[id], 0, sizeof ff.sprite[0]);    /* all parameters 0 */
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
        cons_write(h->x, h->y, data, len);
        break;

    case READ_CONS_OPCODE:
        if (!expect_fields(h, TRANSPORT_F_XY | TRANSPORT_F_ALT16, "read_cons", status))
            break;
        cons_read(h->x, h->y, (uint16_t)h->alt);
        break;

    case WRITE_BOX_OPCODE:
        if (!expect_fields(h, TRANSPORT_F_XY | TRANSPORT_F_ALT32, "write_box", status)) break;
        box_extent(h, &x, &y, &w, &hh);
        box_write(x, y, w, hh, data, len);
        break;

    case READ_BOX_OPCODE:
        if (!expect_fields(h, TRANSPORT_F_XY | TRANSPORT_F_ALT32, "read_box", status)) break;
        box_extent(h, &x, &y, &w, &hh);
        box_read(x, y, w, hh);
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
    for (uint32_t i = 0; i < (uint32_t)FAKE_W * FAKE_H; i++) cell_set(i, SIM_AIR);

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