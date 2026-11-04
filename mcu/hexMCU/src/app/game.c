#define LOG_TAG "game"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "fpga.h"
#include "transport.h"
#include "rooms.h"
#include "player.h"
#include "platform.h"
#include "log.h"
#include "frame_sync.h"
#include "game.h"
#include "usb_input.h"
#include "sprite.h"
#include "assets.h"
#include "funny_man.h"
#include "demo.h"
#include "cli.h"

/* tuning */
#define PROBE_LEN       100u
#define PROBE_RETRY_MS  200u
#define READY_QUIET_MS  10u     /* PROBE: READY high and no stray edges this long */
#define STATUS_EVERY_MS 100u    /* status line redrawn at most this often    */
#define READY_LOW_SHOW_MS 100u  /* READY low this long: say so in the status */
#define FIRST_ROOM_X    0u
#define FIRST_ROOM_Y    0u

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
static uint32_t probe_tries;        /* failed probes in a row (one status line) */
static fpga_err_t probe_err;        /* why the last one failed              */
static bool     failed;             /* a callback saw an error          */
static bool     have_session;
static uint32_t session;            /* the FPGA session we drew into    */

static int room_x = FIRST_ROOM_X;   /* the x-coordinate of the current room */
static int room_y = FIRST_ROOM_Y;   /* the y-coordinate of the current room */
static int next_room_x;             /* the x-coordinate of the next room */
static int next_room_y;             /* the y-coordinate of the next room */

static player_t player;

static portal_t portals[MAX_PORTALS] = {0};

static uint8_t *box;

static void run_enter(void);
static void room_enter(void);

static void go(state_t s)
{
    /* PROBE <-> PROBING repeats every 200 ms while the FPGA doesn't answer:
     * that shows as one status line instead (probe_done). */
    bool probe_loop = (s == S_PROBE && state == S_PROBING) || (s == S_PROBING && state == S_PROBE);
    if (s != state && !probe_loop) LOG_INFO("%s -> %s", state_names[state], state_names[s]);
    state = s;
    if (s == S_RUN) run_enter();
}

/* ---- PROBE: wait for a steady READY before the handshake ----------------
 * Before the first byte of the ECHO goes out, READY must have been high,
 * with no stray edges, for READY_QUIET_MS. Otherwise an FPGA still booting
 * (or a noisy line) could catch the command halfway through and take some
 * byte in the middle as the opcode.
 *
 * Edges taken by commands (fpga_ready_used()) don't count, only stray ones.
 * Two stray edges less than READY_QUIET_MS apart is a READY storm: logged
 * once when it starts and once when it ends, and shown on the status line.
 * Only checked in PROBE (startup, and after a resync); normal commands
 * don't wait for it. */
static struct {
    bool     init;          /* the fields below are set up          */
    uint32_t spare;         /* stray edges counted so far           */
    uint32_t edge_at;       /* when a stray edge was last seen      */
    uint32_t low_at;        /* when READY was last seen low         */
    uint32_t high_at;       /* when READY was last seen high        */
    bool     storm;
    uint32_t storm_edges, storm_from;
} rdy;

static uint32_t stray_ready_count(void)
{
    return transport_ready_count() - fpga_ready_used();
}

/* true: READY has been high, with no stray edges, for READY_QUIET_MS. */
static bool ready_quiet(uint32_t now)
{
    uint32_t c = stray_ready_count();
    bool high = plat_fpga_ready_pin();
    if (!rdy.init) {                    /* watch it for READY_QUIET_MS first */
        rdy.init = true;
        rdy.spare = c;
        rdy.edge_at = rdy.low_at = rdy.high_at = now;
    }
    if (c != rdy.spare) {
        uint32_t n = c - rdy.spare;
        rdy.spare = c;
        if (n > 1 || now - rdy.edge_at < READY_QUIET_MS) {
            if (!rdy.storm) {
                rdy.storm = true;
                rdy.storm_edges = 0;
                rdy.storm_from = now;
                LOG_WARN("READY storm (edges < %u ms apart): waiting for it to stop",
                         READY_QUIET_MS);
            }
            rdy.storm_edges += n;
        }
        rdy.edge_at = now;
    }
    if (high) rdy.high_at = now;
    else      rdy.low_at  = now;

    bool quiet = high && now - rdy.edge_at >= READY_QUIET_MS && now - rdy.low_at >= READY_QUIET_MS;
    if (quiet && rdy.storm) {
        rdy.storm = false;
        LOG_INFO("READY storm over: %lu edges in %lu ms", (unsigned long)rdy.storm_edges,
                 (unsigned long)(rdy.edge_at - rdy.storm_from));
    }
    return quiet;
}

