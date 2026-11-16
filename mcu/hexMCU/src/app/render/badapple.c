/* ======================================================================
 * badapple.c - a 1 bpp video from the SD card, drawn on the SIM grid.
 *
 * The file: 120x90 frames, 1350 bytes each (15 bytes a row, MSB = the
 * leftmost pixel, 1 = white), back to back, 25 fps -- what
 *   ffmpeg ... -pix_fmt monob -f rawvideo badapple.bin
 * makes (BA_FPS must match its fps=). Each pixel becomes one cell byte
 * (BA_WHITE / BA_BLACK), and the video sits in the middle of the grid
 * (x 20..139 on a 160x90 grid).
 *
 * How:
 *   start   STOP_SIM, then the whole grid black (WRITE_BOX bands)
 *   play    every frame (40 ms at 25 fps): the frame that is due,
 *           compared with the last one sent, byte by byte (8 pixels). Only what changed goes out, as
 *           few WRITE_BOXes as N allows:
 *             - a box is only as wide as the changed columns of its rows,
 *               so a narrow change fits more rows in one command
 *             - unchanged rows between changed ones are sent along
 *               (up to BA_MAX_GAP of them) instead of starting a new box
 *           Every command costs a READY handshake, so fewer, fuller
 *           commands is what makes busy scenes keep up.
 *           Every BA_REFRESH_EVERY frames (default: every frame) the
 *           frame is sent whole instead, so a wrong cell can't stick.
 *
 * The frames are read from the card BA_CHUNK at a time (storage_read()
 * opens, seeks and closes the file on every call), in the idle time
 * between frames.
 *
 * Box payloads are built in tx[] when the frame starts, one box after the
 * other, and DMA'd from there. tx[] is not touched again until every
 * command of the frame is done. If a frame isn't out by the time the next
 * is due, the video skips ahead (dropped frames) instead of falling
 * behind; the rows are always compared with the frame last SENT, so a
 * skip still draws the right picture.
 *
 * Each frame starts right after the FPGA's frame SYNC (BA_WAIT_SYNC), so
 * the writes land at the same point of the screen refresh every time.
 *
 * With loop on, it starts over from frame 0 at the end, until
 * "badapple stop".
 *
 * Timing: every frame's send time is measured, and when the video ends
 * or is stopped the log says how many frames were too slow, which one was
 * the slowest, and roughly what one WRITE_BOX costs and what a KB of
 * payload costs (a least-squares fit over all frames). That says what
 * limits busy scenes: the per-command handshake or the bytes.
 *
 * A failed command (fpga.c then cancels everything behind it) stops the
 * video.
 * ====================================================================== */
#define LOG_TAG "apple"

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "badapple.h"
#include "fpga.h"
#include "storage.h"
#include "platform.h"
#include "log.h"
#include "game.h"
#include "frame_sync.h"

#if FPGA_SIM_IDS_PER_BYTE != 1
#error "badapple.c sends one cell per byte (FPGA_SIM_IDS_PER_BYTE 1)"
#endif

#define BA_W          120u
#define BA_H          90u
#define BA_ROW_BYTES  (BA_W / 8u)               /* 15 packed bytes a row  */
#define BA_FRAME      (BA_ROW_BYTES * BA_H)     /* 1350 bytes a frame     */
/* Must match the fps the file was made with (ffmpeg's fps=). */
#ifndef BA_FPS
#define BA_FPS        25u
#endif
#define BA_FRAME_MS   (1000u / BA_FPS)          /* 40 ms at 25 fps        */
#define BA_CHUNK      16u                       /* frames per SD read     */

/* Unchanged rows a box may carry to reach the next changed one, instead
 * of a new command. If the timing log says bytes cost more than commands,
 * make this smaller (0: never). */
#ifndef BA_MAX_GAP
#define BA_MAX_GAP    BA_H
#endif

/* How often a frame is sent whole instead of only what changed:
 *   1  every frame (default): any cell that went wrong (a glitch on the
 *      link, something else drawing into the grid) is fixed by the next
 *      frame, so nothing sticks
 *   N  every Nth frame whole, only the changes in between
 *   0  never: only changes (least traffic, but a wrong cell stays wrong
 *      until that spot changes in the video)
 * A whole frame is 10800 bytes, about 11 WRITE_BOXes at N=1024. */
