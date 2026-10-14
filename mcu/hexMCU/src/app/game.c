#define LOG_TAG "game"

#include <stdint.h>
#include <stdbool.h>

#include "fpga.h"
#include "platform.h"
#include "log.h"
#include "frame_sync.h"
#include "game.h"
#include "usb_input.h"

/* things the MCU side has to agree with the FPGA on */
#define PX_BYTES        2u          /* RGB565, MSB first on the wire     */
#define HUD_CLEAR       0xF81Fu     /* HUD colour key: see-through       */
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

/* tuning */
#define PROBE_LEN       100u
#define PROBE_RETRY_MS  200u
#define FIRST_ROOM      0u

/* ======================================================================
 * The state machine
 *
 *   PROBE -> INFO -> LOAD -> RUN
 *                     ^       |
 *                     |       v  (door)
 *                     +---- SAVE
 *
 * Any failed command -> resync -> PROBE -> INFO. If the FPGA still has
 * the same session (its memory survived), we go back to the state that
 * was interrupted. If not, everything on it is gone and the current
 * room is loaded again.
 * ====================================================================== */
typedef enum { S_PROBE, S_PROBING, S_INFO, S_INFO_WAIT, S_SAVE, S_LOAD, S_RUN } state_t;
static const char *const state_names[] =
    { "probe", "probing", "info", "info-wait", "save", "load", "run" };

static state_t  state = S_PROBE;
static state_t  resume_to = S_LOAD; /* where a resync goes back to (same session) */
static uint32_t retry_at;           /* S_PROBE: don't probe before this */
static bool     failed;             /* a callback saw an error          */
static bool     have_session;
static uint16_t session;            /* the FPGA session we drew into    */

static uint16_t room = FIRST_ROOM;  /* the room on the FPGA / being loaded */
static uint16_t next_room;          /* where the door leads: loaded after SAVE */

static void run_enter(void);

static void go(state_t s)
{
    if (s != state) LOG_INFO("%s -> %s", state_names[state], state_names[s]);
    state = s;
    if (s == S_RUN) run_enter();
}

/* PROBE / INFO */
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

/* SAVE: current room -> storage
 * Runs when we leave a room, before the next one is loaded.
 * Done callbacks set `failed` on any error; that triggers a resync. */
static void save_start(void)
{
    LOG_INFO("saving room %u", room);
    /* TODO: reset save progress */
    go(S_SAVE);
}

/* Same FPGA session, SAVE was interrupted: carry on where it stopped. */
static void save_resume(void)
{
    /* TODO: continue from the first failed command */
    go(S_SAVE);
}

/* Returns true once everything is saved and no commands are in flight. */
static bool save_poll(void)
{
    /* TODO */
    return true;
}

/* LOAD: scene for `room` -> FPGA
 * Done callbacks set `failed` on any error; that triggers a resync. */
static void load_start(void)
{
    LOG_INFO("loading room %u", room);
    /* TODO: reset load progress */
    go(S_LOAD);
}

/* Same FPGA session, LOAD was interrupted: carry on where it stopped. */
static void load_resume(void)
{
    /* TODO: continue from the first failed command */
    go(S_LOAD);
}

/* Returns true once the scene is loaded and no commands are in flight. */
static bool load_poll(void)
{
    /* TODO */
    return true;
}

/* RUN: the game itself  */
static struct {
    uint32_t sync_seen;         /* SYNC count we last started a frame at */
} run;

int x, y;

/* Frames that went by while probing or loading don't count as missed. */
static void run_enter(void)
{
    run.sync_seen = frame_sync_count();
    /* TODO: set up per-room game state */
    const fpga_info_t *in = fpga_link_info();
    x = in->width / 2;
    y = in->height / 2;

}

/* One game frame. n = SYNCs since the last one (n > 1: we fell behind). */
static void run_frame(uint32_t n)
{
    (void)n;
    /* TODO: update + draw. Walking through a door: game_change_room(to). */
}

static void run_poll(void)
{
    //static uint32_t t;
    // if (plat_millis() - t >= 20) {
    //     t = plat_millis();
    //     input_t in;
    //     input_take(&in);
    //     //input_log(&in);
    // }

    // accumilate x y even tho we are not on the next frame.
    const fpga_info_t *screen = fpga_link_info();
    input_t in;
    input_take(&in);
    x += in->mouse_dx;
    if(x<0)
        x = 0;
    if(x>screen->width):
        x = screen->width;
    y += in->mouse_dy;

    uint32_t n = frame_sync_take(&run.sync_seen);
    if (n) run_frame(n);
    //same check for y

}

/* Leave the current room: save it, then load `to`.
 * TODO: let RUN's in-flight commands finish before saving. */
void game_change_room(uint16_t to)
{
    if (state != S_RUN) return;
    next_room = to;
    save_start();
}

/* ---- INFO callback: same session -> resume, new session -> reload ------- */
static void info_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    (void)cmd;
    if (err != FPGA_ERR_NONE) { failed = true; return; }

    const fpga_info_t *in = fpga_link_info();
    LOG_INFO("fpga: %ux%u, N=%u, session 0x%04X",
             in->width, in->height, in->max_cmd, in->session);

    if (have_session && in->session == session) {
        /* same FPGA memory: what was sent is still there */
        switch (resume_to) {
        case S_SAVE: save_resume(); break;
        case S_RUN:  go(S_RUN);     break;
        default:     load_resume(); break;
        }
    } else {
        /* new FPGA memory: send the current room again. A save cut off
         * here drops the room change; the player is still in `room`. */
        have_session = true;
        session      = in->session;
        load_start();
    }
}

/* the state machine */
static void resync(void)
{
    LOG_WARN("command failed in '%s' -> resync", state_names[state]);
    if (state == S_SAVE || state == S_LOAD || state == S_RUN)
        resume_to = state;
    fpga_abort_all();            /* callbacks run now (CANCELLED)... */
    failed   = false;            /* ...so clear this after them      */
    retry_at = plat_millis();
    go(S_PROBE);
}

void game_poll(void)
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
    case S_SAVE:
        if (save_poll()) { room = next_room; load_start(); }
        break;
    case S_LOAD:
        if (load_poll()) go(S_RUN);
        break;
    case S_RUN:
        run_poll();
        break;
    }
}