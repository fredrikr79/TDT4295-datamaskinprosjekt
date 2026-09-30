/* ======================================================================
 * test_host.c - example "game" on top of fpga.c, for the host build.
 *
 * Build it as a second host executable: the same sources as the
 * emulator, with test_host.c in place of host_main.c.
 *
 *   ./test_host [--errors=N] [picture.bmp]    (default: test565.bmp)
 *
 * --errors=N makes N% of transfers fail, to watch it resync.
 *
 * The picture must be an RGB565 BMP, at most the FPGA's screen size.
 * Make one from any image with ImageMagick:
 *
 *   convert test.bmp -alpha off -resize 640x360! -define bmp:subtype=RGB565 test565.bmp
 *   (ImageMagick 7: "magick" instead of "convert")
 *
 * Everything below main() and the "picture file" section is written the
 * way MCU code would use fpga.c. It is one state machine, demo_poll():
 *
 *   PROBE   echo 100 bytes until they come back intact (FPGA is there)
 *   INFO    ask for W, H, N, session id
 *   LOAD    stream the picture into the SIM layer, a few chunks in flight
 *   RUN     bounce a cube around the HUD layer, one frame per FPGA SYNC
 *           pulse; counts the frames it was too slow for
 *
 * Any failed command -> abort everything, back to PROBE. If INFO then
 * reports the same session, the FPGA kept its memory and LOAD is skipped.
 *
 * The patterns worth copying:
 *   - check fpga_has_space() BEFORE filling a buffer (don't read data you
 *     can't send yet)
 *   - a buffer is busy from fpga_submit() until its done callback; ctx
 *     tells the callback which buffer
 *   - FPGA_EFULL: keep the command, submit the same one again later
 *   - split writes with fpga_max_payload()
 * ====================================================================== */
#define LOG_TAG "demo"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpga.h"
#include "transport.h"
#include "platform.h"
#include "log.h"
#include "host_hooks.h"
#include "frame_sync.h"
#include "display_host.h"       /* only for display_init() in main() */

/* ---- things the MCU side has to agree with the FPGA on ---------------- */
#define PX_BYTES        2u          /* RGB565, MSB first on the wire     */
#define HUD_CLEAR       0xF81Fu     /* HUD colour key: see-through       */
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

/* ---- tuning ------------------------------------------------------------ */
#define PROBE_LEN       100u
#define PROBE_RETRY_MS  200u
#define LOAD_BUFS       4u          /* picture chunks in flight at once  */
#define LOAD_BUF_BYTES  1024u       /* >= the largest payload we send    */
#define CUBE            16          /* cube size in pixels               */
#define CUBE_SPEED      3           /* pixels per frame, each axis       */
#define CUBE_COLOUR     RGB565(255, 200, 0)
#define STATS_FRAMES    150u        /* report on-time/behind this often  */

/* The HUD area one frame rewrites: the new cube plus where it was
 * confirmed and where a failed frame may have put it -- up to two moves. */
#define REGION_MAX      (CUBE + 2 * CUBE_SPEED)
#define MAX_FRAME_CMDS  REGION_MAX  /* worst case: one command per row   */

static void put_px(uint8_t *dst, uint16_t v)     /* MSB first */
{
    dst[0] = (uint8_t)(v >> 8);
    dst[1] = (uint8_t)v;
}

static uint16_t min16(uint32_t a, uint32_t b) { return (uint16_t)(a < b ? a : b); }

/* ======================================================================
 * Picture file -- the host stand-in for reading a scene from the SD card
 *
 * A 16-bit BMP with BI_BITFIELDS masks F800/07E0/001F. Rows are stored
 * bottom-up (unless the height is negative) and pixels little-endian, so
 * pic_read() picks the right row and swaps to MSB first.
 * ====================================================================== */