#ifndef BA_REFRESH_EVERY
#define BA_REFRESH_EVERY 1u
#endif

/* Start each frame's writes right after an FPGA frame SYNC, so they land
 * at the same point of the screen refresh every time (a tear, if there is
 * one, stays put instead of wandering; writes that finish ahead of the
 * scan don't tear at all). If SYNC doesn't come within a frame time, the
 * frame goes anyway; after BA_SYNC_GIVE_UP of those in a row the video
 * stops waiting for it. 0: never wait, frames on the MCU's clock only. */
#ifndef BA_WAIT_SYNC
#define BA_WAIT_SYNC 1
#endif
#define BA_SYNC_GIVE_UP 3u

/* The two cell bytes. Pick the material the FPGA's colour table draws
 * lightest for white. The simulation is stopped, so nothing moves. */
#define BA_BLACK      SIM_CELL(MAT_AIR)
#define BA_WHITE      SIM_CELL(MAT_SAND)

typedef enum { BA_OFF, BA_CLEAR, BA_PLAY } phase_t;

typedef struct {
    uint8_t  x, y, w, h;        /* in the video, cells                  */
    uint16_t off;               /* payload in tx[]                      */
} band_t;

static struct {
    phase_t  phase;
    char     path[48];
    uint32_t frames;            /* in the file                          */
    uint16_t grid_w, grid_h;    /* the grid, cells                      */
    uint16_t x0, y0;            /* the video's top-left cell            */
    uint16_t maxp;              /* most payload in one WRITE_BOX        */
    uint16_t clear_rows;        /* grid rows in one clearing WRITE_BOX  */
    uint16_t clear_row;         /* next grid row to clear               */

    uint32_t t0;                /* when frame 0 was due                 */
    bool     started;           /* a frame has been sent this round     */
    uint32_t shown;             /* index of the frame last sent         */
    uint32_t sent, dropped;
    bool     loop;              /* start over at the end                */
    uint32_t loops;             /* times it has started over            */

    /* frame SYNC */
    bool     sync_on;           /* still waiting for SYNC (not given up) */
    bool     sync_waiting;      /* a frame is due, waiting for a SYNC   */
    uint32_t sync_mark;         /* SYNC count when the wait began       */
    uint32_t sync_wait_t;
    uint32_t sync_missed;       /* frames sent without one, in a row    */
    uint32_t sync_missed_all;
    uint32_t sync_c0, sync_t0;  /* for the SYNC rate in the log         */

    uint8_t  in_flight;         /* our commands queued, done not called */
    bool     failed;

    uint32_t chunk_first;       /* frame index of chunk[0]              */
    uint32_t chunk_n;           /* frames in chunk[]                    */

    band_t   bands[BA_H];       /* this frame's boxes                   */
    uint8_t  nbands, next_band; /* next_band: first one not queued yet  */

    /* timing of the frame being sent, and over all frames */
    bool     timing;            /* a frame with commands is out         */
    uint32_t f_t, f_bytes;
    uint16_t f_cmds;
    uint32_t st_n, st_slow;
    uint32_t worst_ms, worst_frame, worst_bytes;
    uint16_t worst_cmds;
    double   s_cc, s_bb, s_cb, s_ct, s_bt;  /* sums for the fit          */

    uint8_t  prev[BA_FRAME];    /* the frame last sent, packed          */
    uint8_t  chunk[BA_CHUNK * BA_FRAME];
    uint8_t  tx[BA_W * BA_H] __attribute__((aligned(8)));
} ba;

/* 8 packed pixels -> 8 cell bytes in one store (little-endian: byte 0 is
 * the leftmost pixel, the MSB of the packed byte). */
static uint64_t lut[256];

static void lut_init(void)
{
    for (unsigned v = 0; v < 256u; v++) {
        uint64_t w = 0;
        for (unsigned i = 0; i < 8u; i++) {
            uint64_t cell = (v & (0x80u >> i)) ? BA_WHITE : BA_BLACK;
            w |= cell << (8u * i);
        }
        lut[v] = w;
    }
}

