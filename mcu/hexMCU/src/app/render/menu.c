/* menu.c - the pause menu (see menu.h). */

#define LOG_TAG "menu"

#include "menu.h"
#include "sprite.h"
#include "assets.h"
#include "log.h"

/* ---- look ----------------------------------------------------------------- */
#define COL_PANEL    RGB565(24, 12, 40)     /* background                 */
#define COL_BORDER   RGB565(255, 220, 0)    /* frame, same as the text    */
#define COL_BUTTON   RGB565(52, 30, 84)
#define COL_HOVER    RGB565(130, 70, 190)   /* the button under the cursor */
#define COL_LINE     RGB565(90, 60, 130)    /* under the title            */

#define BORDER       2      /* frame thickness                            */
#define PAD          12     /* panel edge -> contents                     */
#define BTN_PAD_X    16     /* button edge -> its text                    */
#define BTN_PAD_Y    6
#define GAP          6      /* between buttons                            */
#define MIN_BTN_W    120    /* buttons are at least this wide             */

/* ---- the buttons, top to bottom ---------------------------------------------- */
#define NBTN 4

static const menu_action_t btn_action[NBTN] = { MENU_SAVE, MENU_LOAD, MENU_RESTART, MENU_DRAW };

static struct {
    bool     made;
    bool     open;
    int      hover;                     /* button highlighted, -1 = none   */
    int32_t  last_cx, last_cy;          /* cursor at the last frame        */

    /* layout, screen coordinates */
    int16_t  px, py, pw, ph;            /* panel (inside the frame)        */
    int16_t  bx, bw, bh;                /* buttons: x, size                */
    int16_t  by[NBTN];                  /* buttons: y                      */

    /* sprites, back to front (made in this order, so drawn in it) */
    sprite_t *panel, *frame[4], *line, *title;
    sprite_t *btn[NBTN], *btn_hover[NBTN], *label[NBTN];
} m = { .hover = -1 };

static int16_t max16(int16_t a, int16_t b) { return a > b ? a : b; }

static sprite_img_t *label_img(int i)
{
    switch (btn_action[i]) {
    case MENU_SAVE:    return assets.menu_save;
    case MENU_LOAD:    return assets.menu_load;
    case MENU_RESTART: return assets.menu_restart;
    default:           return assets.menu_draw;
    }
}

void menu_init(uint16_t screen_w, uint16_t screen_h)
{
    if (m.made) return;
    m.made = true;

    /* sizes from the texts, so new or longer labels just fit */
    int16_t tw = (int16_t)sprite_img_w(assets.menu_title);
    int16_t th = (int16_t)sprite_img_h(assets.menu_title);
    int16_t lw = 0, lh = 0;
    for (int i = 0; i < NBTN; i++) {
        lw = max16(lw, (int16_t)sprite_img_w(label_img(i)));
        lh = max16(lh, (int16_t)sprite_img_h(label_img(i)));
    }
    m.bw = max16(MIN_BTN_W, (int16_t)(lw + 2 * BTN_PAD_X));
    m.bw = max16(m.bw, tw);
    m.bh = (int16_t)(lh + 2 * BTN_PAD_Y);

    m.pw = (int16_t)(m.bw + 2 * PAD);
    m.ph = (int16_t)(PAD + th + GAP + 1 + GAP + NBTN * m.bh + (NBTN - 1) * GAP + PAD);

    /* centred */
    m.px = (int16_t)(((int32_t)screen_w - m.pw) / 2);
    m.py = (int16_t)(((int32_t)screen_h - m.ph) / 2);
    if (m.px < BORDER) m.px = BORDER;
    if (m.py < BORDER) m.py = BORDER;

    int16_t x0 = m.px, y0 = m.py;
    int16_t line_y = (int16_t)(y0 + PAD + th + GAP);
    m.bx = (int16_t)(x0 + PAD);
    for (int i = 0; i < NBTN; i++)
        m.by[i] = (int16_t)(line_y + 1 + GAP + i * (m.bh + GAP));

    /* images: one per size and colour, shared where they can be */
    sprite_img_t *panel = sprite_img_solid((uint16_t)m.pw, (uint16_t)m.ph, COL_PANEL);
    sprite_img_t *edge_h = sprite_img_solid((uint16_t)(m.pw + 2 * BORDER), BORDER, COL_BORDER);
    sprite_img_t *edge_v = sprite_img_solid(BORDER, (uint16_t)m.ph, COL_BORDER);
    sprite_img_t *line   = sprite_img_solid((uint16_t)m.bw, 1, COL_LINE);
    sprite_img_t *button = sprite_img_solid((uint16_t)m.bw, (uint16_t)m.bh, COL_BUTTON);
    sprite_img_t *hover  = sprite_img_solid((uint16_t)m.bw, (uint16_t)m.bh, COL_HOVER);

    /* sprites, back to front, all hidden */
    m.panel    = sprite_new(panel, x0, y0, false);
    m.frame[0] = sprite_new(edge_h, (int16_t)(x0 - BORDER), (int16_t)(y0 - BORDER), false);
    m.frame[1] = sprite_new(edge_h, (int16_t)(x0 - BORDER), (int16_t)(y0 + m.ph), false);
    m.frame[2] = sprite_new(edge_v, (int16_t)(x0 - BORDER), y0, false);
    m.frame[3] = sprite_new(edge_v, (int16_t)(x0 + m.pw), y0, false);
    m.line     = sprite_new(line, m.bx, line_y, false);
    m.title    = sprite_new(assets.menu_title, (int16_t)(x0 + (m.pw - tw) / 2),
                            (int16_t)(y0 + PAD), false);

    for (int i = 0; i < NBTN; i++) {
        sprite_img_t *l = label_img(i);
        m.btn[i]       = sprite_new(button, m.bx, m.by[i], false);
        m.btn_hover[i] = sprite_new(hover, m.bx, m.by[i], false);
        m.label[i]     = sprite_new(l, (int16_t)(m.bx + (m.bw - (int16_t)sprite_img_w(l)) / 2),
                                    (int16_t)(m.by[i] + (m.bh - (int16_t)sprite_img_h(l)) / 2),
                                    false);
    }
    LOG_INFO("menu: %dx%d at (%d,%d)", m.pw, m.ph, m.px, m.py);
}

