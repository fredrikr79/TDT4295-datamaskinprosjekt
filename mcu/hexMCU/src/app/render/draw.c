/* draw.c - drawing mode (see draw.h). */

#define LOG_TAG "draw"

#include <string.h>

#include "draw.h"
#include "fpga.h"
#include "sprite.h"
#include "assets.h"
#include "log.h"

/* ---- look ----------------------------------------------------------------- */
#define COL_PANEL    RGB565(24, 12, 40)
#define COL_OUTLINE  RGB565(255, 255, 255)
#define MARGIN       4      /* screen edge -> panel           */
#define PAD          4      /* panel edge -> text             */
#define GAP          12     /* material name -> size          */
#define COL_RUNNING  RGB565(30, 110, 50)    /* sim panel while running */

#define MAX_IN_FLIGHT 3     /* placements queued at once: more just skip a frame */
#define FILL_BYTES   1024   /* the biggest box we send, in bytes          */

/* ---- brush sizes ------------------------------------------------------------- */
static uint16_t isqrt16(uint32_t v)
{
    uint32_t r = 0;
    while ((r + 1) * (r + 1) <= v) r++;
    return (uint16_t)r;
}

unsigned draw_sizes(uint16_t out[DRAW_SIZES_MAX])
{
    /* the biggest square that fits in one WRITE_BOX */
    uint32_t bytes = fpga_max_payload(WRITE_BOX_OPCODE);
    if (bytes > FILL_BYTES) bytes = FILL_BYTES;     /* the fill buffers' size */
    uint32_t cells = bytes * FPGA_SIM_IDS_PER_BYTE;
    uint16_t max = isqrt16(cells);
    if (max < 1) max = 1;

    unsigned n = 0;
    for (uint16_t s = 1; s <= 16 && s < max && n < DRAW_SIZES_MAX - 1u; s = (uint16_t)(s * 2u))
        out[n++] = s;
    out[n++] = max;
    return n;
}

/* ---- state ----------------------------------------------------------------- */
static struct {
    bool     made, active;
    uint16_t sw, sh;                        /* screen                    */
    uint16_t size[DRAW_SIZES_MAX];
    unsigned nsizes, cur_size;
    uint8_t  mat;                           /* material_t                */
    unsigned in_flight;
    bool     failed;                        /* a WRITE_BOX failed        */
    bool     running;                       /* the simulation, as shown  */
    bool     wait_release;                  /* no placing until the place /
                                               erase buttons are all let go */

    /* HUD, bottom left: one sprite per text, the current one shown */
    sprite_t *panel;
    sprite_t *mat_name[MAT_COUNT];
    sprite_t *size_name[DRAW_SIZES_MAX];

    /* HUD, bottom right: "Paused" / "Running", on a panel each */
    sprite_t *sim_panel[2], *sim_text[2];   /* [0] paused, [1] running */

    /* the brush outline at the cursor, per size: top, bottom, left, right
     * (sizes 1 and 2: one solid square in [0]) */
    sprite_t *outline[DRAW_SIZES_MAX][4];
} d = { .mat = MAT_SAND, .cur_size = 2 };

/* Every byte of a solid box is the same, so one buffer per material holds
 * any box; it stays valid while the DMA sends it. */
static uint8_t fill[MAT_COUNT][FILL_BYTES];

static int16_t max16(int16_t a, int16_t b) { return a > b ? a : b; }

