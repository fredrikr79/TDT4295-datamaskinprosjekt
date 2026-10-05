/* ======================================================================
 * fake_fpga.c - what the FPGA does with the commands, for the host build.
 *
 * Model:
 *   - Two pixel layers, SIM and HUD, RGB565. They live in display_host.c
 *     (display_layer()), which shows the HUD over the SIM layer with
 *     DISPLAY_HUD_CLEAR pixels see-through. Pixels travel MSB first,
 *     2 bytes each.
 *   - Commands arrive through an input FIFO of MAX_CMD bytes (N in the
 *     INFO answer), header included. Bytes beyond that are dropped with a
 *     warning -- fpga.c should never let that happen once it knows N.
 *   - Commands with an answer (echo, read_*, info) fill the output FIFO
 *     when they are processed. READ transactions (READ_OPCODE is a dummy
 *     the FPGA ignores) drain it; reading past the end gives 0x00.
 *   - Every command is answered with READY after READY_US, with a status
 *     on the 3 status pins. Always FPGA_STATUS_OK until codes are agreed.
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
#define PIXEL_BYTES   2u            /* RGB565, MSB first (PROTOCOL)        */
#define READY_US      200           /* command -> READY pulse              */
#define FRAME_HZ      30u           /* SYNC pulses: one per finished frame */
#define FRAME_NS      (1000000000ull / FRAME_HZ)

/* PROTOCOL: box = [X1][Y1][X2][Y2]. 1: X2,Y2 are width and height.
 * 0: X2,Y2 are the inclusive opposite corner. */
#define BOX_IS_WH     1

/* PROTOCOL: which layer the line commands work on. */
#define LINE_LAYER    DISPLAY_HUD

/* ---- state ------------------------------------------------------------ */
static struct {
    uint8_t  out[OUT_FIFO];     /* answer FIFO                              */
    uint16_t out_len, out_pos;
    bool     sim_running;
    uint32_t session;           /* new on every reset, reported by INFO     */
    uint64_t next_sync_ns;      /* 0 = not started                          */
} ff;

/* ---- pixel access -----------------------------------------------------
 * A region is w pixels wide starting at (x, y); pixel i lands at
 * (x + i % w, y + i / w). A line is a region too wide to ever wrap, so
 * running off the right edge is out of bounds rather than the next row. */
#define LINE_W  UINT16_MAX

static uint16_t *pixel(display_layer_t L, uint32_t x, uint32_t y)
{
    if (x >= FAKE_W || y >= FAKE_H) return NULL;
    return &display_layer(L)[(size_t)y * DISPLAY_W + x];
}

/* Write whole pixels from src into the region. */
static void put_region(display_layer_t L, const char *what, uint16_t x,
                       uint16_t y, uint16_t w, const uint8_t *src,
                       uint32_t nbytes)
{
    uint32_t npix = nbytes / PIXEL_BYTES, oob = 0;

    if (w == 0) { LOG_WARN("%s: zero width, nothing written", what); return; }
    if (nbytes % PIXEL_BYTES)
        LOG_WARN("%s: %lu bytes is not a whole number of %u-byte pixels, "
                 "last %lu dropped", what, (unsigned long)nbytes, PIXEL_BYTES,
                 (unsigned long)(nbytes % PIXEL_BYTES));

    for (uint32_t i = 0; i < npix; i++) {
        uint16_t *p = pixel(L, x + i % w, y + i / w);
        if (p) *p = (uint16_t)(src[2 * i] << 8 | src[2 * i + 1]);
        else   oob++;
    }
    if (oob)
        LOG_WARN("%s at (%u,%u): %lu of %lu pixels out of bounds (%ux%u)",
                 what, x, y, (unsigned long)oob, (unsigned long)npix,
                 FAKE_W, FAKE_H);
}

/* Answer with nbytes read from the region. Out of bounds reads as 0. */
static void answer_region(display_layer_t L, const char *what, uint16_t x,
                          uint16_t y, uint16_t w, uint32_t nbytes)
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
        const uint16_t *p = pixel(L, x + i % w, y + i / w);
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

/* Box header fields -> start, width, height. */
static void box_extent(const transport_hdr_t *h,
                       uint16_t *x, uint16_t *y, uint16_t *w, uint16_t *hh)
{
    uint16_t a = (uint16_t)(h->alt >> 16), b = (uint16_t)h->alt;
    *x = h->x;
    *y = h->y;
#if BOX_IS_WH
    *w = a;  *hh = b;
#else
    *w  = a >= h->x ? (uint16_t)(a - h->x + 1u) : 0u;
    *hh = b >= h->y ? (uint16_t)(b - h->y + 1u) : 0u;
#endif
}

/* ---- header sanity ---------------------------------------------------- */
static bool expect_fields(const transport_hdr_t *h, uint8_t fields,
                          const char *what)
{
    if (h->fields == fields) return true;
    LOG_WARN("%s: header fields 0x%X, expected 0x%X -- ignored",
             what, h->fields, fields);
    return false;
}