static struct {
    FILE    *f;
    uint32_t data_off;          /* file offset of the pixel data  */
    uint32_t stride;            /* bytes per stored row (4-aligned) */
    bool     bottom_up;
    uint16_t w, h;
} pic;

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static bool pic_open(const char *path)
{
    uint8_t h[70];
    pic.f = fopen(path, "rb");
    if (!pic.f) { LOG_ERR("can't open %s", path); return false; }
    if (fread(h, 1, sizeof h, pic.f) != sizeof h || h[0] != 'B' || h[1] != 'M') {
        LOG_ERR("%s: not a BMP", path);
        return false;
    }

    int32_t w = (int32_t)le32(&h[18]), hh = (int32_t)le32(&h[22]);
    uint16_t bpp  = le16(&h[28]);
    uint32_t comp = le32(&h[30]);
    if (bpp != 16 || comp != 3 || le32(&h[54]) != 0xF800u ||
        le32(&h[58]) != 0x07E0u || le32(&h[62]) != 0x001Fu) {
        LOG_ERR("%s: not RGB565 (convert it: see the top of test_host.c)", path);
        return false;
    }

    pic.data_off  = le32(&h[10]);
    pic.w         = (uint16_t)w;
    pic.h         = (uint16_t)(hh < 0 ? -hh : hh);
    pic.bottom_up = hh > 0;
    pic.stride    = ((uint32_t)pic.w * 2u + 3u) & ~3u;
    if (pic.w == 0 || pic.w > 1024 || pic.h == 0) {
        LOG_ERR("%s: %dx%d not supported", path, (int)w, (int)hh);
        return false;
    }
    LOG_INFO("picture %s: %ux%u RGB565", path, pic.w, pic.h);
    return true;
}

/* w x h pixels starting at (x, y), row by row, MSB first, into dst. */
static bool pic_read(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *dst)
{
    uint8_t row[2u * 1024u];
    for (uint16_t r = 0; r < h; r++) {
        uint32_t fy = pic.bottom_up ? (uint32_t)(pic.h - 1u - (y + r)) : (uint32_t)(y + r);
        if (fseek(pic.f, (long)(pic.data_off + fy * pic.stride + x * 2u), SEEK_SET) != 0 ||
            fread(row, 2, w, pic.f) != w)
            return false;
        for (uint16_t i = 0; i < w; i++, dst += 2)
            put_px(dst, le16(&row[2u * i]));
    }
    return true;
}

/* ======================================================================
 * The demo state machine
 * ====================================================================== */
typedef enum { S_PROBE, S_PROBING, S_INFO, S_INFO_WAIT, S_LOAD, S_RUN } state_t;
static const char *const state_names[] =
    { "probe", "probing", "info", "info-wait", "load", "run" };

static state_t  state = S_PROBE;
static uint32_t retry_at;           /* S_PROBE: don't probe before this */
static bool     failed;             /* a callback saw an error          */
static bool     have_session;
static uint16_t session;            /* the FPGA session we drew into    */
static bool     bg_loaded;          /* LOAD finished in that session    */

static void run_enter(void);

static void go(state_t s)
{
    if (s != state) LOG_INFO("%s -> %s", state_names[state], state_names[s]);
    state = s;
    if (s == S_RUN) run_enter();
}

/* ---- PROBE / INFO ------------------------------------------------------ */
static uint8_t probe_tx[PROBE_LEN], probe_rx[PROBE_LEN];

static void probe_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    (void)cmd;
    if (err == FPGA_ERR_NONE) {
        go(S_INFO);
    } else {
        LOG_DBG("probe: %s", fpga_err_str(err));
        retry_at = plat_millis() + PROBE_RETRY_MS;
        go(S_PROBE);
    }
}

static void info_done(const fpga_cmd_t *cmd, fpga_err_t err);

/* ---- LOAD: picture -> SIM layer ----------------------------------------
 * The picture is sent in chunks as big as fpga_max_payload() allows: whole
 * rows per box if a row fits, otherwise pieces of one row. LOAD_BUFS
 * buffers rotate; each is busy from submit until its done callback.
 *
 * The queue runs in order, so when a chunk fails, every chunk before it
 * arrived. After the resync, LOAD carries on from the first failed chunk
 * instead of starting over. */
typedef struct {
    bool    busy;
    uint8_t data[LOAD_BUF_BYTES];
} load_buf_t;

static load_buf_t lbuf[LOAD_BUFS];
static struct {
    uint16_t x, y;              /* next pixel to send               */
    uint16_t in_flight;
    uint32_t started_ms;
    bool     have_resume;       /* a chunk failed: resume here      */
    uint16_t resume_x, resume_y;
} load;