/* ---- FPGA commands ----------------------------------------------------- */
static void cmd_done(const fpga_cmd_t *c, fpga_err_t err)
{
    (void)c;
    ba.in_flight--;
    /* The first error is the real one; the CANCELLED ones behind it, and
     * the ABORTED ones from our own stop, say nothing new. */
    if (err != FPGA_ERR_NONE && ba.phase != BA_OFF && !ba.failed) {
        ba.failed = true;
        LOG_ERR("%s", fpga_last_failure());
    }
}

/* Queue c with our callback. false: not queued -- queue full (try again
 * next poll), or refused (then ba.failed is set). */
static bool submit(fpga_cmd_t *c)
{
    c->done = cmd_done;
    fpga_status_t st = fpga_submit(c);
    if (st == FPGA_OK) { ba.in_flight++; return true; }
    if (st != FPGA_EFULL) {
        LOG_ERR("command not queued: %s", fpga_status_str(st));
        ba.failed = true;
    }
    return false;
}

/* The whole grid black, a band of rows at a time, from tx[] (all
 * BA_BLACK at this point). */
static void submit_clear(void)
{
    while (ba.clear_row < ba.grid_h) {
        uint16_t h = ba.clear_rows;
        if (h > ba.grid_h - ba.clear_row) h = (uint16_t)(ba.grid_h - ba.clear_row);
        fpga_cmd_t c = fpga_cmd_write_box(fpga_box(0, ba.clear_row, ba.grid_w, h),
                                          ba.tx, (uint16_t)(h * ba.grid_w));
        if (!submit(&c)) return;
        ba.clear_row = (uint16_t)(ba.clear_row + h);
    }
}

/* This frame's boxes, as many as the queue takes now. */
static void submit_bands(void)
{
    while (ba.next_band < ba.nbands) {
        const band_t *b = &ba.bands[ba.next_band];
        uint16_t len = (uint16_t)(b->w * b->h);
        fpga_cmd_t c = fpga_cmd_write_box(fpga_box((uint16_t)(ba.x0 + b->x),
                                                   (uint16_t)(ba.y0 + b->y), b->w, b->h),
                                          &ba.tx[b->off], len);
        if (!submit(&c)) return;
        ba.f_cmds++;
        ba.f_bytes += len;
        ba.next_band++;
    }
}

/* ---- planning a frame ------------------------------------------------------
 * lo[y], hi[y]: the first and last packed byte of row y that changed (-1:
 * the row didn't). Boxes are made top to bottom, greedily: start at a
 * changed row, keep adding rows while the box (as wide as the union of
 * its rows' changes) still fits in maxp, and end it on its last changed
 * row. The payload is expanded into tx[] right away. */
static void plan_frame(const uint8_t *f, const int8_t *lo, const int8_t *hi)
{
    uint16_t off = 0;
    unsigned y = 0;

    ba.nbands = 0;
    while (y < BA_H) {
        if (lo[y] < 0) { y++; continue; }

        int blo = lo[y], bhi = hi[y], clo = blo, chi = bhi;
        unsigned h = 1, gap = 0;
        for (unsigned r = y + 1; r < BA_H; r++) {
            int nlo = clo, nhi = chi;
            if (lo[r] < 0) {
                if (++gap > BA_MAX_GAP) break;
            } else {
                gap = 0;
                if (lo[r] < nlo) nlo = lo[r];
                if (hi[r] > nhi) nhi = hi[r];
            }
            if ((r - y + 1u) * (unsigned)(nhi - nlo + 1) * 8u > ba.maxp) break;
            clo = nlo;
            chi = nhi;
            if (lo[r] >= 0) { h = r - y + 1u; blo = clo; bhi = chi; }
        }

        unsigned nb = (unsigned)(bhi - blo + 1);         /* packed bytes a row */
        band_t *b = &ba.bands[ba.nbands++];
        b->x   = (uint8_t)(blo * 8);
        b->w   = (uint8_t)(nb * 8u);
        b->y   = (uint8_t)y;
        b->h   = (uint8_t)h;
        b->off = off;
        for (unsigned r = 0; r < h; r++) {               /* rows are 8-aligned */
            const uint8_t *src = &f[(y + r) * BA_ROW_BYTES + (unsigned)blo];
            uint64_t *d = (uint64_t *)&ba.tx[off];
            for (unsigned i = 0; i < nb; i++) d[i] = lut[src[i]];
            off = (uint16_t)(off + nb * 8u);
        }
        y += h;
    }
}

