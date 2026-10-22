#define LOG_TAG "game"

#include <stdint.h>
#include <stdbool.h>

#include "fpga.h"
#include "rooms.h"
#include "platform.h"
#include "log.h"
#include "frame_sync.h"
#include "game.h"
#include "usb_input.h"
#include "render.h"                 /* also PX_BYTES, HUD_CLEAR, RGB565 */
#include "font.h"
#include "text.h"
#include "font.h"       /* test font; or font_kiwisoda.h */

/* tuning */
#define PROBE_LEN       100u
#define PROBE_RETRY_MS  200u
#define FIRST_ROOM_X    0u
#define FIRST_ROOM_Y    0u
#define CURSOR_SIZE     8
#define CURSOR_COLOUR   RGB565(255, 255, 255)
#define TEST_FONT       font_kiwisoda
#define TEST_SCALE      2           /* 8 px font -> 16 px letters */
#define TEST_TEXT       "Gaming"

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
 
/* This is the higher state machine that is responsible for error recovery and switching of scenes */
typedef enum { S_PROBE, S_PROBING, S_INFO, S_INFO_WAIT, S_SAVE, S_LOAD, S_RUN } state_t;
static const char *const state_names[] =
    { "probe", "probing", "info", "info-wait", "save", "load", "run" };

static state_t  state = S_PROBE;
static state_t  resume_to = S_LOAD; /* where a resync goes back to (same session) */
static uint32_t retry_at;           /* S_PROBE: don't probe before this */
static bool     failed;             /* a callback saw an error          */
static bool     have_session;
static uint16_t session;            /* the FPGA session we drew into    */

static int room_x = FIRST_ROOM_X;   /* the x-coordinate of the current room */
static int room_y = FIRST_ROOM_Y;   /* the y-coordinate of the current room */
static int next_room_x;             /* the x-coordinate of the next room */
static int next_room_y;             /* the y-coordinate of the next room */

static bool receiving_room_from_fpga = false;
static bool sending_room_from_fpga = false;

static uint8_t room_tiles[ROOM_X*ROOM_Y] = {0};

static void run_enter(void);
static void room_enter(void);
static void test_text_draw(void);

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
    LOG_INFO("saving room %d, %d", room_x, room_y);
    receiving_room_from_fpga = true;
    /* TODO: reset save progress */
    read_room_from_fpga(room_tiles);
    go(S_SAVE);
}

/* Same FPGA session, SAVE was interrupted: carry on where it stopped. */
static void save_resume(void)
{
    /* TODO: continue from the first failed command */
    go(S_SAVE);
}

/* Change variable to indicate that all the data from the FPGA has be received */
void save_finish(void)
{
    receiving_room_from_fpga = false;
}

/* Returns true once everything is saved and no commands are in flight. */
static bool save_poll(void)
{
    return !receiving_room_from_fpga;
}

/* LOAD: scene for `room` -> FPGA
 * Done callbacks set `failed` on any error; that triggers a resync. */
static void load_start(void)
{
    LOG_INFO("loading room %d, %d", room_x, room_y);
    sending_room_from_fpga = true;
    /* TODO: reset load progress */
    room_enter();
    read_room(room_x, room_y, room_tiles);
    send_room_to_fpga(room_tiles);
    go(S_LOAD);
}

/* Same FPGA session, LOAD was interrupted: carry on where it stopped. */
static void load_resume(void)
{
    /* TODO: continue from the first failed command */
    go(S_LOAD);
}

/* Change variable to indicate that all the data has been sendt to the FPGA */
void load_finish(void)
{
    sending_room_from_fpga = false;
}

/* Returns true once the scene is loaded and no commands are in flight. */
static bool load_poll(void)
{
    return !sending_room_from_fpga;
}

/* RUN: the game itself  */
static struct {
    uint32_t sync_seen;         /* SYNC count we last started a frame at */
    int32_t  cx, cy;            /* cursor position, follows the mouse    */
} run;

/* State machine for run, handels if we are showing menu or doing other actions. */
typedef enum { RS_SIM, RS_MENU } run_state_t;
static const char *const run_state_names[] =
    { "simulating", "menu" };

