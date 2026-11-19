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
#include "demo.h"
#include "probe.h"
#include "led.h"
#include "cli.h"
#include "menu.h"
#include "draw.h"
#include "hud.h"
#include "badapple.h"
#include "storage.h"

/* tuning */
#define FIRST_ROOM_X  0
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
static bool     fpga_reprogram;     /* this S_FPGA_RESET pulses PROGRAM_B, not RESET */



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

/* What a finished SAVE goes on to: the next room (through a door), or
 * back to the game (Save in the menu). */
static enum { AFTER_SAVE_NEXT_ROOM, AFTER_SAVE_RUN } after_save = AFTER_SAVE_NEXT_ROOM;

static player_t player;

static uint8_t box[VELOCITY_BOX_X*VELOCITY_BOX_Y] = {0};
static uint8_t box_player_projectile[VELOCITY_BOX_X*VELOCITY_BOX_Y] = {0};
static velocity_box_t velocity_box;

static bool shoot_pressed_last_time = false;
static bool switch_pressed_last_time = false;

static void run_enter(void);
static void room_enter(void);
static void menu_reset(void);

static void handle_player_keys(input_t in);

static void go(state_t s)
{
    if (s != state) {
        LOG_INFO("%s -> %s", state_names[state], state_names[s]);
        frame_sync_heartbeat_reset();   /* a new state gets the full time (frame_sync.h) */
    }
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
    menu_reset();               /* the load starts the simulation itself */
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
    uint32_t last_ms;           /* when the last frame ran (the stick cursor) */
    int32_t  cx, cy;            /* cursor position, follows the mouse    */
    input_t  keys;              /* this frame's input (taken once per frame) */
} run;

/* State machine for run: playing, in the menu (the simulation paused), or
 * drawing (no player; the mouse or a controller puts material into the
 * grid, see draw.h; it starts paused). */
typedef enum { RS_SIM, RS_MENU, RS_DRAW } run_state_t;
static const char *const run_state_names[] =
    { "simulating", "menu", "drawing" };

static run_state_t  run_state = RS_SIM;

static void r_go(run_state_t s)
{
    if (s == run_state) return;
    LOG_INFO("%s -> %s", run_state_names[run_state], run_state_names[s]);
    if (run_state == RS_DRAW) {         /* leaving drawing: the player and HUD are back */
        draw_leave();
        player_show(true);
        hud_visible(true);
    }
    run_state = s;
    if (s == RS_MENU) menu_open();
    else              menu_close();
    if (s == RS_DRAW) {                 /* drawing: no player, no player controls, no HUD */
        player_show(false);
        hud_visible(false);
        draw_enter();
    }
}

/* ---- START_SIM / STOP_SIM from the game -------------------------------------
 * The menu pauses the simulation and starts it again. A full queue just
 * means "try again next poll"; a failed command is a resync like any other. */
static int8_t sim_wanted = -1;          /* -1: nothing to send, 0: stop, 1: start */

static void sim_cmd_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    (void)cmd;
    if (err != FPGA_ERR_NONE && err != FPGA_ERR_CANCELLED) failed = true;
}

static void sim_flush(void)
{
    if (sim_wanted < 0) return;
    fpga_cmd_t c = fpga_cmd_sim_run(sim_wanted == 1);
    c.done = sim_cmd_done;
    fpga_status_t st = fpga_submit(&c);
    if (st == FPGA_EFULL) return;                       /* next poll */
    if (st != FPGA_OK) LOG_WARN("%s not sent: %s", sim_wanted ? "START_SIM" : "STOP_SIM",
                                fpga_status_str(st));
    sim_wanted = -1;
}

static void sim_run(bool run)
{
    sim_wanted = run;
    sim_flush();
}

/* Back to playing without a word to the FPGA: a load that's starting runs
 * the simulation itself. */
static void menu_reset(void)
{
    sim_wanted = -1;
    if (run_state != RS_SIM) r_go(RS_SIM);
}