void draw_init(uint16_t screen_w, uint16_t screen_h)
{
    if (d.made) return;
    d.made = true;
    d.sw = screen_w;
    d.sh = screen_h;
    d.nsizes = draw_sizes(d.size);
    if (d.cur_size >= d.nsizes) d.cur_size = d.nsizes - 1u;

    for (unsigned m = 0; m < MAT_COUNT; m++) memset(fill[m], SIM_CELL(m), sizeof fill[m]);

    /* HUD panel: as wide as the longest name and the longest size */
    int16_t mw = 0, sw = 0, th = 0;
    for (unsigned m = 0; m < MAT_COUNT; m++) {
        mw = max16(mw, (int16_t)sprite_img_w(assets.material[m]));
        th = max16(th, (int16_t)sprite_img_h(assets.material[m]));
    }
    for (unsigned i = 0; i < d.nsizes; i++) {
        sw = max16(sw, (int16_t)sprite_img_w(assets.draw_size[i]));
        th = max16(th, (int16_t)sprite_img_h(assets.draw_size[i]));
    }
    int16_t pw = (int16_t)(PAD + mw + GAP + sw + PAD), ph = (int16_t)(PAD + th + PAD);
    int16_t px = MARGIN, py = (int16_t)(screen_h - MARGIN - ph);

    d.panel = sprite_new(sprite_img_solid((uint16_t)pw, (uint16_t)ph, COL_PANEL), px, py, false);
    for (unsigned m = 0; m < MAT_COUNT; m++)
        d.mat_name[m] = sprite_new(assets.material[m], (int16_t)(px + PAD), (int16_t)(py + PAD), false);
    for (unsigned i = 0; i < d.nsizes; i++)
        d.size_name[i] = sprite_new(assets.draw_size[i], (int16_t)(px + PAD + mw + GAP),
                                    (int16_t)(py + PAD), false);

    /* sim state panel, bottom right: one size for both texts, so it
     * doesn't jump when it changes */
    sprite_img_t *st[2] = { assets.draw_paused, assets.draw_running };
    int16_t stw = 0, sth = 0;
    for (int k = 0; k < 2; k++) {
        stw = max16(stw, (int16_t)sprite_img_w(st[k]));
        sth = max16(sth, (int16_t)sprite_img_h(st[k]));
    }
    int16_t spw = (int16_t)(PAD + stw + PAD), sph = (int16_t)(PAD + sth + PAD);
    int16_t spx = (int16_t)(screen_w - MARGIN - spw), spy = (int16_t)(screen_h - MARGIN - sph);
    for (int k = 0; k < 2; k++) {
        d.sim_panel[k] = sprite_new(sprite_img_solid((uint16_t)spw, (uint16_t)sph,
                                                     k ? COL_RUNNING : COL_PANEL),
                                    spx, spy, false);
        d.sim_text[k]  = sprite_new(st[k], (int16_t)(spx + (spw - (int16_t)sprite_img_w(st[k])) / 2),
                                    (int16_t)(spy + PAD), false);
    }

    /* outlines: two thin images per size, shared by the opposite sides */
    for (unsigned i = 0; i < d.nsizes; i++) {
        uint16_t s = d.size[i];
        if (s <= 2) {
            d.outline[i][0] = sprite_new(sprite_img_solid(s, s, COL_OUTLINE), 0, 0, false);
            continue;
        }
        sprite_img_t *h = sprite_img_solid(s, 1, COL_OUTLINE);
        sprite_img_t *v = sprite_img_solid(1, (uint16_t)(s - 2u), COL_OUTLINE);
        d.outline[i][0] = sprite_new(h, 0, 0, false);
        d.outline[i][1] = sprite_new(h, 0, 0, false);
        d.outline[i][2] = sprite_new(v, 0, 0, false);
        d.outline[i][3] = sprite_new(v, 0, 0, false);
    }
    LOG_INFO("brush sizes 1 .. %u", d.size[d.nsizes - 1u]);
}

/* ---- HUD --------------------------------------------------------------------- */
static void hud_show(bool on)
{
    sprite_show(d.panel, on);
    for (int k = 0; k < 2; k++) {
        sprite_show(d.sim_panel[k], on && k == (int)d.running);
        sprite_show(d.sim_text[k],  on && k == (int)d.running);
    }
    for (unsigned m = 0; m < MAT_COUNT; m++) sprite_show(d.mat_name[m], on && m == d.mat);
    for (unsigned i = 0; i < d.nsizes; i++) {
        sprite_show(d.size_name[i], on && i == d.cur_size);
        for (unsigned k = 0; k < 4; k++) sprite_show(d.outline[i][k], false);
    }
}

/* The outline of the current size around the box that would be placed. */
static void outline_at(int32_t x, int32_t y)
{
    for (unsigned i = 0; i < d.nsizes; i++) {
        uint16_t s = d.size[i];
        bool on = d.active && i == d.cur_size;
        if (s <= 2) {
            sprite_set(d.outline[i][0], (int16_t)x, (int16_t)y, on);
            continue;
        }
        sprite_set(d.outline[i][0], (int16_t)x, (int16_t)y, on);
        sprite_set(d.outline[i][1], (int16_t)x, (int16_t)(y + s - 1), on);
        sprite_set(d.outline[i][2], (int16_t)x, (int16_t)(y + 1), on);
        sprite_set(d.outline[i][3], (int16_t)(x + s - 1), (int16_t)(y + 1), on);
    }
}

void draw_enter(void)
{
    d.active  = true;
    d.running = false;                      /* the menu stopped it */
    /* the click / cross that chose Draw in the menu is most likely still
     * down: don't let it place a box the moment drawing starts */
    d.wait_release = true;
    hud_show(true);
    LOG_INFO("drawing: material %u, %ux%u", d.mat, d.size[d.cur_size], d.size[d.cur_size]);
}

void draw_leave(void)
{
    d.active = false;
    hud_show(false);
}

bool draw_active(void) { return d.active; }

/* ---- placing ------------------------------------------------------------------- */
static void placed(const fpga_cmd_t *c, fpga_err_t err)
{
    (void)c;
    if (d.in_flight) d.in_flight--;
    if (err != FPGA_ERR_NONE && err != FPGA_ERR_CANCELLED) {
        LOG_WARN("WRITE_BOX: %s", fpga_err_str(err));
        d.failed = true;
    }
}