/* ---- timing ----------------------------------------------------------------- */
static void frame_sent(void)
{
    uint32_t ms = plat_millis() - ba.f_t;
    double c = ba.f_cmds, b = ba.f_bytes / 1024.0, t = ms;

    ba.st_n++;
    if (ms > BA_FRAME_MS) ba.st_slow++;
    if (ms > ba.worst_ms) {
        ba.worst_ms    = ms;
        ba.worst_frame = ba.shown;
        ba.worst_cmds  = ba.f_cmds;
        ba.worst_bytes = ba.f_bytes;
    }
    ba.s_cc += c * c;  ba.s_bb += b * b;  ba.s_cb += c * b;
    ba.s_ct += c * t;  ba.s_bt += b * t;
}

/* No floats in the log (newlib-nano's printf may not have them). */
static void timing_log(void)
{
    if (ba.st_n == 0) return;
    LOG_INFO("%lu of %lu frames took over %u ms; slowest: frame %lu (%lu:%02lu), "
             "%u boxes, %lu bytes, %lu ms",
             (unsigned long)ba.st_slow, (unsigned long)ba.st_n, BA_FRAME_MS,
             (unsigned long)ba.worst_frame, (unsigned long)(ba.worst_frame / BA_FPS / 60u),
             (unsigned long)(ba.worst_frame / BA_FPS % 60u), ba.worst_cmds,
             (unsigned long)ba.worst_bytes, (unsigned long)ba.worst_ms);

    /* t = a * boxes + b * KB, least squares (no constant: an empty frame
     * costs nothing). Needs frames of different shapes to tell a from b. */
    double det = ba.s_cc * ba.s_bb - ba.s_cb * ba.s_cb;
    if (det <= 1e-6 * ba.s_cc * ba.s_bb) {
        LOG_INFO("cost per box / per KB: can't tell apart (all frames alike)");
        return;
    }
    double a = (ba.s_ct * ba.s_bb - ba.s_bt * ba.s_cb) / det;    /* ms per box */
    double b = (ba.s_bt * ba.s_cc - ba.s_ct * ba.s_cb) / det;    /* ms per KB  */
    LOG_INFO("about %ld us per WRITE_BOX + %ld us per KB of payload",
             (long)(a * 1000.0), (long)(b * 1000.0));
}

/* ---- the file ------------------------------------------------------------ */
static bool in_chunk(uint32_t i)
{
    return i >= ba.chunk_first && i < ba.chunk_first + ba.chunk_n;
}

/* Read BA_CHUNK frames from frame i on. */
static bool load_chunk(uint32_t i)
{
    uint32_t got = 0, t = plat_millis();
    storage_status_t st = storage_read(ba.path, i * BA_FRAME, ba.chunk, sizeof ba.chunk, &got);
    t = plat_millis() - t;
    if (st != STORAGE_OK || got < BA_FRAME) {
        LOG_ERR("%s, frame %lu: %s", ba.path, (unsigned long)i,
                st != STORAGE_OK ? storage_status_str(st) : "short read");
        return false;
    }
    ba.chunk_first = i;
    ba.chunk_n     = got / BA_FRAME;
    LOG_DBG("frames %lu..%lu from the card in %lu ms", (unsigned long)i,
            (unsigned long)(i + ba.chunk_n - 1u), (unsigned long)t);
    return true;
}

static const uint8_t *frame(uint32_t i)
{
    if (!in_chunk(i) && !load_chunk(i)) return NULL;
    return &ba.chunk[(i - ba.chunk_first) * BA_FRAME];
}