/* ---- the menu's choices ------------------------------------------------------ */
static void menu_choose(menu_action_t a)
{
    LOG_INFO("menu: %s", menu_action_name(a));
    switch (a) {
    case MENU_CLOSE:                    /* back to the game */
        r_go(RS_SIM);
        sim_run(true);
        break;
    case MENU_SAVE:                     /* save the room, then play on in it */
        r_go(RS_SIM);
        after_save = AFTER_SAVE_RUN;
        save_start();                   /* the simulation is already stopped */
        break;
    case MENU_LOAD:                     /* the room as last saved, without saving now */
        load_start();                   /* starts the simulation when done */
        break;
    case MENU_RESTART:                  /* the room as designed: forget the save */
        delete_save(room_x, room_y);
        load_start();
        break;
    case MENU_DRAW:                     /* starts paused (the menu stopped the sim) */
        r_go(RS_DRAW);
        break;
    default:
        break;
    }
}

/* Esc, or the PS button on a controller */
static bool escape_pressed(const input_t *in)
{
    if (key_pressed(in, KEY_ESCAPE)) return true;
    for (unsigned p = 0; p < INPUT_PADS; p++)
        if (in->pad[p].connected && (in->pad[p].pressed & PAD_HOME)) return true;
    return false;
}

/* ---- the cursor from a controller -------------------------------------------
 * The right stick moves it (faster the further it's pushed), and a finger
 * on the touchpad drags it: a swipe across the whole pad crosses the whole
 * screen. Added to the mouse's movement (which needs nothing like this: a
 * mouse count is a pixel, however often frames come).
 * The stick's speed is per second of real time, so it's the same at any
 * frame rate: 714 px/s is the 12 px a frame it had at the FPGA's 59.5 Hz. */
#define CURSOR_STICK_PX_S 714   /* pixels per second at full tilt */
#define CURSOR_DEADZONE   16

static void pad_pointer(const input_t *in, uint32_t dt_ms, int32_t *dx, int32_t *dy)
{
    static int32_t frac_x, frac_y;      /* sub-pixel stick movement, x 127000 */
    static bool    was_down[INPUT_PADS];
    static uint16_t last_x[INPUT_PADS], last_y[INPUT_PADS];
    const fpga_info_t *scr = fpga_link_info();

    for (unsigned p = 0; p < INPUT_PADS; p++) {
        const pad_t *pad = &in->pad[p];
        if (!pad->connected) { was_down[p] = false; continue; }

        frac_x += pad_deadzone(pad->rx, CURSOR_DEADZONE) * CURSOR_STICK_PX_S * (int32_t)dt_ms;
        frac_y += pad_deadzone(pad->ry, CURSOR_DEADZONE) * CURSOR_STICK_PX_S * (int32_t)dt_ms;

        const pad_touch_t *t = &pad->touch[0];
        if (t->down && was_down[p]) {
            *dx += ((int32_t)t->x - last_x[p]) * scr->width  / PAD_TOUCH_W;
            *dy += ((int32_t)t->y - last_y[p]) * scr->height / PAD_TOUCH_H;
        }
        was_down[p] = t->down;
        last_x[p] = t->x;
        last_y[p] = t->y;
    }
    *dx += frac_x / 127000;             /* full tilt is 127, a second 1000 ms */
    *dy += frac_y / 127000;
    frac_x %= 127000;
    frac_y %= 127000;
}
/* The cursor, shown in every mode. The in-game HUD is hud.c's. */
static sprite_t     *cursor;

static int32_t cursor_w(void) { return sprite_img_w(assets.cursor); }
static int32_t cursor_h(void) { return sprite_img_h(assets.cursor); }

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* ---- timing (frame_sync.h) -----------------------------------------------------
 * RUN does a frame (input, cursor, sprites) per new SYNC; several at once
 * (we fell behind) is still one frame. While the game uses the FPGA (LOAD,
 * SAVE, RUN), a SYNC that stops is the FPGA gone: resync. Nothing is timed
 * by counting SYNCs: the player, the stick cursor and the animations go by
 * real time, so they play the same at any frame rate. */