/* Shows or hides everything; the highlight follows m.hover. */
static void show_all(bool on)
{
    sprite_show(m.panel, on);
    for (int i = 0; i < 4; i++) sprite_show(m.frame[i], on);
    sprite_show(m.line, on);
    sprite_show(m.title, on);
    for (int i = 0; i < NBTN; i++) {
        sprite_show(m.btn[i],       on && i != m.hover);
        sprite_show(m.btn_hover[i], on && i == m.hover);
        sprite_show(m.label[i],     on);
    }
}

static void set_hover(int i)
{
    if (i == m.hover) return;
    m.hover = i;
    if (m.open) show_all(true);
}

/* Which button (cx, cy) is on, -1 if none. */
static int button_at(int32_t cx, int32_t cy)
{
    if (cx < m.bx || cx >= m.bx + m.bw) return -1;
    for (int i = 0; i < NBTN; i++)
        if (cy >= m.by[i] && cy < m.by[i] + m.bh) return i;
    return -1;
}

void menu_open(void)
{
    if (m.open) return;
    m.open  = true;
    m.hover = -1;
    show_all(true);
}

void menu_close(void)
{
    if (!m.open) return;
    m.open = false;
    show_all(false);
}

bool menu_is_open(void) { return m.open; }

menu_action_t menu_frame(const input_t *in, int32_t cx, int32_t cy)
{
    if (!m.open) return MENU_NONE;

    if (key_pressed(in, KEY_ESCAPE)) return MENU_CLOSE;

    /* the mouse picks when it moves (so the arrow keys aren't overruled
     * by a cursor that just sits on another button) */
    bool moved = cx != m.last_cx || cy != m.last_cy;
    m.last_cx = cx;
    m.last_cy = cy;
    if (moved) set_hover(button_at(cx, cy));

    if (key_pressed(in, KEY_DOWN)) set_hover(m.hover < 0 ? 0 : (m.hover + 1) % NBTN);
    if (key_pressed(in, KEY_UP))   set_hover(m.hover <= 0 ? NBTN - 1 : m.hover - 1);

    if (mouse_pressed(in, MOUSE_LEFT)) {
        int i = button_at(cx, cy);          /* only a click on a button counts */
        if (i >= 0) return btn_action[i];
    }
    if (key_pressed(in, KEY_ENTER) && m.hover >= 0) return btn_action[m.hover];
    return MENU_NONE;
}

const char *menu_action_name(menu_action_t a)
{
    switch (a) {
    case MENU_CLOSE:   return "close";
    case MENU_SAVE:    return "save";
    case MENU_LOAD:    return "load";
    case MENU_RESTART: return "restart room";
    case MENU_DRAW:    return "draw";
    default:           return "none";
    }
}