static run_state_t  run_state = RS_SIM;

static void r_go(run_state_t s)
{
    if (s != run_state) LOG_INFO("%s -> %s", run_state_names[run_state], run_state_names[s]);
    run_state = s;
}
static render_cube_t cursor = RENDER_CUBE_INIT(CURSOR_SIZE, CURSOR_COLOUR);
static text_t        test_text;

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* Runs on every way into RUN, also after a resync that kept the session,
 * so nothing here may reset the game. Frames that went by while probing
 * or loading don't count as missed. */
static void run_enter(void)
{
    run.sync_seen = frame_sync_count();
    test_text_draw();
}

/* Test: a sentence centred near the top of the screen. Drawn on every way
 * into RUN, so it comes back after a resync that may have cut it short. */
static void test_text_draw(void)
{
    const fpga_info_t *scr = fpga_link_info();
    int32_t w = font_text_width(&TEST_FONT, TEST_TEXT) * TEST_SCALE;
    int32_t x = w < scr->width ? (scr->width - w) / 2 : 0;
    text_draw(&test_text, (int16_t)x, 16, &TEST_FONT, TEST_TEXT,
              RGB565(255, 220, 0), HUD_CLEAR, TEST_SCALE);
}

/* Per-room game state: from load_start(), once per room. */
static void room_enter(void)
{
    const fpga_info_t *scr = fpga_link_info();
    run.cx = (scr->width  - CURSOR_SIZE) / 2;
    run.cy = (scr->height - CURSOR_SIZE) / 2;
}

/* One game frame. n = SYNCs since the last one (n > 1: we fell behind). */
static void run_frame(uint32_t n)
{
    (void)n;
    /* TODO: update + draw. Walking through a door: game_change_room(to). */

    /* busy: the last cursor frame isn't back yet, try again next frame */
    render_cube(&cursor, (int16_t)run.cx, (int16_t)run.cy);

    switch (run_state) {
    case RS_SIM:  /* do sim */  break;
    case RS_MENU: /* do menu */ break;
    }
}

static void run_poll(void)
{
    /* Input every poll, not just on frames: the mouse adds up in between. */
    const fpga_info_t *scr = fpga_link_info();
    input_t in;
    input_take(&in);
    /* input_log(&in); */
    run.cx = clamp32(run.cx + in.mouse_dx, 0, (int32_t)scr->width  - CURSOR_SIZE);
    run.cy = clamp32(run.cy + in.mouse_dy, 0, (int32_t)scr->height - CURSOR_SIZE);

    if (key_pressed(&in, KEY_ESCAPE))
        r_go(run_state == RS_MENU ? RS_SIM : RS_MENU);
        

    if (!render_cube_poll(&cursor)) { failed = true; return; }
    if (!text_poll(&test_text))     { failed = true; return; }
    uint32_t n = frame_sync_take(&run.sync_seen);
    if (n) run_frame(n);
}

/* Leave the current room: save it, then load `to`.
 * TODO: let RUN's in-flight commands finish before saving. */
void game_change_room(int to_x, int to_y)
{
    if (state != S_RUN) return;
    next_room_x = to_x;
    next_room_y = to_y;
    save_start();
}

/* ---- INFO callback: same session -> resume, new session -> reload ------- */
static void info_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    (void)cmd;
    if (err != FPGA_ERR_NONE) { failed = true; return; }

    const fpga_info_t *in = fpga_link_info();
    LOG_INFO("fpga: %ux%u, N=%u, session 0x%08lX",
             in->width, in->height, in->max_cmd, (unsigned long)in->session);

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
        render_cube_forget(&cursor);    /* unknown what of ours it has */
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
    render_cube_lost(&cursor);   /* ...so these come after them      */
    text_lost(&test_text);
    failed   = false;
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
        if (save_poll())
        {
            save_room(room_x, room_y, room_tiles);
            room_x = next_room_x;
            room_y = next_room_y;
            load_start();
        }
        break;
    case S_LOAD:
        if (load_poll()) go(S_RUN);
        break;
    case S_RUN:
        run_poll();
        break;
    }
}
