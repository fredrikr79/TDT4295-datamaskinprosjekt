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
    uint16_t gw, gh;                        /* the grid, cells           */
    uint16_t size[DRAW_SIZES_MAX];          /* brush sizes, cells        */
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

    /* the brush outline, per size: top, bottom, left, right, two pieces
     * each (see outline_at()). In screen pixels around the cells the brush
     * fills, so size 1 is a 4x4 frame. */
    sprite_t *outline[DRAW_SIZES_MAX][4][2];
    uint16_t  outline_len[DRAW_SIZES_MAX];  /* pixels, one piece */
    uint16_t  sw, sh;                       /* the screen, pixels */
} d = { .mat = MAT_SAND, .cur_size = 2 };

/* Every byte of a solid box is the same, so one buffer per material holds
 * any box; it stays valid while the DMA sends it. */
static uint8_t fill[MAT_COUNT][FILL_BYTES];

/* The byte a drawn cell gets: the material in the top 4 bits, and in the
 * low 4 bits what the material needs to start out right (fpga.h). */
static uint8_t draw_cell(unsigned m)
{
    switch (m) {
    case MAT_LIFE:      return (uint8_t)(SIM_CELL(m) | SIM_LIFE_ALIVE);       /* 0x68 */
    case MAT_EXPLOSIVE: return (uint8_t)(SIM_CELL(m) | SIM_EXPLOSIVE_FULL);  /* 0x7F */
    default:            return SIM_CELL(m);
    }
}

static int16_t max16(int16_t a, int16_t b) { return a > b ? a : b; }

void draw_init(uint16_t screen_w, uint16_t screen_h)
{
    if (d.made) return;
    d.made = true;
    d.sw = screen_w;
    d.sh = screen_h;
    d.gw = (uint16_t)(screen_w / FPGA_SIM_SCALE);   /* = fpga_link_info()->sim_w */
    d.gh = (uint16_t)(screen_h / FPGA_SIM_SCALE);
    d.nsizes = draw_sizes(d.size);
    if (d.cur_size >= d.nsizes) d.cur_size = d.nsizes - 1u;

    for (unsigned m = 0; m < MAT_COUNT; m++) memset(fill[m], draw_cell(m), sizeof fill[m]);

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

    /* outlines: per size, a line half the brush long (rounded up to whole
     * cells), across and down, shared by all eight pieces. Two pieces
     * overlapping make a side of any length from one piece to the whole
     * brush, which is what a box cut by the screen's edge needs. */
    for (unsigned i = 0; i < d.nsizes; i++) {
        uint16_t len = (uint16_t)fpga_cell_to_px((d.size[i] + 1u) / 2u);   /* pixels */
        sprite_img_t *across = sprite_img_solid(len, 1, COL_OUTLINE);
        sprite_img_t *down   = sprite_img_solid(1, len, COL_OUTLINE);
        d.outline_len[i] = len;
        for (unsigned k = 0; k < 4; k++)
            for (unsigned j = 0; j < 2; j++)
                d.outline[i][k][j] = sprite_new(k < 2 ? across : down, 0, 0, false);
    }
    LOG_INFO("grid %ux%u cells, brush sizes 1 .. %u cells", d.gw, d.gh, d.size[d.nsizes - 1u]);
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
        for (unsigned k = 0; k < 4; k++)
            for (unsigned j = 0; j < 2; j++) sprite_show(d.outline[i][k][j], false);
    }
}

/* The brush's box in cells, x0 <= x < x1, y0 <= y < y1: size x size,
 * centred on the cell under screen pixel (px, py), cut to the grid. So
 * near an edge it is smaller: what place() writes and the outline shows.
 * false: nothing of it is on the grid. */
static bool brush_box(int32_t px, int32_t py,
                      int32_t *x0, int32_t *y0, int32_t *x1, int32_t *y1)
{
    int32_t s = d.size[d.cur_size];
    *x0 = fpga_px_to_cell(px) - s / 2;
    *y0 = fpga_px_to_cell(py) - s / 2;
    *x1 = *x0 + s;
    *y1 = *y0 + s;
    if (*x0 < 0) *x0 = 0;
    if (*y0 < 0) *y0 = 0;
    if (*x1 > d.gw) *x1 = d.gw;
    if (*y1 > d.gh) *y1 = d.gh;
    return *x1 > *x0 && *y1 > *y0;
}

static int32_t min32(int32_t a, int32_t b) { return a < b ? a : b; }

/* The outline around the brush's box (cells, cut to the grid), in screen
 * pixels. Every piece is placed wholly on the screen: the FPGA mustn't get
 * a sprite that hangs off it (it may wrap around or write past the HUD
 * layer). A side is two pieces of one length, one from each end, so they
 * overlap in the middle; that gives any length from one piece up to two.
 * The box cut by an edge is never shorter than one piece (the cursor's
 * cell is on the grid, so at least half the brush is), only if the grid
 * is smaller than the brush; then there is no outline. */
static void outline_at(bool any, int32_t cx0, int32_t cy0, int32_t cx1, int32_t cy1)
{
    int32_t x0 = fpga_cell_to_px(cx0), y0 = fpga_cell_to_px(cy0);
    int32_t x1 = min32(fpga_cell_to_px(cx1), d.sw);     /* past the box, pixels */
    int32_t y1 = min32(fpga_cell_to_px(cy1), d.sh);
    for (unsigned i = 0; i < d.nsizes; i++) {
        int32_t l = d.outline_len[i];
        bool on = d.active && any && i == d.cur_size && x1 - x0 >= l && y1 - y0 >= l;
        if (!on) {
            for (unsigned k = 0; k < 4; k++)
                for (unsigned j = 0; j < 2; j++) sprite_show(d.outline[i][k][j], false);
            continue;
        }
        const int16_t at[4][2][2] = {                   /* [side][piece] = { x, y } */
            { { x0,     y0     }, { x1 - l, y0     } },     /* top    */
            { { x0,     y1 - 1 }, { x1 - l, y1 - 1 } },     /* bottom */
            { { x0,     y0     }, { x0,     y1 - l } },     /* left   */
            { { x1 - 1, y0     }, { x1 - 1, y1 - l } },     /* right  */
        };
        for (unsigned k = 0; k < 4; k++)
            for (unsigned j = 0; j < 2; j++)
                sprite_set(d.outline[i][k][j], at[k][j][0], at[k][j][1], true);
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

/* A size x size box of material m (cells) at the cursor (screen pixels
 * px, py), cut to the grid. Size 1 is one byte of payload. */
static void place(uint8_t m, int32_t px, int32_t py)
{
    if (d.in_flight >= MAX_IN_FLIGHT) return;       /* the queue is busy: next frame */
    int32_t x0, y0, x1, y1;
    if (!brush_box(px, py, &x0, &y0, &x1, &y1)) return;

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

    /* the outline follows the cursor, jumping a whole cell at a time, and
     * shrinks with the box at the screen's edges */
    int32_t bx0, by0, bx1, by1;
    bool any = brush_box(cx, cy, &bx0, &by0, &bx1, &by1);
    outline_at(any, bx0, by0, bx1, by1);

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