/* ---- commands --------------------------------------------------------- */
int32_t fake_fpga_write(const transport_hdr_t *h, const uint8_t *data,
                        uint8_t *status)
{
    *status = FPGA_STATUS_OK;   /* TODO: real codes once they are agreed */

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

    case SEND_LINE_OPCODE:
        if (!expect_fields(h, TRANSPORT_F_XY | TRANSPORT_F_ALT16, "send_line"))
            break;
        if ((uint16_t)h->alt != len)       /* PROTOCOL: N = line bytes */
            LOG_WARN("send_line: N=%u but %u payload bytes", (unsigned)(uint16_t)h->alt, len);
        put_region(LINE_LAYER, "send_line", h->x, h->y, LINE_W, data, len);
        break;

    case READ_LINE_OPCODE:
        if (!expect_fields(h, TRANSPORT_F_XY | TRANSPORT_F_ALT16, "read_line"))
            break;
        answer_region(LINE_LAYER, "read_line", h->x, h->y, LINE_W,
                      (uint16_t)h->alt);
        break;

    case SEND_HUD_BOX_OPCODE:
    case SEND_SIM_BOX_OPCODE: {
        bool is_hud = h->opcode == SEND_HUD_BOX_OPCODE;
        const char *what = is_hud ? "send_hud_box" : "send_sim_box";
        if (!expect_fields(h, TRANSPORT_F_XY | TRANSPORT_F_ALT32, what)) break;
        box_extent(h, &x, &y, &w, &hh);
        if ((uint32_t)w * hh * PIXEL_BYTES != len)
            LOG_WARN("%s: %ux%u box is %lu bytes, got %u", what, w, hh,
                     (unsigned long)w * hh * PIXEL_BYTES, len);
        put_region(is_hud ? DISPLAY_HUD : DISPLAY_SIM, what, x, y, w, data, len);
        break;
    }

    case READ_HUD_BOX_OPCODE:
    case READ_SIM_BOX_OPCODE: {
        bool is_hud = h->opcode == READ_HUD_BOX_OPCODE;
        const char *what = is_hud ? "read_hud_box" : "read_sim_box";
        if (!expect_fields(h, TRANSPORT_F_XY | TRANSPORT_F_ALT32, what)) break;
        box_extent(h, &x, &y, &w, &hh);
        answer_region(is_hud ? DISPLAY_HUD : DISPLAY_SIM, what, x, y, w,
                      (uint32_t)w * hh * PIXEL_BYTES);
        break;
    }

    case STOP_SIM_OPCODE:
    case START_SIM_OPCODE: {
        bool run = h->opcode == START_SIM_OPCODE;
        if (len != 1 || (data[0] & 1u) != (run ? 1u : 0u))
            LOG_WARN("%s: expected payload [XXXXXXX%u]", run ? "start_sim"
                     : "stop_sim", run ? 1u : 0u);
        if (ff.sim_running != run)
            LOG_INFO("simulation %s", run ? "started" : "stopped");
        ff.sim_running = run;
        break;
    }

    case INFO_OPCODE: {
        /* [W][H][N][SID], 16 bits each, MSB first */
        const uint8_t info[FPGA_INFO_BYTES] = {
            (uint8_t)(FAKE_W  >> 8), (uint8_t)FAKE_W,
            (uint8_t)(FAKE_H  >> 8), (uint8_t)FAKE_H,
            (uint8_t)(MAX_CMD >> 8), (uint8_t)MAX_CMD,
            (uint8_t)(ff.session >> 24), (uint8_t)(ff.session >> 16), (uint8_t)(ff.session >> 8),(uint8_t)ff.session
        };
        answer_bytes(info, sizeof info);
        break;
    }

    default:
        LOG_WARN("unknown opcode 0x%02X (%u payload bytes)", h->opcode, len);
        break;
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
 * The layers are display_host's, so there is nothing to render here: the
 * window composites them itself at its next frame. */
bool fake_fpga_poll(uint64_t now_ns)
{
    /* SYNC: one pulse per frame period. If the host loop stalled, pulse
     * once for every period that went by, like the real FPGA would have
     * while the MCU was busy -- the counter shows the MCU fell behind. */
    if (ff.next_sync_ns == 0) ff.next_sync_ns = now_ns + FRAME_NS;
    while (now_ns >= ff.next_sync_ns) {
        ff.next_sync_ns += FRAME_NS;
        frame_sync_isr();
    }
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


    LOG_INFO("fake fpga: %ux%u, N=%u, %u bytes/pixel, session 0x%04X, "
             "ready %d us, sync %u Hz", FAKE_W, FAKE_H, MAX_CMD, PIXEL_BYTES,
             ff.session, READY_US, FRAME_HZ);
}