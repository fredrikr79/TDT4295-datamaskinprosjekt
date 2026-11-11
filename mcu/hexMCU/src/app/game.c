#define LOG_TAG "game"

#include <stdint.h>
#include <stdbool.h>

#include "fpga.h"
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
#include "probe.h"
#include "led.h"
#include "cli.h"

/* tuning */
#define FIRST_ROOM_X -3
#define FIRST_ROOM_Y  0

/* ======================================================================
 * The state machine
 *
 *   PROBE -> LOAD -> RUN
 *             ^       |
 *             |       v  (door)
 *             +---- SAVE
 *
 * PROBE is the handshake (probe.c: steady READY, ECHO, INFO).
 * With loading off (console: "loading off"), PROBE -> HOLD instead: the
 * game sends nothing more and the console has the FPGA to itself.
 * Any failed command -> resync -> PROBE. If the FPGA still has
 * the same session (its memory survived), we go back to the state that
 * was interrupted. If not, everything on it is gone and the current
 * room is loaded again.
 * ====================================================================== */
 
/* This is the higher state machine that is responsible for error recovery and switching of scenes */
typedef enum { S_PROBE, S_SAVE, S_LOAD, S_RUN, S_HOLD, S_FPGA_RESET } state_t;
static const char *const state_names[] = { "probe", "save", "load", "run", "hold", "fpga-reset" };

#define FPGA_RESET_PULSE_MS 10u     /* the FPGA's reset pin held low this long */
static uint32_t fpga_reset_until;



#ifndef GAME_LOADING
#define GAME_LOADING 1          /* 0: start with loading off: HOLD, no probing */
#endif
static bool     loading = GAME_LOADING;

static state_t  state = GAME_LOADING ? S_PROBE : S_HOLD;
static state_t  resume_to = S_LOAD; /* where a resync goes back to (same session) */
static bool     failed;             /* a callback saw an error          */
static bool     have_session;
static uint32_t session;            /* the FPGA session we drew into    */

static int room_x = FIRST_ROOM_X;   /* the x-coordinate of the current room */
static int room_y = FIRST_ROOM_Y;   /* the y-coordinate of the current room */
static int next_room_x;             /* the x-coordinate of the next room */
static int next_room_y;             /* the y-coordinate of the next room */

static player_t player;

static portal_t portals[MAX_PORTALS] = {0};

static uint8_t box[VELOCITY_BOX_X*VELOCITY_BOX_Y] = {0};
static velocity_box_t velocity_box;

static void run_enter(void);
static void room_enter(void);

static void handle_movement_keys(input_t in);

static void go(state_t s)
{
    if (s != state) LOG_INFO("%s -> %s", state_names[state], state_names[s]);
    state = s;
    if (s == S_RUN) run_enter();
}

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
    input_t  keys;              /* this frame's input (taken once per frame) */
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
/* ---- frames: the FPGA's SYNC, or a timer while there is none -------------
 * RUN steps once per SYNC (FPGA_FRAME_HZ, 25 Hz). If no SYNC has come for
 * SYNC_LOST_MS, frames come from a FRAME_MS timer instead, so the game
 * still runs while the FPGA doesn't send SYNC (yet). Logged each time it
 * switches. */
#define SYNC_LOST_MS 200u
#define FRAME_MS     (1000u / FPGA_FRAME_HZ)    /* 40 ms at 25 Hz, like SYNC */

static struct {
    uint32_t last_sync_ms;      /* when a SYNC was last seen           */
    uint32_t next_frame_ms;     /* timer mode: when the next frame is due */
    bool     timer;             /* frames from the timer right now     */
} frames;

/* Frames to run now: SYNCs since last time, or timer ticks without SYNC. */
static uint32_t frames_due(void)
{
    uint32_t now = plat_millis();
    uint32_t n = frame_sync_take(&run.sync_seen);
    if (n) {
        frames.last_sync_ms = now;
        if (frames.timer) {
            frames.timer = false;
            LOG_INFO("frame SYNC from the FPGA: frames follow it again");
        }
        return n;
    }
    if (!frames.timer) {
        if (now - frames.last_sync_ms < SYNC_LOST_MS) return 0;
        frames.timer = true;
        frames.next_frame_ms = now;
        LOG_WARN("no frame SYNC from the FPGA for %u ms: frames from a %u ms timer instead",
                 SYNC_LOST_MS, FRAME_MS);
    }
    if ((int32_t)(now - frames.next_frame_ms) < 0) return 0;
    n = (now - frames.next_frame_ms) / FRAME_MS + 1u;
    frames.next_frame_ms += n * FRAME_MS;
    return n;
}