/* The one status line in front of the console prompt while in PROBE. */
static void probe_status(uint32_t now, bool now_please)
{
    static uint32_t shown_at;
    if (!now_please && now - shown_at < STATUS_EVERY_MS) return;
    shown_at = now;

    char msg[64];
    if (rdy.storm)
        snprintf(msg, sizeof msg, "READY storm: %lu edges, waiting", (unsigned long)rdy.storm_edges);
    else if (rdy.init && now - rdy.high_at >= READY_LOW_SHOW_MS)
        snprintf(msg, sizeof msg, "waiting for FPGA READY high (%lu ms)",
                 (unsigned long)(now - rdy.high_at));
    else if (probe_tries)
        snprintf(msg, sizeof msg, "probing FPGA: x%lu (%s)", (unsigned long)probe_tries,
                 fpga_err_str(probe_err));
    else
        msg[0] = '\0';
    cli_status(msg);
}

/* PROBE / INFO */
static uint8_t probe_tx[PROBE_LEN], probe_rx[PROBE_LEN];

static void probe_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    (void)cmd;
    if (err == FPGA_ERR_NONE) {
        cli_status(NULL);
        if (probe_tries) {
            LOG_INFO("FPGA answered after %lu failed probe%s", (unsigned long)probe_tries,
                     probe_tries == 1 ? "" : "s");
        }
        probe_tries = 0;
        go(S_INFO);
    } else {
        probe_tries++;
        probe_err = err;
        retry_at  = plat_millis() + PROBE_RETRY_MS;
        probe_status(plat_millis(), true);      /* one line, updated in place */
        go(S_PROBE);
    }
}

static void info_done(const fpga_cmd_t *cmd, fpga_err_t err);

/* SAVE: the current room, FPGA -> card (rooms.c reads it back row by row,
 * then writes the file).
 * Runs when we leave a room, before the next one is loaded. A failed
 * command makes room_poll() say ROOM_FAILED; that triggers a resync. */
static void save_start(void)
{
    room_save_start(room_x, room_y);
    go(S_SAVE);
}

/* Same FPGA session, SAVE was interrupted: the grid is still there, so
 * carry on reading it from the first row not done yet. */
static void save_resume(void)
{
    if (!room_resume()) room_save_start(room_x, room_y);
    go(S_SAVE);
}

/* LOAD: room (room_x, room_y), card -> FPGA (rooms.c reads the file, then
 * sends it row by row). */
static void load_start(void)
{
    room_enter();
    room_load_start(room_x, room_y);
    go(S_LOAD);
}

/* Same FPGA session, LOAD was interrupted: carry on from the first row
 * not done yet (without resetting the game state room_enter() sets). */
static void load_resume(void)
{
    if (!room_resume()) room_load_start(room_x, room_y);
    go(S_LOAD);
}

/* RUN: the game itself  */
static struct {
    uint32_t sync_seen;         /* SYNC count we last started a frame at */
    int32_t  cx, cy;            /* cursor position, follows the mouse    */
    input_t  keys;              /* latest input, for held keys in run_frame */
} run;

/* State machine for run, handels if we are showing menu or doing other actions. */
typedef enum { RS_SIM, RS_MENU } run_state_t;
static const char *const run_state_names[] =
    { "simulating", "menu" };

static run_state_t  run_state = RS_SIM;

struct {
    sprite_t *menu_bg;
    sprite_t *menu_paused;
    sprite_t *menu_save;
    sprite_t *menu_load;             
} menu;


static void r_go(run_state_t s)
{
    if (s != run_state) LOG_INFO("%s -> %s", run_state_names[run_state], run_state_names[s]);
    run_state = s;
    switch (run_state) {
        case RS_SIM: // exit menu
            sprite_show(menu.menu_bg, false);
            sprite_show(menu.menu_load, false);
            sprite_show(menu.menu_paused, false);
            sprite_show(menu.menu_save, false);
        break;
        case RS_MENU: // enter menu
            sprite_show(menu.menu_bg, true);
            sprite_show(menu.menu_load, true);
            sprite_show(menu.menu_paused, true);
            sprite_show(menu.menu_save, true);
        break;
    }
}
/* HUD: the cursor, a test sentence and a demo animation (sprite.h) */
static sprite_t     *cursor;
static sprite_t     *title;
static sprite_ani_t *spinner;       /* only if sprites/spinner.spr is on the card */

static int32_t cursor_w(void) { return sprite_img_w(assets.cursor); }
static int32_t cursor_h(void) { return sprite_img_h(assets.cursor); }

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
}

