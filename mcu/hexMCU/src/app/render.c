/* render.c - drawing on the FPGA's HUD layer (see render.h).
 *
 * A cube frame is a short list of jobs, each a rectangle that is either
 * filled from the cube's buffer or cleared to HUD_CLEAR:
 *
 *   near:  ONE job covering the new cube, the old one and any unknown
 *          areas -- cube colour where the cube is, clear elsewhere. Erasing
 *          and drawing in one go, so there's no frame where it's missing.
 *   far:   clear the old cube, clear each unknown area, then draw the new
 *          cube (last, so it wins where they overlap).
 *
 * Jobs are cut into commands as they are sent: whole rows per command if
 * a row fits fpga_max_payload(), else pieces of a row. The queue runs in
 * order, so the jobs land in the order they were built.
 *
 * Only a frame whose every command came back OK counts as drawn. If one
 * failed, everything the frame touched is "unknown" (stale) and the next
 * frame clears it too. */

#define LOG_TAG "render"

#include <string.h>

#include "render.h"
#include "log.h"

#define CLEAR_PX 512u               /* HUD_CLEAR pixels, shared by all clears */

static uint8_t clear_px[CLEAR_PX * PX_BYTES];
static bool    clear_ready;

static void put_px(uint8_t *dst, uint16_t v)     /* MSB first */
{
    dst[0] = (uint8_t)(v >> 8);
    dst[1] = (uint8_t)v;
}

static uint16_t min16(uint32_t a, uint32_t b) { return (uint16_t)(a < b ? a : b); }

/* ---- rectangles ------------------------------------------------------------ */
static render_rect_t rect_union(render_rect_t a, render_rect_t b)
{
    int32_t x0 = a.x < b.x ? a.x : b.x, y0 = a.y < b.y ? a.y : b.y;
    int32_t x1 = (a.x + a.w > b.x + b.w) ? a.x + a.w : b.x + b.w;
    int32_t y1 = (a.y + a.h > b.y + b.h) ? a.y + a.h : b.y + b.h;
    return (render_rect_t){ (int16_t)x0, (int16_t)y0, (uint16_t)(x1 - x0), (uint16_t)(y1 - y0) };
}

static bool inside(render_rect_t r, int32_t x, int32_t y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static void add_stale(render_cube_t *c, render_rect_t r)
{
    if (c->n_stale < RENDER_STALE_MAX) c->stale[c->n_stale++] = r;
    else c->stale[RENDER_STALE_MAX - 1] = rect_union(c->stale[RENDER_STALE_MAX - 1], r);
}

/* ---- building a frame ----------------------------------------------------- */
static void add_job(render_cube_t *c, render_rect_t r, bool clear)
{
    c->jobs[c->n_jobs++] = (render_job_t){ r, clear };
}

/* buf = the rectangle r: cube colour inside the cube at (x, y), clear elsewhere */
static void fill(render_cube_t *c, render_rect_t r, int16_t x, int16_t y)
{
    render_rect_t cube = { x, y, c->size, c->size };
    uint8_t *p = c->buf;
    for (uint16_t row = 0; row < r.h; row++)
        for (uint16_t col = 0; col < r.w; col++, p += PX_BYTES)
            put_px(p, inside(cube, r.x + col, r.y + row) ? c->colour : HUD_CLEAR);
}

bool render_cube(render_cube_t *c, int16_t x, int16_t y)
{
    if (c->active) return false;

    if (!clear_ready) {
        for (unsigned i = 0; i < CLEAR_PX; i++) put_px(&clear_px[i * PX_BYTES], HUD_CLEAR);
        clear_ready = true;
    }
    if (c->size == 0 || c->size > RENDER_CUBE_MAX) c->size = RENDER_CUBE_MAX;

    /* stay on the screen: out-of-bounds commands are our problem, not the FPGA's */
    const fpga_info_t *scr = fpga_link_info();
    int32_t max_x = (int32_t)scr->width - c->size, max_y = (int32_t)scr->height - c->size;
    if (x > max_x) x = (int16_t)max_x;
    if (y > max_y) y = (int16_t)max_y;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    if (c->drawn && c->ox == x && c->oy == y && c->n_stale == 0)
        return true;                            /* already there */

    render_rect_t cube = { x, y, c->size, c->size };
    render_rect_t all  = cube;
    if (c->drawn) all = rect_union(all, (render_rect_t){ c->ox, c->oy, c->size, c->size });
    for (unsigned i = 0; i < c->n_stale; i++) all = rect_union(all, c->stale[i]);

    c->n_jobs = 0;
    if ((uint32_t)all.w * all.h <= RENDER_REGION * RENDER_REGION) {
        fill(c, all, x, y);                     /* near: one rectangle */
        add_job(c, all, false);
    } else {                                    /* far: one by one */
        if (c->drawn) add_job(c, (render_rect_t){ c->ox, c->oy, c->size, c->size }, true);
        for (unsigned i = 0; i < c->n_stale; i++) add_job(c, c->stale[i], true);
        fill(c, cube, x, y);
        add_job(c, cube, false);
    }

    c->fx = x;
    c->fy = y;
    c->job = 0;
    c->row = c->col = 0;
    c->active = true;
    return true;
}

/* ---- sending -------------------------------------------------------------- */
static void cube_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    render_cube_t *c = cmd->ctx;
    if (c->pending) c->pending--;
    if (err != FPGA_ERR_NONE) c->failed = true;
}

