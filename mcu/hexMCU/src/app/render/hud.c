/* hud.c - the in-game HUD (see hud.h). */

#define LOG_TAG "hud"

#include "hud.h"
#include "sprite.h"
#include "assets.h"
#include "log.h"
#include "platform.h"                       /* plat_millis(): HUD_DEMO */

/* ---- look ------------------------------------------------------------------ */
#define BAR_W        6      /* pixels                                        */
#define BAR_H        120
#define BAR_GAP      3      /* between the two bars                          */
#define BAR_MARGIN   4      /* from the screen's right edge (the bottom edge
                               is 0: the fill hides below it)                */
#define COL_EMPTY    RGB565( 60,  60,  60)  /* dark grey, behind both       */
#define COL_HEALTH   RGB565(220,  40,  40)  /* red                          */
#define COL_FUEL     RGB565(255, 140,   0)  /* orange                       */

#define TITLE_Y      16     /* the test sentence, centred near the top       */

/* ---- state ------------------------------------------------------------------ */
typedef struct {
    sprite_t *empty, *fill;
    int16_t   x;
    uint8_t   percent;
} bar_t;

static struct {
    bool          made;
    bool          shown;
    uint16_t      sh;                   /* screen height                     */
    bar_t         health, fuel;
    sprite_t     *title;
    sprite_ani_t *spinner;              /* only if sprites/spinner.spr is on the card */
} h = { .shown = true, .health.percent = 100, .fuel.percent = 100 };

/* Where a bar's sprites go for its percentage. The fill is the whole bar,
 * with its top at (screen bottom - the part that should show): the rest of
 * it is below the screen and isn't drawn. At 0 % it's hidden instead of
 * placed just off the screen. */
static void bar_place(const bar_t *b)
{
    int16_t top   = (int16_t)(h.sh - BAR_H);
    int16_t shown = (int16_t)((BAR_H * b->percent + 50u) / 100u);     /* pixels, rounded */
    sprite_set(b->empty, b->x, top, h.shown);
    sprite_set(b->fill,  b->x, (int16_t)(h.sh - shown), h.shown && shown > 0);
}

static void bar_set(bar_t *b, unsigned percent)
{
    if (percent > 100u) percent = 100u;
    if (b->percent == percent) return;
    b->percent = (uint8_t)percent;
    bar_place(b);                       /* sprite_poll() sends the move */
}

/* ---- public ------------------------------------------------------------------ */
void hud_init(uint16_t screen_w, uint16_t screen_h)
{
    if (h.made) return;
    h.made = true;
    h.sh   = screen_h;

    /* test: a sentence centred near the top */
    int32_t w = sprite_img_w(assets.title);
    int32_t x = w < screen_w ? (screen_w - w) / 2 : 0;
    h.title = sprite_new(assets.title, (int16_t)x, TITLE_Y, true);

    /* demo: an animation from the card, top-left, 4 frames a second */
    h.spinner = sprite_ani_file("sprites/spinner.spr", 250, 8, 8, true);

    /* the bars: health against the right edge, fuel to its left. Empty
     * bars first, so the fills are drawn on top of them. */
    sprite_img_t *empty  = sprite_img_solid(BAR_W, BAR_H, COL_EMPTY);
    sprite_img_t *health = sprite_img_solid(BAR_W, BAR_H, COL_HEALTH);
    sprite_img_t *fuel   = sprite_img_solid(BAR_W, BAR_H, COL_FUEL);
    h.health.x = (int16_t)(screen_w - BAR_MARGIN - BAR_W);
    h.fuel.x   = (int16_t)(h.health.x - BAR_GAP - BAR_W);
    h.health.empty = sprite_new(empty, 0, 0, false);
    h.fuel.empty   = sprite_new(empty, 0, 0, false);
    h.health.fill  = sprite_new(health, 0, 0, false);
    h.fuel.fill    = sprite_new(fuel, 0, 0, false);
    bar_place(&h.health);
    bar_place(&h.fuel);
    LOG_INFO("bars %ux%u px at the bottom right%s", BAR_W, BAR_H,
             HUD_DEMO ? " (HUD_DEMO: sliding by themselves)" : "");
}

void hud_visible(bool on)
{
    if (h.shown == on) return;
    h.shown = on;
    sprite_show(h.title, on);
    sprite_ani_show(h.spinner, on);
    bar_place(&h.health);
    bar_place(&h.fuel);
}

void health_bar_set(unsigned percent) { bar_set(&h.health, percent); }
void fuel_bar_set(unsigned percent)   { bar_set(&h.fuel, percent); }

/* HUD_DEMO: both bars run 100 -> 0 -> 100 every 4 s (real time), fuel a
 * second behind health. */
#define DEMO_PERIOD_MS 4000u

static unsigned demo_level(uint32_t t)          /* 100 -> 0 -> 100 over a period */
{
    uint32_t half = DEMO_PERIOD_MS / 2u, p = t % DEMO_PERIOD_MS;
    return p < half ? 100u - 100u * p / half : 100u * (p - half) / half;
}

void hud_frame(void)
{
#if HUD_DEMO
    uint32_t t = plat_millis();
    health_bar_set(demo_level(t));
    fuel_bar_set(demo_level(t + DEMO_PERIOD_MS - 1000u));
#endif
}