/* ---- the player: fixed steps, one per SYNC or on real time ------------------
 * The player takes about PHYS_HZ steps a second (frame_steps_take()): one
 * per SYNC while SYNC comes at close to that rate (the real FPGA's
 * 59.52 Hz: smooth, a step every frame, 0.8 % slow), and PHYS_HZ a second
 * of real time otherwise. player.c's numbers (speeds, gravity, timers) are
 * per step.
 * A step is a READ_BOX of the cells around the player (velocity_box), then
 * update_player() in its callback, which sets the next box. Only one is
 * out at a time: the next owed step's read goes out from the callback, so
 * a step always reads the box the last one asked for. At most
 * PHYS_MAX_OWED owed: after a stall the player doesn't race to catch up.
 * Time only counts while playing (not in the menu, drawing or loading). */
#ifndef PHYS_HZ
#define PHYS_HZ        60u
#endif
#define PHYS_MAX_OWED  3u

static struct {
    frame_steps_t clock;        /* PHYS_HZ steps a second of real time */
    uint8_t       owed;         /* steps due, not done                 */
    bool          reading;      /* a step's READ_BOX is out            */
} phys;

/* Time so far doesn't count (an in-flight read still finishes). */
static void phys_reset(void)
{
    frame_steps_start(&phys.clock, PHYS_HZ);
    phys.owed = 0;
}

static void update_player_callback(const fpga_cmd_t *cmd, fpga_err_t err);
static void update_projectile_callback(const fpga_cmd_t *cmd, fpga_err_t err);

/* F8: the player to the top of the screen, same x. Done here, with no read
 * out: a read already sent is for the box around the old place. */
static bool teleport_top;

static void teleport(void)
{
    teleport_top      = false;
    player.y          = 0;
    player.velocity_y = 0;
    velocity_box = create_velocity_box(player.x, player.y, PLAYER_SIZE_X, PLAYER_SIZE_Y,
                                       player.velocity_x, player.velocity_y);
    player_sprite_init((int16_t)(player.x * FPGA_SIM_SCALE), 0);
}

/* The next owed step's READ_BOX, if none is out. A full queue: next frame. */
static void phys_read(void)
{
    if (phys.reading) return;
    if (teleport_top) teleport();
    if (phys.owed == 0) return;
    fpga_cmd_t c = fpga_cmd_read_box(velocity_box.box, box, velocity_box.length);
    c.done = update_player_callback;
    fpga_status_t st = fpga_submit(&c);
    if (player.projectile.active)
    {
        fpga_cmd_t plr_proj_cmd = fpga_cmd_read_box(
            player.projectile.velocity_box.box,
            player.projectile.box,
            player.projectile.velocity_box.length
        );
        plr_proj_cmd.done = update_projectile_callback;
        plr_proj_cmd.ctx = &(player.projectile);
        fpga_status_t proj_status = fpga_submit(&plr_proj_cmd);
    }
    if (st == FPGA_OK)    { phys.reading = true; return; }
    if (st == FPGA_EFULL) return;
    static bool told;           /* a bad box: once, not 60 times a second */
    if (!told) {
        told = true;
        LOG_WARN("player READ_BOX not sent: %s (box %u,%u %ux%u)", fpga_status_str(st),
                 velocity_box.box.x1, velocity_box.box.y1, velocity_box.box.x2, velocity_box.box.y2);
    }
    phys.owed = 0;
}

/* Once a frame while playing: the steps owed since the last frame, then
 * the first one's read. */
static void phys_frame(void)
{
    uint32_t owed = phys.owed + frame_steps_take(&phys.clock);
    phys.owed = (uint8_t)(owed < PHYS_MAX_OWED ? owed : PHYS_MAX_OWED);
    phys_read();
}