/* The next command of the current job, without moving on. */
static fpga_cmd_t next_cmd(render_cube_t *c, uint16_t *rows, uint16_t *cols)
{
    const render_job_t *j = &c->jobs[c->job];
    uint32_t max_px = fpga_max_payload(SEND_HUD_BOX_OPCODE) / PX_BYTES;
    if (j->clear && max_px > CLEAR_PX) max_px = CLEAR_PX;
    if (max_px == 0) max_px = 1;                /* submit will say it's too big */

    uint16_t x, y;
    uint8_t *data;
    if (max_px >= j->r.w) {                     /* whole rows */
        *rows = min16(max_px / j->r.w, j->r.h - c->row);
        *cols = j->r.w;
        x = (uint16_t)j->r.x;
        y = (uint16_t)(j->r.y + c->row);
        data = j->clear ? clear_px : &c->buf[(uint32_t)c->row * j->r.w * PX_BYTES];
    } else {                                    /* a piece of one row */
        *rows = 1;
        *cols = min16(max_px, j->r.w - c->col);
        x = (uint16_t)(j->r.x + c->col);
        y = (uint16_t)(j->r.y + c->row);
        data = j->clear ? clear_px
                        : &c->buf[((uint32_t)c->row * j->r.w + c->col) * PX_BYTES];
    }

    fpga_cmd_t cmd = fpga_cmd_send_hud_box(fpga_box(x, y, *cols, *rows), data,
                                           (uint16_t)(*cols * *rows * PX_BYTES));
    cmd.done = cube_done;
    cmd.ctx  = c;
    return cmd;
}

static void advance(render_cube_t *c, uint16_t rows, uint16_t cols)
{
    const render_job_t *j = &c->jobs[c->job];
    if (cols < j->r.w) {                        /* pieces of a row */
        c->col = (uint16_t)(c->col + cols);
        if (c->col < j->r.w) return;
        c->col = 0;
    }
    c->row = (uint16_t)(c->row + rows);
    if (c->row < j->r.h) return;
    c->row = 0;
    c->job++;
}

bool render_cube_poll(render_cube_t *c)
{
    if (c->failed) return false;
    if (!c->active) return true;

    while (c->job < c->n_jobs) {
        uint16_t rows, cols;
        fpga_cmd_t cmd = next_cmd(c, &rows, &cols);
        fpga_status_t st = fpga_submit(&cmd);
        if (st == FPGA_EFULL) return true;      /* same command next poll */
        if (st != FPGA_OK) { c->failed = true; return false; }
        c->pending++;
        advance(c, rows, cols);
    }

    if (c->pending) return true;                /* buf is busy until all are back */
    c->drawn   = true;                          /* every command came back OK */
    c->ox      = c->fx;
    c->oy      = c->fy;
    c->n_stale = 0;                             /* the frame covered those too */
    c->active  = false;
    return true;
}

bool render_cube_busy(const render_cube_t *c)
{
    return c->active;
}

/* ---- errors --------------------------------------------------------------- */
void render_cube_lost(render_cube_t *c)
{
    if (c->active) {
        /* Some of it may have landed: everything it touched is unknown,
         * the old cube included (it was one of the jobs). */
        for (unsigned i = 0; i < c->n_jobs; i++) add_stale(c, c->jobs[i].r);
        c->drawn  = false;
        c->active = false;
    }
    c->pending = 0;
    c->failed  = false;
}

/* New FPGA session. Its memory was reset at some point -- but maybe before
 * our last frames, which then landed in the new memory. So nothing is
 * known: the confirmed cube becomes an unknown area too, and the next
 * frame clears everywhere the cube may be. */
void render_cube_forget(render_cube_t *c)
{
    render_cube_lost(c);
    if (c->drawn) add_stale(c, (render_rect_t){ c->ox, c->oy, c->size, c->size });
    c->drawn = false;
}