/* ---- start / stop -------------------------------------------------------- */
static void finish(const char *why)
{
    ba.phase = BA_OFF;                      /* cmd_done: no more error lines */
    if (ba.in_flight) fpga_abort_all();     /* ours, and anything queued with them */
    LOG_INFO("%s: %lu frames sent, %lu dropped, %lu loops", why, (unsigned long)ba.sent,
             (unsigned long)ba.dropped, (unsigned long)ba.loops);
    timing_log();

    uint32_t ms = plat_millis() - ba.sync_t0;
    uint64_t n  = (uint32_t)(frame_sync_count() - ba.sync_c0);
    if (ba.sent && ms >= 1000u)                     /* got as far as playing */
        LOG_INFO("FPGA frame SYNC: %lu.%lu Hz (expected %u)%s",
                 (unsigned long)(n * 1000u / ms), (unsigned long)(n * 10000u / ms % 10u),
                 (unsigned)FPGA_FRAME_HZ,
                 BA_WAIT_SYNC && ba.sync_missed_all ? ", some frames went without one" : "");
}

bool badapple_busy(void) { return ba.phase != BA_OFF; }

void badapple_stop(void)
{
    if (ba.phase == BA_OFF) { LOG_INFO("not playing"); return; }
    finish("stopped");
}

bool badapple_start(const char *path, bool loop)
{
    if (ba.phase != BA_OFF) { LOG_WARN("already playing ('badapple stop')"); return false; }
    if (game_loading()) {
        LOG_ERR("the game is using the grid: 'loading off' first");
        return false;
    }

    const fpga_info_t *in = fpga_link_info();
    if (!in->valid) { LOG_ERR("N not known yet: 'info query' first"); return false; }
    if (in->sim_w < BA_W || in->sim_h < BA_H) {
        LOG_ERR("grid %ux%u is smaller than the video (%ux%u)", in->sim_w, in->sim_h,
                BA_W, BA_H);
        return false;
    }

    uint16_t maxp = fpga_max_payload(WRITE_BOX_OPCODE);
    if (maxp < BA_W || maxp < in->sim_w) {
        LOG_ERR("N=%u: not even one row fits in a WRITE_BOX", in->max_cmd);
        return false;
    }

    size_t n = path ? strlen(path) : 0;
    if (n == 0 || n >= sizeof ba.path) { LOG_ERR("bad file name"); return false; }

    uint32_t size = 0;
    storage_status_t st = storage_size(path, &size);
    if (st != STORAGE_OK) { LOG_ERR("%s: %s", path, storage_status_str(st)); return false; }
    if (size == 0 || size % BA_FRAME) {
        LOG_ERR("%s: %lu bytes is not whole %ux%u 1 bpp frames", path,
                (unsigned long)size, BA_W, BA_H);
        return false;
    }

    memset(&ba, 0, sizeof ba);
    memcpy(ba.path, path, n + 1);
    ba.frames     = size / BA_FRAME;
    ba.loop       = loop;
    ba.grid_w     = in->sim_w;
    ba.grid_h     = in->sim_h;
    ba.x0         = (uint16_t)((in->sim_w - BA_W) / 2u);
    ba.y0         = (uint16_t)((in->sim_h - BA_H) / 2u);
    ba.maxp       = maxp < sizeof ba.tx ? maxp : (uint16_t)sizeof ba.tx;
    ba.clear_rows = (uint16_t)(ba.maxp / ba.grid_w);

    lut_init();
    memset(ba.tx, BA_BLACK, sizeof ba.tx);

    fpga_cmd_t stop = fpga_cmd_sim_run(false);  /* nothing may move under the video */
    if (!submit(&stop)) {
        LOG_ERR("couldn't queue STOP_SIM (queue full?)");
        return false;
    }

    ba.phase = BA_CLEAR;
    LOG_INFO("%s: %lu frames (%lu:%02lu), up to %u bytes per WRITE_BOX (N=%u), "
             "whole frame every %u%s", path,
             (unsigned long)ba.frames,
             (unsigned long)(ba.frames / BA_FPS / 60u), (unsigned long)(ba.frames / BA_FPS % 60u),
             ba.maxp, in->max_cmd, BA_REFRESH_EVERY, loop ? ", looping" : "");
    return true;
}