/* Runs on every way into RUN, also after a resync that kept the session,
 * so nothing here may reset the game. Time that went by while probing,
 * loading or in a demo doesn't count. */
static void run_enter(void)
{
    input_t stale;
    input_take(&stale);         /* input from loading / a demo / HOLD doesn't count */
    run.sync_seen = frame_sync_count();
    run.last_ms   = plat_millis();
    phys_reset();
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
    player_sprite_init((int) player.x, (int) player.y);   /* the player's picture, until it first moves */
    projectile_sprite_init(
        player.projectile.x * FPGA_SIM_SCALE,
        player.projectile.y * FPGA_SIM_SCALE
    );

    /* before the cursor: drawn under it */
    hud_init(scr->width, scr->height);     /* the bars, the test sentence, the animation */
    menu_init(scr->width, scr->height);
    draw_init(scr->width, scr->height);

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

/* The player's state, from player.c */
void set_player(player_t plr)
{
    player = plr;
    (&(player.projectile))->box = box_player_projectile;
}

/* Set the next velocity box */
void set_next_velocity_box(velocity_box_t *vb)
{
    velocity_box = *vb;
}

/* A step's READ_BOX is back: one step of the player. */
static void update_player_callback(const fpga_cmd_t *cmd, fpga_err_t err)
{
    phys.reading = false;
    if (err != FPGA_ERR_NONE)
    {
        if (err == FPGA_ERR_CANCELLED || err == FPGA_ERR_ABORTED)
            return;                                 /* a resync / reset is already on it */
        LOG_WARN(
            "read box, x %d, y %d, dx %d, dy %d: %s",
            velocity_box.box.x1,
            velocity_box.box.y1,
            velocity_box.box.x2,
            velocity_box.box.y2,
            fpga_err_str(err)
        );
        failed = true;                              /* the link is out of step: resync */
        return;
    }

    if (state != S_RUN) return;     /* a room started loading (F5, F6...): the box is old */
    if (phys.owed) phys.owed--;
    update_player(&player, cmd->rx, velocity_box);  /* moves its sprite too */
#if !HUD_DEMO
    health_bar_set(player.health * 100u / PLAYER_MAX_HEALTH);
    fuel_bar_set(player.fly_meter * 100u / MAX_FLY_TIME);
#endif
    /* The next owed step, if any -- unless that step took us out of
     * playing (a portal saves the room, dying reloads it). */
    if (state == S_RUN && run_state == RS_SIM) phys_read();
}

/* A step's READ_BOX is back: one step of the player. */
static void update_projectile_callback(const fpga_cmd_t *cmd, fpga_err_t err)
{
    projectile_t projectile = *((projectile_t*) (cmd->ctx));

    phys.reading = false;
    if (err != FPGA_ERR_NONE)
    {
        if (err == FPGA_ERR_CANCELLED || err == FPGA_ERR_ABORTED)
            return;                                 /* a resync / reset is already on it */
        LOG_WARN(
            "read box, x %d, y %d, dx %d, dy %d: %s",
            projectile.velocity_box.box.x1,
            projectile.velocity_box.box.y1,
            projectile.velocity_box.box.x2,
            projectile.velocity_box.box.y2,
            fpga_err_str(err)
        );
        failed = true;                              /* the link is out of step: resync */
        return;
    }

    if (state != S_RUN) return;     /* a room started loading (F5, F6...): the box is old */
    if (phys.owed) phys.owed--;
    update_projectile((projectile_t*) (cmd->ctx));  /* moves its sprite too */
    /* The next owed step, if any -- unless that step took us out of
     * playing (a portal saves the room, dying reloads it). */
    if (state == S_RUN && run_state == RS_SIM) phys_read();
}


/* ---- the board's user button -------------------------------------------------
 *   short press (let go before BUTTON_LONG_MS)  Bad Apple: start / stop, as F1
 *   long press  (held BUTTON_LONG_MS)           a new game: every save deleted,
 *                                               back to room FIRST_ROOM_X, _Y
 * The long press happens as soon as it's been held long enough, without
 * waiting for the release. Read every poll (button_poll()), acted on in RUN
 * only, like the function keys: a press in any other state is dropped. */
#define BUTTON_DEBOUNCE_MS  30u
#define BUTTON_LONG_MS      1500u

typedef enum { BTN_NONE, BTN_SHORT, BTN_LONG } button_event_t;
static button_event_t button_event;     /* for hotkeys() */

static void button_poll(void)
{
    static bool     raw, stable, long_done;
    static uint32_t raw_at, down_at;
    uint32_t now = plat_millis();
    bool r = plat_button_down();
    button_event_t ev = BTN_NONE;

    if (r != raw) { raw = r; raw_at = now; }            /* bouncing: wait */
    if (raw != stable && now - raw_at >= BUTTON_DEBOUNCE_MS) {
        stable = raw;
        if (stable) { down_at = now; long_done = false; }
        else if (!long_done) ev = BTN_SHORT;            /* let go early */
    }
    if (stable && !long_done && now - down_at >= BUTTON_LONG_MS) {
        long_done = true;
        ev = BTN_LONG;
    }
    if (ev != BTN_NONE) button_event = state == S_RUN ? ev : BTN_NONE;
    else if (state != S_RUN) button_event = BTN_NONE;
}

/* ---- function keys, the controller and the button: shortcuts -------------------
 *   F1  Bad Apple: start / stop (badapple.bin, Air and Stone, like the console)
 *       also: Share on a controller, a short press of the board's button
 *   F5  restart this room: as designed, its save deleted (the menu's Restart room)
 *   F6  clear the room: the whole grid air, on the FPGA (the 'clear' demo); the
 *       room file only changes if the room is saved after
 *   F7  back to the start: room FIRST_ROOM_X, _Y, the player at its starting
 *       place (saved rooms are kept: F9 first for a whole new game)
 *   F8  the player to the top of the screen, same x
 *   F9  delete every saved room (sd del save/)
 *   a new game, F9 + F7 in one: Options on a controller, a long press of the
 *       board's button
 * Only while the game runs (RUN). true: a room started loading, RUN is left. */

/* Pressed on any connected controller since the last frame. */
static bool any_pad_pressed(const input_t *in, uint32_t buttons)
{
    for (unsigned p = 0; p < INPUT_PADS; p++)
        if (in->pad[p].connected && (in->pad[p].pressed & buttons)) return true;
    return false;
}

static void badapple_toggle(const char *by)
{
    LOG_INFO("%s: Bad Apple %s", by, badapple_busy() ? "off" : "on");
    if (badapple_busy()) badapple_stop();
    else                 badapple_start("badapple.bin", false, MAT_AIR, MAT_STONE);
}

static void delete_saves(const char *by)
{
    uint32_t files = 0;
    storage_status_t st = storage_delete_dir("save/", &files);
    if (st == STORAGE_OK || st == STORAGE_NOT_FOUND) {
        LOG_INFO("%s: saved rooms deleted (%lu file%s)", by, (unsigned long)files,
                 files == 1 ? "" : "s");
        led_flash(LED_SAVES_DELETED, LED_SAVES_DELETED_MS);   /* 3 quick white blinks */
    } else
        LOG_WARN("%s: deleting save/: %s", by, storage_status_str(st));
}

static void back_to_start(const char *by)
{
    LOG_INFO("%s: back to the start, room %d, %d", by, FIRST_ROOM_X, FIRST_ROOM_Y);
    room_x = FIRST_ROOM_X;
    room_y = FIRST_ROOM_Y;
    player_set_default(&player);
    velocity_box = create_velocity_box(player.x, player.y, PLAYER_SIZE_X, PLAYER_SIZE_Y,
                                       player.velocity_x, player.velocity_y);
    player_sprite_init((int16_t)(player.x * FPGA_SIM_SCALE),
                       (int16_t)(player.y * FPGA_SIM_SCALE));
    projectile_sprite_init(
        player.projectile.x * FPGA_SIM_SCALE,
        player.projectile.y * FPGA_SIM_SCALE
    );
    load_start();
}

/* Every save deleted, and from the first room: a whole new game. */
static void new_game(const char *by)
{
    if (badapple_busy()) badapple_stop();   /* it would draw over the new room */
    delete_saves(by);
    back_to_start(by);
}

static bool hotkeys(const input_t *in)
{
    button_event_t btn = button_event;
    button_event = BTN_NONE;

    if (key_pressed(in, KEY_F1))               badapple_toggle("F1");
    if (any_pad_pressed(in, PAD_SELECT))       badapple_toggle("Share");
    if (btn == BTN_SHORT)                      badapple_toggle("button");

    if (any_pad_pressed(in, PAD_START)) { new_game("Options"); return true; }
    if (btn == BTN_LONG)                { new_game("button (long)"); return true; }

    if (key_pressed(in, KEY_F9)) delete_saves("F9");   /* before F7: F9 + F7 is a new game */
    if (key_pressed(in, KEY_F6)) {
        LOG_INFO("F6: clearing the room");
        demo_start("clear");                        /* the game waits while it runs */
    }
    if (key_pressed(in, KEY_F8)) {
        LOG_INFO("F8: the player to the top");
        teleport_top = true;                        /* done by phys_read() */
    }
    if (key_pressed(in, KEY_F5)) {
        LOG_INFO("F5: restarting room %d, %d", room_x, room_y);
        delete_save(room_x, room_y);
        load_start();
        return true;
    }

    if (key_pressed(in, KEY_F7)) { back_to_start("F7"); return true; }
    return false;
}

/* One game frame: on a SYNC, however many came since the last one. */
static void run_frame(void)
{
    /* the player only moves while playing (not in the menu, not while drawing) */
    if (run_state == RS_SIM) phys_frame();
    else                     phys_reset();

    sprite_move(cursor, (int16_t)run.cx, (int16_t)run.cy);   /* sprite_poll sends it */
    sprite_frame();             /* animations, by real time */
    hud_frame();                /* HUD_DEMO: the bars slide */
}

static void run_poll(void)
{
    if (!sprite_poll()) { failed = true; return; }
    sim_flush();                /* a START_SIM / STOP_SIM the queue had no room for */
    if (frame_sync_take(&run.sync_seen) == 0) return;   /* no new SYNC: no frame */

    uint32_t dt_ms = frame_elapsed_ms(&run.last_ms, 100u);  /* the stick cursor */

    /* Input once per frame: usb_input adds up presses, releases and mouse
     * movement between takes, so nothing that happened in between is lost
     * (taking every poll would throw presses away before a frame saw them). */
    const fpga_info_t *scr = fpga_link_info();
    input_take(&run.keys);
    led_set(LED_NO_INPUT, !run.keys.keyboard && !run.keys.mouse);
    /* input_log(&run.keys); */
    int32_t dx = run.keys.mouse_dx, dy = run.keys.mouse_dy;
    pad_pointer(&run.keys, dt_ms, &dx, &dy);
    /* The tip (the sprite's top-left) stays on the screen, so every cell
     * can be pointed at; the rest of the picture may hang off the right
     * and bottom edges. */
    run.cx = clamp32(run.cx + dx, 0, (int32_t)scr->width  - 1);
    run.cy = clamp32(run.cy + dy, 0, (int32_t)scr->height - 1);

    if (hotkeys(&run.keys)) return;                 /* F5 / F7: a room is loading */

    switch (run_state) {
    case RS_MENU: {
        menu_action_t a = menu_frame(&run.keys, run.cx, run.cy);
        if (a != MENU_NONE) menu_choose(a);
        if (state != S_RUN) return;     /* Save / Load / Restart left RUN */
        break;
    }
    case RS_DRAW:
        if (escape_pressed(&run.keys)) {              /* drawing off: play on */
            r_go(RS_SIM);
            sim_run(true);                  /* it may have been paused */
            break;
        }
        switch (draw_frame(&run.keys, run.cx, run.cy)) {
        case DRAW_FAILED: failed = true;    break;
        case DRAW_PAUSE:  sim_run(false);   break;
        case DRAW_RUN:    sim_run(true);    break;
        default:                            break;
        }
        break;
    case RS_SIM:
        if (escape_pressed(&run.keys)) {              /* the menu: pause */
            r_go(RS_MENU);
            sim_run(false);
            break;
        }
        handle_player_keys(run.keys);
        break;
    }

    run_frame();
}

/* Held on any connected controller. */
static bool any_pad_held(const input_t *in, uint32_t buttons)
{
    for (unsigned p = 0; p < INPUT_PADS; p++)
        if (in->pad[p].connected && (in->pad[p].held & buttons)) return true;
    return false;
}

/* The player's controls, while playing:
 *   left / right   A / D, the arrow keys, or the d-pad
 *   jump           space, the up arrow, d-pad up, or cross (A on an Xbox-style
 *                  pad); held in the air, it flies the jetpack */
static void handle_player_keys(input_t in)
{
    bool left_pressed  = key_held(&in, KEY_A)
                         || key_held(&in, KEY_LEFT)
                         || any_pad_held(&in, PAD_LEFT);
    bool right_pressed = key_held(&in, KEY_D)
                         || key_held(&in, KEY_RIGHT)
                         || any_pad_held(&in, PAD_RIGHT);

    bool shoot_pressed = mouse_held(&in, MOUSE_LEFT)
                         || any_pad_held(&in, PAD_R2);
    bool switch_pressed = mouse_held(&in, MOUSE_RIGHT)
                         || any_pad_held(&in, PAD_R1);

    if (shoot_pressed && !shoot_pressed_last_time)
        queue_shot(run.cx, run.cy);
    shoot_pressed_last_time = shoot_pressed;
    if (
        switch_pressed
        && !switch_pressed_last_time
        && !(player.projectile.active)
    )
    {
        enum projectile_type type = (&(player.projectile))->type;
        (&(player.projectile))->type = (type + 1) % PROJECTILE_TYPES;
    }
    switch_pressed_last_time = switch_pressed;

    if (left_pressed && !right_pressed)      move_left();
    else if (right_pressed && !left_pressed) move_right();
    else                                     move_stop();

    if (key_held(&in, KEY_SPACE) || key_held(&in, KEY_UP) ||
        any_pad_held(&in, PAD_UP | PAD_SOUTH))
        start_jump();
}

/* Leave the current room: save it, then load `to`.
 * TODO: let RUN's in-flight commands finish before saving. */
void game_change_room(int to_x, int to_y)
{
    if (state != S_RUN) return;
    menu_reset();
    next_room_x = to_x;
    next_room_y = to_y;
    after_save = AFTER_SAVE_NEXT_ROOM;
    save_start();
}

// Delete all saved rooms and start over
void game_reset()
{
    if (state != S_RUN) return;
    menu_reset();
    delete_saves("Death");
    room_x = FIRST_ROOM_X;
    room_y = FIRST_ROOM_Y;
    player_set_default(&player);
    load_start();
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

/* ---- resets (console: "reset", "reset fpga", "reset mcu", and with -b) ---
 * FPGA: everything queued is dropped, the reset pin is held low for
 * FPGA_RESET_PULSE_MS, and then it's a new FPGA: everything on it is
 * forgotten and the handshake starts over (with loading off: back to HOLD).
 * MCU: a full restart, as from power-up. Both: the FPGA's reset pulse, then
 * the MCU restart.
 * -b ("reprogram") does the same with PROGRAM_B instead of the reset pin:
 * the FPGA wipes its design and loads it from its flash again. That takes a
 * while; the handshake waits for DONE, as at power-up. */
static void fpga_pulse(bool program, bool hold)
{
    if (program) plat_fpga_program(hold);
    else         plat_fpga_reset(hold);
}

static void fpga_reset_start(bool program)
{
    if (program)
        LOG_WARN("reprogramming the FPGA (PROGRAM_B low for %u ms): it loads its design "
                 "from flash again", FPGA_RESET_PULSE_MS);
    else
        LOG_WARN("resetting the FPGA (reset pin low for %u ms)", FPGA_RESET_PULSE_MS);
    if (demo_busy()) demo_stop();
    if (state == S_PROBE) probe_stop();
    fpga_abort_all();                   /* callbacks run now (CANCELLED) */
    fpga_reprogram = program;
    fpga_pulse(program, true);
    fpga_reset_until = plat_millis() + FPGA_RESET_PULSE_MS;
    go(S_FPGA_RESET);
}

void game_reset_fpga(void)     { fpga_reset_start(false); }
void game_reprogram_fpga(void) { fpga_reset_start(true); }

void game_reset_mcu(void)
{
    LOG_WARN("resetting the MCU");
    plat_mcu_reset();                   /* doesn't return */
}

static void reset_all(bool program)
{
    LOG_WARN("%s the FPGA and resetting the MCU", program ? "reprogramming" : "resetting");
    fpga_abort_all();
    fpga_pulse(program, true);
    uint32_t t0 = plat_millis();
    while (plat_millis() - t0 < FPGA_RESET_PULSE_MS) { }   /* the MCU restarts anyway */
    fpga_pulse(program, false);
    plat_mcu_reset();
}

void game_reset_all(void)     { reset_all(false); }
void game_reprogram_all(void) { reset_all(true); }

/* The reset pulse is over: a new FPGA, nothing of ours on it. */
static void fpga_reset_done(void)
{
    fpga_pulse(fpga_reprogram, false);
    LOG_INFO("FPGA %s released: starting over", fpga_reprogram ? "PROGRAM_B" : "reset");
    sprite_forget();                    /* its memory is gone: send it all again */
    have_session = false;               /* -> connected() loads everything */
    resume_to    = S_LOAD;
    failed       = false;
    if (loading) { probe_start(); go(S_PROBE); }
    else         go(S_HOLD);
}

/* the state machine */
static void resync(const char *why)
{
    led_flash(LED_RESYNC, 1000);
    LOG_WARN("'%s': %s -> resync", state_names[state], why);
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
    button_poll();                      /* the board's button: hotkeys() acts on it */

    if (state == S_FPGA_RESET) {        /* before anything else: nothing may be sent */
        if ((int32_t)(plat_millis() - fpga_reset_until) >= 0) fpga_reset_done();
        return;
    }
    if (demo_busy()) { paused = true; demo_poll(); return; }
    if (paused) {
        paused = false;
        frame_sync_heartbeat_reset();   /* the demo's time doesn't count */
        if (state == S_RUN) run_enter();
    }

    if (state == S_HOLD) {              /* the console's turn: not even a resync */
        static bool told;
        if (!told && !GAME_LOADING) {
            told = true;
            LOG_INFO("loading is off (GAME_LOADING 0): no probing, the FPGA is the "
                     "console's ('loading on' to start)");
        }
        return;
    }
    if (failed) { resync(fpga_last_failure()); return; }
    if ((state == S_LOAD || state == S_SAVE || state == S_RUN) && !frame_sync_alive()) {
        LOG_WARN("no SYNC from the FPGA for %u ms", (unsigned)FRAME_SYNC_HEARTBEAT_MS);
        resync("no heartbeat: the FPGA is gone");
        return;
    }

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
        case ROOM_DONE:
            if (after_save == AFTER_SAVE_RUN) { /* saved from the menu: play on */
                go(S_RUN);
                sim_run(true);                  /* a save leaves it stopped */
                break;
            }
            room_x = next_room_x;               /* through a door: the next room */
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