/* setup things for the menu */
static void menu_setup(void)
{
    menu.menu_bg     = sprite_new(sprite_img_solid(200, 400, RGB565(19, 5, 30)), 200, 50, false);
    menu.menu_paused = sprite_new(assets.paused,200, 50, false);
    menu.menu_save   = sprite_new(assets.save_game,200, 100, false);
    menu.menu_load   = sprite_new(assets.load_game,200, 150, false);
}


/* Images and the HUD's sprites, made once, on the first load. From here
 * on sprite_poll() uploads the images in the background, and sends
 * everything again after a resync or a new FPGA session. */
static void sprites_setup(void)
{
    static bool done;
    if (done) return;
    done = true;

    sprite_init();
    assets_init();              /* reads the sprite files' headers from the card */

    const fpga_info_t *scr = fpga_link_info();
    player_init(100, 100);      /* start position for the room */

    /* test: a sentence centred near the top */
    int32_t w = sprite_img_w(assets.title);
    int32_t x = w < scr->width ? (scr->width - w) / 2 : 0;
    title = sprite_new(assets.title, (int16_t)x, 16, true);

    /* demo: an animation from the card, top-left, 10 frames a second */
    spinner = sprite_ani_file("sprites/spinner.spr", 6, 8, 8, true);

    menu_setup();
    cursor = sprite_new(assets.cursor, 0, 0, true);
}

/* Per-room game state: from load_start(), once per room. */
static void room_enter(void)
{
    sprites_setup();
    const fpga_info_t *scr = fpga_link_info();
    run.cx = (scr->width  - cursor_w()) / 2;
    run.cy = (scr->height - cursor_h()) / 2;
    sprite_move(cursor, (int16_t)run.cx, (int16_t)run.cy);
}

/* One game frame. n = SYNCs since the last one (n > 1: we fell behind). */
static void run_frame(uint32_t n)
{
    (void)n;

    update_player_position(0, 0, player, box);
    // fpga_cmd_read_sim_box(  // TODO: Use the new command
    //     next_velocity_box(player), box, 25*25
    // );
    
    int overlap = pos_in_portal(player.x, player.y, portals);
    if (overlap != -1)
    {
        player.promised_exit = (uint8_t) overlap;
        game_change_room(
            portals[overlap].dest_x,
            portals[overlap].dest_y
        );
    }
    
    /* TODO: update + draw */

    sprite_move(cursor, (int16_t)run.cx, (int16_t)run.cy);   /* sprite_poll sends it */
    sprite_frame(n);            /* animations step */

    switch (run_state) {
    case RS_SIM:  player_frame(&run.keys, n); break;
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
    run.keys = in;              /* held keys are "down right now", so the latest copy is enough */
    run.cx = clamp32(run.cx + in.mouse_dx, 0, (int32_t)scr->width  - cursor_w());
    run.cy = clamp32(run.cy + in.mouse_dy, 0, (int32_t)scr->height - cursor_h());

    if (key_pressed(&in, KEY_ESCAPE))
        r_go(run_state == RS_MENU ? RS_SIM : RS_MENU);
        

    if (!sprite_poll()) { failed = true; return; }
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
        sprite_forget();                /* its sprites are gone: send them again */
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
    sprite_lost();               /* ...so this comes after them      */
    failed   = false;
    retry_at = plat_millis();
    rdy.init = false;            /* READY must be steady again first */
    go(S_PROBE);
}

void game_poll(void)
{
    /* A console demo has the FPGA to itself: the game waits (its commands
     * already queued still finish). Afterwards RUN doesn't count the frames
     * that went by as missed. */
    static bool paused;
    if (demo_busy()) { paused = true; demo_poll(); return; }
    if (paused) { paused = false; if (state == S_RUN) run_enter(); }

    if (failed) { resync(); return; }

    switch (state) {
    case S_PROBE: {
        uint32_t now = plat_millis();
        bool quiet = ready_quiet(now);  /* tracked while waiting for retry_at too */
        probe_status(now, false);
        if (!quiet || (int32_t)(now - retry_at) < 0) break;
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
        switch (room_poll()) {
        case ROOM_FAILED: failed = true; break;
        case ROOM_DONE:                         /* saved: on to the next room */
            room_x = next_room_x;
            room_y = next_room_y;
            load_start();
            break;
        default: break;
        }
        break;
    case S_LOAD:
        /* sprites upload alongside the room */
        if (!sprite_poll()) { failed = true; break; }
        switch (room_poll()) {
        case ROOM_FAILED: failed = true; break;
        case ROOM_DONE:   go(S_RUN);     break;
        default: break;
        }
        break;
    case S_RUN:
        run_poll();
        break;
    }
}