/* ---- the main loop ------------------------------------------------------- */
static void play_poll(void)
{
    /* The frame being sent: the rest of its boxes, then wait until it's
     * all out (tx[] is in use until then). */
    submit_bands();
    if (ba.next_band < ba.nbands || ba.in_flight) return;
    if (ba.timing) { ba.timing = false; frame_sent(); }

    /* Idle until the next frame: read ahead if it's not in the chunk. */
    if (ba.started && ba.shown + 1u < ba.frames && !in_chunk(ba.shown + 1u) &&
        !load_chunk(ba.shown + 1u)) {
        finish("stopped, card read failed");
        return;
    }

    uint32_t due = (plat_millis() - ba.t0) / BA_FRAME_MS;  /* should be on screen now */
    if (ba.started && due <= ba.shown) return;              /* not time yet */
    if (due >= ba.frames) {
        if (!ba.loop) { finish("done"); return; }
        /* Frame 0 is due exactly one video length after the last frame 0,
         * so a loop doesn't drift. started = false: the first frame of the
         * new round is sent whole (it's compared with nothing). */
        ba.t0     += ba.frames * BA_FRAME_MS;
        ba.started = false;
        ba.loops++;
        return;
    }

    /* Due: now wait for the FPGA's next SYNC (a new edge, not one from
     * before), at most a frame time. */
    if (ba.sync_on) {
        if (!ba.sync_waiting) {
            ba.sync_waiting = true;
            ba.sync_mark    = frame_sync_count();
            ba.sync_wait_t  = plat_millis();
            return;
        }
        bool got = frame_sync_count() != ba.sync_mark;
        if (!got && plat_millis() - ba.sync_wait_t < BA_FRAME_MS) return;
        ba.sync_waiting = false;
        if (got) {
            ba.sync_missed = 0;
        } else {
            ba.sync_missed_all++;
            if (++ba.sync_missed >= BA_SYNC_GIVE_UP) {
                ba.sync_on = false;
                LOG_WARN("no frame SYNC from the FPGA: frames on the MCU clock only");
            }
        }
        due = (plat_millis() - ba.t0) / BA_FRAME_MS;       /* the wait took time */
        if (due >= ba.frames) return;                       /* the end: next poll */
    }

    if (ba.started && due > ba.shown + 1u) ba.dropped += due - ba.shown - 1u;

    const uint8_t *f = frame(due);
    if (!f) { finish("stopped, card read failed"); return; }

    /* Whole: every row in full width (compared with nothing). */
    bool whole = !ba.started ||
                 (BA_REFRESH_EVERY > 0u && ba.sent % BA_REFRESH_EVERY == 0u);

    int8_t lo[BA_H], hi[BA_H];
    for (unsigned y = 0; y < BA_H; y++) {
        const uint8_t *row = &f[y * BA_ROW_BYTES], *old = &ba.prev[y * BA_ROW_BYTES];
        int l = -1, h = -1;
        for (unsigned i = 0; i < BA_ROW_BYTES; i++) {
            if (!whole && row[i] == old[i]) continue;
            if (l < 0) l = (int)i;
            h = (int)i;
        }
        lo[y] = (int8_t)l;
        hi[y] = (int8_t)h;
    }
    plan_frame(f, lo, hi);
    memcpy(ba.prev, f, BA_FRAME);

    ba.started   = true;
    ba.shown     = due;
    ba.sent++;
    ba.next_band = 0;
    ba.f_t       = plat_millis();
    ba.f_cmds    = 0;
    ba.f_bytes   = 0;
    ba.timing    = ba.nbands > 0;
    submit_bands();
}

void badapple_poll(void)
{
    if (ba.phase == BA_OFF) return;
    if (ba.failed) { finish("stopped, a command failed"); return; }

    if (ba.phase == BA_CLEAR) {
        submit_clear();
        if (ba.clear_row < ba.grid_h || ba.in_flight) return;
        ba.phase   = BA_PLAY;               /* tx[] is free from here on */
        ba.t0      = plat_millis();
        ba.sync_on = BA_WAIT_SYNC;
        ba.sync_c0 = frame_sync_count();
        ba.sync_t0 = ba.t0;
        return;
    }

    play_poll();
    /* Start what was just queued now, not a whole main-loop pass later.
     * (Allowed here: this isn't a done callback.) */
    if (ba.phase == BA_PLAY) fpga_poll();
}