/* A size x size box of material m centred on (cx, cy), cut to the screen. */
static void place(uint8_t m, int32_t cx, int32_t cy)
{
    if (d.in_flight >= MAX_IN_FLIGHT) return;       /* the queue is busy: next frame */
    int32_t s = d.size[d.cur_size];
    int32_t x0 = cx - s / 2, y0 = cy - s / 2, x1 = x0 + s, y1 = y0 + s;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > d.sw) x1 = d.sw;
    if (y1 > d.sh) y1 = d.sh;
    if (x1 <= x0 || y1 <= y0) return;

    uint16_t w = (uint16_t)(x1 - x0), h = (uint16_t)(y1 - y0);
    fpga_cmd_t c = fpga_cmd_write_box(fpga_box((uint16_t)x0, (uint16_t)y0, w, h), fill[m],
                                      (uint16_t)fpga_sim_bytes((uint32_t)w * h));
    c.done = placed;
    if (fpga_submit(&c) == FPGA_OK) d.in_flight++;
}

/* ---- input ----------------------------------------------------------------------- */
static bool any_pad_pressed(const input_t *in, uint32_t b)
{
    for (unsigned p = 0; p < INPUT_PADS; p++)
        if (in->pad[p].connected && (in->pad[p].pressed & b)) return true;
    return false;
}

static bool any_pad_held(const input_t *in, uint32_t b)
{
    for (unsigned p = 0; p < INPUT_PADS; p++)
        if (in->pad[p].connected && (in->pad[p].held & b)) return true;
    return false;
}

static void set_material(int m)
{
    m = ((m % (int)MAT_COUNT) + (int)MAT_COUNT) % (int)MAT_COUNT;     /* wraps both ways */
    if ((uint8_t)m == d.mat) return;
    d.mat = (uint8_t)m;
    hud_show(true);
}

static void set_size(int i)
{
    if (i < 0) i = 0;
    if (i >= (int)d.nsizes) i = (int)d.nsizes - 1;
    if ((unsigned)i == d.cur_size) return;
    d.cur_size = (unsigned)i;
    hud_show(true);
}

draw_event_t draw_frame(const input_t *in, int32_t cx, int32_t cy)
{
    if (d.failed) { d.failed = false; return DRAW_FAILED; }
    if (!d.active) return DRAW_OK;

    /* material: number row, wheel, L1 / R1, d-pad left / right */
    for (unsigned m = 0; m < MAT_COUNT && m < 10u; m++) {
        uint8_t key = m == 0 ? KEY_0 : (uint8_t)(KEY_1 + m - 1u);
        if (key_pressed(in, key)) set_material((int)m);
    }
    int step = 0;
    if (in->wheel > 0) step++;              /* up / away: next */
    if (in->wheel < 0) step--;
    if (any_pad_pressed(in, PAD_R1 | PAD_RIGHT)) step++;
    if (any_pad_pressed(in, PAD_L1 | PAD_LEFT))  step--;
    if (step) set_material((int)d.mat + step);

    /* brush size: W / S, arrows, d-pad up / down */
    int grow = 0;
    if (key_pressed(in, KEY_W) || key_pressed(in, KEY_UP))   grow++;
    if (key_pressed(in, KEY_S) || key_pressed(in, KEY_DOWN)) grow--;
    if (any_pad_pressed(in, PAD_UP))   grow++;
    if (any_pad_pressed(in, PAD_DOWN)) grow--;
    if (grow) set_size((int)d.cur_size + grow);

    /* the outline follows the cursor */
    int32_t s = d.size[d.cur_size];
    outline_at(cx - s / 2, cy - s / 2);

    /* place / erase while held */
    bool erase = mouse_held(in, MOUSE_RIGHT) || any_pad_held(in, PAD_EAST | PAD_L2);
    bool put   = mouse_held(in, MOUSE_LEFT)  || any_pad_held(in, PAD_SOUTH | PAD_R2);
    if (d.wait_release && !erase && !put) d.wait_release = false;
    if (!d.wait_release) {
        if (erase)    place(MAT_AIR, cx, cy);
        else if (put) place(d.mat, cx, cy);
    }

    /* pause / run: square, Enter, middle (wheel) button */
    if (any_pad_pressed(in, PAD_WEST) || key_pressed(in, KEY_ENTER) ||
        mouse_pressed(in, MOUSE_MIDDLE)) {
        d.running = !d.running;
        hud_show(true);
        LOG_INFO("simulation %s", d.running ? "running" : "paused");
        return d.running ? DRAW_RUN : DRAW_PAUSE;
    }
    return DRAW_OK;
}