static void load_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    load_buf_t *b = cmd->ctx;    /* which buffer this command used */
    b->busy = false;             /* ...and it is ours again         */
    load.in_flight--;
    if (err == FPGA_ERR_NONE) return;

    /* The box header says where this chunk was going. Keep the first
     * one that failed; later ones come back CANCELLED. */
    uint16_t x = cmd->hdr.x, y = cmd->hdr.y;
    if (!load.have_resume || y < load.resume_y ||
        (y == load.resume_y && x < load.resume_x)) {
        load.have_resume = true;
        load.resume_x    = x;
        load.resume_y    = y;
    }
    failed = true;
}

static void load_start(void)
{
    memset(&load, 0, sizeof load);
    load.started_ms = plat_millis();
    bg_loaded = false;
    go(S_LOAD);
}

/* Same FPGA session, LOAD was interrupted: pick up at the failed chunk. */
static void load_resume(void)
{
    if (load.have_resume) {
        load.x = load.resume_x;
        load.y = load.resume_y;
        load.have_resume = false;
    }
    LOG_INFO("resuming background at (%u,%u)", load.x, load.y);
    go(S_LOAD);
}

static void load_poll(void)
{
    const uint16_t max_px = min16(fpga_max_payload(SEND_SIM_BOX_OPCODE),
                                  LOAD_BUF_BYTES) / PX_BYTES;

    while (load.y < pic.h) {
        /* Space first, then a free buffer, THEN read the data. */
        if (!fpga_has_space()) return;
        load_buf_t *b = NULL;
        for (unsigned i = 0; i < LOAD_BUFS; i++)
            if (!lbuf[i].busy) { b = &lbuf[i]; break; }
        if (!b) return;          /* all in flight: callbacks will free one */

        uint16_t w, h;
        if (max_px >= pic.w) {   /* whole rows fit */
            w = pic.w;
            h = min16(max_px / pic.w, pic.h - load.y);
        } else {                 /* a piece of one row */
            w = min16(max_px, pic.w - load.x);
            h = 1;
        }

        if (!pic_read(load.x, load.y, w, h, b->data)) {
            LOG_ERR("picture read failed at (%u,%u)", load.x, load.y);
            exit(1);
        }

        fpga_box_t box = { load.x, load.y, w, h };
        fpga_cmd_t c = fpga_cmd_send_sim_box(box, b->data, (uint16_t)(w * h * PX_BYTES));
        c.done = load_done;
        c.ctx  = b;
        if (fpga_submit(&c) != FPGA_OK) { failed = true; return; }  /* can't be EFULL: checked */
        b->busy = true;
        load.in_flight++;

        load.x = (uint16_t)(load.x + w);
        if (load.x >= pic.w) { load.x = 0; load.y = (uint16_t)(load.y + h); }
    }

    if (load.in_flight == 0) {
        LOG_INFO("background loaded in %lu ms",
                 (unsigned long)(plat_millis() - load.started_ms));
        bg_loaded = true;
        go(S_RUN);
    }
}

/* ---- RUN: a cube bouncing around the HUD --------------------------------
 * One frame rewrites the smallest rectangle covering the old and the new
 * cube: cube colour where the cube is now, HUD_CLEAR elsewhere. That both
 * erases and draws in one go, so there's no frame where the cube is gone.
 * If the rectangle is bigger than one command allows, it is split into
 * row bands; the frame buffer is busy until every band's callback ran.
 *
 * Only a frame whose every band came back OK counts as drawn. If a frame
 * failed, nobody knows whether it reached the FPGA -- so it is "stale",
 * and the next frame's rectangle covers that spot too. */
static struct {
    int16_t    x, y, dx, dy;    /* where the cube is going           */
    int16_t    ox, oy;          /* confirmed drawn here on the FPGA  */
    bool       drawn;
    int16_t    fx, fy;          /* the frame in flight draws it here */
    bool       frame_active;    /* a frame is submitted / in flight  */
    bool       stale;           /* a frame to fx,fy failed: unknown  */
    uint32_t   sync_seen;       /* SYNC count we last started a frame at */
    uint32_t   frames, behind;  /* since the last report              */