static void run_enter(void)
{
    input_t stale;
    input_take(&stale);         /* input from loading / a demo / HOLD doesn't count */
    run.sync_seen = frame_sync_count();
    frames.last_sync_ms  = plat_millis();     /* give SYNC a chance first */
    frames.next_frame_ms = plat_millis();
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
    player_init_old(100, 100);      /* start position for the room */

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

/* Set the next velocity box */
void set_player(player_t plr)
{
    player = plr;
}

void portals_init()
{
    init_empty_portals(portals, MAX_PORTALS);
}

/* Set the next velocity box */
void set_next_velocity_box(velocity_box_t *vb)
{
    velocity_box = *vb;
}

static void update_player_callback(const fpga_cmd_t *cmd, fpga_err_t err)
{
    if (err != FPGA_ERR_NONE)
    {
        LOG_WARN(
            "read box, x %d, y %d, dx %d, dy %d: %s",
            velocity_box.box.x1,
            velocity_box.box.y1,
            velocity_box.box.x2,
            velocity_box.box.y2,
            fpga_err_str(err)
        );
        return;
    }

    float old_x = player.x;
    float old_y = player.y;

    update_player(&player, cmd->rx, velocity_box, portals);

    if(player.x != old_x || player.y != old_y) funny_man_move(player.x, player.y);
}

/* One game frame. n = SYNCs since the last one (n > 1: we fell behind). */
static void run_frame(uint32_t n)
{
    (void)n;

    //comment out for testing
    fpga_cmd_t player_box_cmd = fpga_cmd_read_box(
        velocity_box.box, box, velocity_box.length
    );
    player_box_cmd.done = update_player_callback;
    fpga_submit(&player_box_cmd);
    
    /* TODO: update + draw */

    sprite_move(cursor, (int16_t)run.cx, (int16_t)run.cy);   /* sprite_poll sends it */
    sprite_frame(n);            /* animations step */

    // switch (run_state) {
    // case RS_SIM:  player_frame(&run.keys, n); break;
    // case RS_MENU: /* do menu */ break;
    // }
}

static void run_poll(void)
{
    if (!sprite_poll()) { failed = true; return; }
    uint32_t n = frames_due();
    if (n == 0) return;

    /* Input once per frame: usb_input adds up presses, releases and mouse
     * movement between takes, so nothing that happened in between is lost
     * (taking every poll would throw presses away before a frame saw them). */
    const fpga_info_t *scr = fpga_link_info();
    input_take(&run.keys);
    led_set(LED_NO_INPUT, !run.keys.keyboard && !run.keys.mouse);
    /* input_log(&run.keys); */
    run.cx = clamp32(run.cx + run.keys.mouse_dx, 0, (int32_t)scr->width  - cursor_w());
    run.cy = clamp32(run.cy + run.keys.mouse_dy, 0, (int32_t)scr->height - cursor_h());

    if (key_pressed(&run.keys, KEY_ESCAPE))
        r_go(run_state == RS_MENU ? RS_SIM : RS_MENU);

    handle_movement_keys(run.keys);

    run_frame(n);
}

static void handle_movement_keys(input_t in)
{
    bool left_pressed = key_held(&in, KEY_A);
    bool right_pressed = key_held(&in, KEY_D);

    if (left_pressed && !right_pressed)      move_left();
    else if (right_pressed && !left_pressed) move_right();
    else                                     move_stop();

    if (key_held(&in, KEY_SPACE)) start_jump();
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

/* ---- the handshake is done: same session -> resume, new -> reload ------ */
static void connected(void)
{
    const fpga_info_t *in = fpga_link_info();
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

/* ---- loading on / off (console) -----------------------------------------
 * Off: the game holds (HOLD) right away and sends nothing, so the console
 * can test the FPGA by hand -- also in the middle of the handshake, which
 * then stops trying. Commands the game already queued still finish.
 * Built with GAME_LOADING 0 it starts like that: HOLD, no probing at all.
 * On: from HOLD, a new handshake (the console may have changed anything),
 * then on as after a resync: same session -> carry on where it was, new
 * session -> load everything again. */
bool game_loading(void) { return loading; }

static void hold(void)
{
    if (state == S_SAVE || state == S_LOAD || state == S_RUN) resume_to = state;
    cli_status(NULL);
    LOG_INFO("loading is off: the game holds, the FPGA is the console's ('loading on' to go on)");
    go(S_HOLD);
}

void game_set_loading(bool on)
{
    loading = on;
    if (!on) {
        if (state == S_PROBE) probe_stop();     /* no more tries: the console takes over now */
        if (state != S_HOLD) hold();
        return;
    }
    if (state != S_HOLD) return;        /* already going */
    LOG_INFO("loading is on: handshake again, then on from '%s'", state_names[resume_to]);
    fpga_abort_all();                   /* whatever the console left queued */
    sprite_lost();
    failed = false;
    probe_start();
    go(S_PROBE);
}

/* ---- resets (console: "reset", "reset fpga", "reset mcu") ---------------
 * FPGA: everything queued is dropped, the reset pin is held low for
 * FPGA_RESET_PULSE_MS, and then it's a new FPGA: everything on it is
 * forgotten and the handshake starts over (with loading off: back to HOLD).
 * MCU: a full restart, as from power-up. Both: the FPGA's reset pulse, then
 * the MCU restart. */
void game_reset_fpga(void)
{
    LOG_WARN("resetting the FPGA (reset pin low for %u ms)", FPGA_RESET_PULSE_MS);
    if (demo_busy()) demo_stop();
    if (state == S_PROBE) probe_stop();
    fpga_abort_all();                   /* callbacks run now (CANCELLED) */
    plat_fpga_reset(true);
    fpga_reset_until = plat_millis() + FPGA_RESET_PULSE_MS;
    go(S_FPGA_RESET);
}

void game_reset_mcu(void)
{
    LOG_WARN("resetting the MCU");
    plat_mcu_reset();                   /* doesn't return */
}

void game_reset_all(void)
{
    LOG_WARN("resetting the FPGA and the MCU");
    fpga_abort_all();
    plat_fpga_reset(true);
    uint32_t t0 = plat_millis();
    while (plat_millis() - t0 < FPGA_RESET_PULSE_MS) { }   /* the MCU restarts anyway */
    plat_fpga_reset(false);
    plat_mcu_reset();
}

/* The reset pulse is over: a new FPGA, nothing of ours on it. */
static void fpga_reset_done(void)
{
    plat_fpga_reset(false);
    LOG_INFO("FPGA reset released: starting over");
    sprite_forget();                    /* its memory is gone: send it all again */
    have_session = false;               /* -> connected() loads everything */
    resume_to    = S_LOAD;
    failed       = false;
    if (loading) { probe_start(); go(S_PROBE); }
    else         go(S_HOLD);
}

/* the state machine */
static void resync(void)
{
    led_flash(LED_RESYNC, 1000);
    LOG_WARN("command failed in '%s' -> resync: %s", state_names[state], fpga_last_failure());
    if (state == S_SAVE || state == S_LOAD || state == S_RUN)
        resume_to = state;
    fpga_abort_all();            /* callbacks run now (CANCELLED)... */
    sprite_lost();               /* ...so this comes after them      */
    failed = false;
    probe_start();
    go(S_PROBE);
}

void game_poll(void)
{
    /* A console demo has the FPGA to itself: the game waits (its commands
     * already queued still finish). Afterwards RUN doesn't count the frames
     * that went by as missed. */
    static bool paused;

    /* the status LED: what the game is doing (led.h has the rest) */
    led_set(LED_HOLD,    state == S_HOLD);
    led_set(LED_RUNNING, state == S_RUN);
    if (state != S_RUN) led_set(LED_NO_INPUT, false);
    led_poll();

    if (state == S_FPGA_RESET) {        /* before anything else: nothing may be sent */
        if ((int32_t)(plat_millis() - fpga_reset_until) >= 0) fpga_reset_done();
        return;
    }
    if (demo_busy()) { paused = true; demo_poll(); return; }
    if (paused) { paused = false; if (state == S_RUN) run_enter(); }

    if (state == S_HOLD) {              /* the console's turn: not even a resync */
        static bool told;
        if (!told && !GAME_LOADING) {
            told = true;
            LOG_INFO("loading is off (GAME_LOADING 0): no probing, the FPGA is the "
                     "console's ('loading on' to start)");
        }
        return;
    }
    if (failed) { resync(); return; }

    switch (state) {
    case S_PROBE:
        if (probe_poll() != PROBE_DONE) break;
        if (loading) connected();
        else         hold();
        break;
    case S_HOLD:
    case S_FPGA_RESET:
        break;
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