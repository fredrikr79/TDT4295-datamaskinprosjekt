#ifndef RENDER_H
#define RENDER_H

/* Drawing on the FPGA's HUD layer. Portable: MCU and host build.
 *
 * A render_cube_t is a filled square that can be moved around. Moving it
 * erases where it was and draws where it is in one frame, and it keeps
 * track of what really reached the FPGA, so a failed or aborted frame
 * gets cleaned up by the next one.
 *
 *   static render_cube_t cursor = RENDER_CUBE_INIT(8, RGB565(255, 255, 255));
 *
 *   every poll:            if (!render_cube_poll(&cursor)) -> resync
 *   on a frame:            render_cube(&cursor, x, y);
 *   resync, after fpga_abort_all():   render_cube_lost(&cursor);
 *   new FPGA session:                 render_cube_forget(&cursor);  */

#include <stdbool.h>
#include <stdint.h>

#include "fpga.h"

/* ---- what the MCU and the FPGA agree on --------------------------------- */
#define PX_BYTES        2u          /* RGB565, MSB first on the wire     */
#define HUD_CLEAR       0xF81Fu     /* HUD colour key: see-through       */
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

/* ---- limits --------------------------------------------------------------- */
#define RENDER_CUBE_MAX   16u       /* biggest cube size, pixels          */
#define RENDER_STALE_MAX  4u        /* unknown areas remembered after errors */

/* Old and new cube are drawn as one rectangle if it is at most this many
 * pixels a side (squared); further apart, they are done separately. */
#define RENDER_REGION     (3u * RENDER_CUBE_MAX)

typedef struct {
    int16_t  x, y;
    uint16_t w, h;
} render_rect_t;

typedef struct {
    render_rect_t r;
    bool          clear;            /* true: HUD_CLEAR; false: from buf */
} render_job_t;

typedef struct {
    uint16_t size, colour;

    /* what the FPGA has */
    bool          drawn;            /* confirmed drawn at ox, oy          */
    int16_t       ox, oy;
    uint8_t       n_stale;          /* areas a failed frame may have hit  */
    render_rect_t stale[RENDER_STALE_MAX];

    /* the frame in flight */
    bool          active;
    bool          failed;           /* a command of it came back with an error */
    int16_t       fx, fy;           /* ...draws the cube here             */
    render_job_t  jobs[2 + RENDER_STALE_MAX];
    uint8_t       n_jobs, job;      /* jobs built / the one being sent    */
    uint16_t      row, col;         /* where in that job                  */
    uint16_t      pending;          /* submitted, done not called yet     */
    uint8_t       buf[RENDER_REGION * RENDER_REGION * PX_BYTES];
} render_cube_t;

#define RENDER_CUBE_INIT(size_, colour_) { .size = (size_), .colour = (colour_) }

/* Start a frame that shows the cube at x, y (clamped to the screen).
 * Returns false if the previous frame is still going (nothing done):
 * draw again next frame. Nothing is sent if nothing changed. */
bool render_cube(render_cube_t *c, int16_t x, int16_t y);

/* Sends what fits into the FPGA queue. Call it every poll. Returns false
 * when a command failed: resync, then render_cube_lost(). */
bool render_cube_poll(render_cube_t *c);

bool render_cube_busy(const render_cube_t *c);

/* After fpga_abort_all(): what the aborted frame drew is unknown. */
void render_cube_lost(render_cube_t *c);

/* New FPGA session: we can't tell which of our frames it still has (it
 * may have restarted before the last ones), so the next frame clears
 * everywhere the cube may be. */
void render_cube_forget(render_cube_t *c);

#endif