    uint8_t    buf[REGION_MAX * REGION_MAX * PX_BYTES];
    fpga_cmd_t cmds[MAX_FRAME_CMDS];
    uint8_t    n_cmds, next_cmd;  /* built / submitted so far        */
    uint8_t    pending;           /* submitted, done not called yet  */
} cube = { .x = 40, .y = 40, .dx = CUBE_SPEED, .dy = CUBE_SPEED - 1 };

/* Frames that went by while probing or loading don't count as missed. */
static void run_enter(void)
{
    cube.sync_seen = frame_sync_count();
    cube.frames = cube.behind = 0;
}

static void cube_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    (void)cmd;
    cube.pending--;
    if (err != FPGA_ERR_NONE) failed = true;
}

static void cube_build_frame(void)
{
    const fpga_info_t *in = fpga_link_info();

    /* move, bouncing off the screen edges */
    cube.x = (int16_t)(cube.x + cube.dx);
    cube.y = (int16_t)(cube.y + cube.dy);
    if (cube.x < 0 || cube.x + CUBE > in->width)  { cube.dx = (int16_t)-cube.dx; cube.x = (int16_t)(cube.x + 2 * cube.dx); }
    if (cube.y < 0 || cube.y + CUBE > in->height) { cube.dy = (int16_t)-cube.dy; cube.y = (int16_t)(cube.y + 2 * cube.dy); }

    /* region = new cube, grown to cover wherever an old one may be */
    int16_t x0 = cube.x, y0 = cube.y, x1 = (int16_t)(cube.x + CUBE), y1 = (int16_t)(cube.y + CUBE);
    const struct { bool on; int16_t x, y; } old[2] = {
        { cube.drawn, cube.ox, cube.oy },       /* confirmed           */
        { cube.stale, cube.fx, cube.fy },       /* failed, maybe there */
    };
    for (unsigned i = 0; i < 2; i++) {
        if (!old[i].on) continue;
        if (old[i].x < x0) x0 = old[i].x;
        if (old[i].y < y0) y0 = old[i].y;
        if (old[i].x + CUBE > x1) x1 = (int16_t)(old[i].x + CUBE);
        if (old[i].y + CUBE > y1) y1 = (int16_t)(old[i].y + CUBE);
    }
    uint16_t w = (uint16_t)(x1 - x0), h = (uint16_t)(y1 - y0);

    for (uint16_t r = 0; r < h; r++)
        for (uint16_t c = 0; c < w; c++) {
            int16_t px = (int16_t)(x0 + c), py = (int16_t)(y0 + r);
            bool in_cube = px >= cube.x && px < cube.x + CUBE &&
                           py >= cube.y && py < cube.y + CUBE;
            put_px(&cube.buf[(r * w + c) * PX_BYTES], in_cube ? CUBE_COLOUR : HUD_CLEAR);
        }

    /* split into bands of whole rows that fit one command */
    uint16_t rows = min16(fpga_max_payload(SEND_HUD_BOX_OPCODE) / (w * PX_BYTES), h);
    if (rows == 0) rows = 1;     /* N smaller than one row: submit says ETOOBIG */
    cube.n_cmds = cube.next_cmd = 0;
    for (uint16_t r = 0; r < h; r = (uint16_t)(r + rows)) {
        uint16_t bh = min16(rows, h - r);
        fpga_box_t box = { (uint16_t)x0, (uint16_t)(y0 + r), w, bh };
        fpga_cmd_t c = fpga_cmd_send_hud_box(box, &cube.buf[r * w * PX_BYTES],
                                             (uint16_t)(w * bh * PX_BYTES));
        c.done = cube_done;
        cube.cmds[cube.n_cmds++] = c;
    }

    cube.fx = cube.x;            /* drawn only once every band is back */
    cube.fy = cube.y;
    cube.frame_active = true;
}

static void run_poll(void)
{
    /* 1. finish submitting this frame. EFULL: same command next poll. */
    while (cube.next_cmd < cube.n_cmds) {
        fpga_status_t st = fpga_submit(&cube.cmds[cube.next_cmd]);
        if (st == FPGA_EFULL) return;
        if (st != FPGA_OK) { failed = true; return; }
        cube.pending++;
        cube.next_cmd++;
    }

    /* 2. the frame buffer is busy until every band is done */
    if (cube.pending) return;
    if (cube.frame_active) {     /* all bands came back OK (errors resync) */
        cube.ox = cube.fx;
        cube.oy = cube.fy;
        cube.drawn = true;
        cube.stale = false;      /* this frame covered the stale spot too */
        cube.frame_active = false;
    }

    /* 3. next frame when the FPGA starts one. More than one SYNC since
     *    last time means we were still busy when a frame went by: behind.
     *    This demo just skips the missed frames; a game could instead move
     *    things n steps so the speed stays the same. */
    uint32_t n = frame_sync_take(&cube.sync_seen);
    if (n == 0) return;
    cube.frames++;
    cube.behind += n - 1;
    if (cube.frames + cube.behind >= STATS_FRAMES) {
        LOG_INFO("frames: %lu on time, %lu missed",
                 (unsigned long)cube.frames, (unsigned long)cube.behind);
        cube.frames = cube.behind = 0;
    }
    cube_build_frame();
}

/* ---- INFO callback: decides between LOAD and RUN ----------------------- */
static void info_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    (void)cmd;
    if (err != FPGA_ERR_NONE) { failed = true; return; }

    const fpga_info_t *in = fpga_link_info();
    LOG_INFO("fpga: %ux%u, N=%u, session 0x%04X",
             in->width, in->height, in->max_cmd, in->session);
    if (pic.w > in->width || pic.h > in->height) {
        LOG_ERR("picture %ux%u is bigger than the FPGA screen", pic.w, pic.h);
        exit(1);
    }

    if (have_session && in->session == session) {
        /* same FPGA memory: what was drawn is still there */
        if (bg_loaded) go(S_RUN);
        else           load_resume();
    } else {
        have_session = true;
        session      = in->session;
        cube.drawn   = false;    /* new FPGA memory: nothing is drawn */
        cube.stale   = false;
        load_start();
    }
}

/* ---- the state machine -------------------------------------------------- */
static void resync(void)
{
    LOG_WARN("command failed in '%s' -> resync", state_names[state]);
    fpga_abort_all();            /* callbacks run now and free the buffers */
    failed        = false;
    if (cube.frame_active) {     /* may or may not have reached the FPGA */
        cube.stale        = true;
        cube.frame_active = false;
    }
    cube.n_cmds   = cube.next_cmd = 0;
    retry_at      = plat_millis();
    go(S_PROBE);
}

static void demo_poll(void)
{
    if (failed) { resync(); return; }

    switch (state) {
    case S_PROBE: {
        if ((int32_t)(plat_millis() - retry_at) < 0) break;
        for (unsigned i = 0; i < PROBE_LEN; i++)
            probe_tx[i] = (uint8_t)(i * 37u + retry_at);   /* new pattern each try */
        fpga_cmd_t c = fpga_cmd_echo(probe_tx, probe_rx, PROBE_LEN);
        c.done = probe_done;
        if (fpga_submit(&c) == FPGA_OK) go(S_PROBING);
        break;
    }
    case S_INFO: {
        fpga_cmd_t c = fpga_cmd_info();
        c.done = info_done;
        if (fpga_submit(&c) == FPGA_OK) go(S_INFO_WAIT);
        break;
    }
    case S_PROBING:
    case S_INFO_WAIT:
        break;                   /* the done callback moves us on */
    case S_LOAD:
        load_poll();
        break;
    case S_RUN:
        run_poll();
        break;
    }
}

/* ======================================================================
 * main -- the host "board": window, fake link, then the main loop
 * ====================================================================== */
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    const char *path   = "test565.bmp";
    unsigned    errors = 0;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--errors=", 9) == 0)
            errors = (unsigned)strtoul(argv[i] + 9, NULL, 10);
        else
            path = argv[i];
    }
    if (!pic_open(path)) return 1;

    host_fpga_set_clock(20.0, 8);
    host_fpga_set_error_rate(errors);
    if (display_init("hexmcu test_host", 2))
        atexit(display_shutdown);
    transport_init();

    for (;;) {                   /* same shape as myMain() */
        transport_poll();        /* fake link + window; closing it exits */
        fpga_poll();             /* runs the queue, calls done callbacks */
        demo_poll();
    }
}