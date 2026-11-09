/* funny_man.c - the stand-in character (see funny_man.h). */

#include <string.h>

#include "funny_man.h"
#include "fpga.h"
#include "sprite.h"

/* The drawing: one letter per pixel, looked up in palette() below.
 * Front face, top face to the back, side face to the right. Edit away --
 * every row must stay PLAYER_ART letters long. */
static const char *const art[PLAYER_ART] = {
    "   KKKKKKKKKKKKK",
    "  KLLLLLLLLLLLKK",
    " KLLLLLLLLLLLKDK",
    "KKKKKKKKKKKKKDDK",
    "KYYYYYYYYYYYKDDK",
    "KYYYYYYYYYYYKDDK",
    "KYYKKYYYKKYYKDDK",
    "KYYKKYYYKKYYKDDK",
    "KYYYYYYYYYYYKDDK",
    "KYPYYYYYYYPYKDDK",
    "KYKYYYYYYYKYKDDK",
    "KYYKKKKKKKYYKDDK",
    "KYYYYYYYYYYYKDDK",
    "KYYYYYYYYYYYKDK ",
    "KYYYYYYYYYYYKK  ",
    "KKKKKKKKKKKKK   ",
};

static uint16_t palette(char c)
{
    switch (c) {
    case 'K': return RGB565( 20,  20,  30);     /* outline, eyes, mouth */
    case 'Y': return RGB565(255, 200,  40);     /* front face           */
    case 'L': return RGB565(255, 235, 130);     /* top, in the light    */
    case 'D': return RGB565(200, 130,  20);     /* side, in the shade   */
    case 'P': return RGB565(255, 120, 140);     /* cheeks               */
    default:  return SPRITE_CLEAR;              /* ' ': see-through     */
    }
}

static sprite_t *spr;
static int16_t   x, y;

/* The image is drawn straight from the art while it uploads (no pixel
 * buffer): each art pixel becomes a PLAYER_SCALE x PLAYER_SCALE block. */
static void make_px(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    (void)ctx;
    for (uint32_t i = 0; i < len; i += PX_BYTES) {
        uint32_t p  = (offset + i) / PX_BYTES;
        uint32_t ax = (p % PLAYER_SIZE) / PLAYER_SCALE;
        uint32_t ay = (p / PLAYER_SIZE) / PLAYER_SCALE;
        char c = ax < strlen(art[ay]) ? art[ay][ax] : ' ';
        uint16_t colour = palette(c);
        buf[i]     = (uint8_t)(colour >> 8);
        buf[i + 1] = (uint8_t)colour;
    }
}

void player_init_old(int16_t px0, int16_t py0)
{
    x = px0;
    y = py0;
    if (!spr)       /* after sprite_init() */
        spr = sprite_new(sprite_img_fill(PLAYER_SIZE, PLAYER_SIZE, make_px, NULL), x, y, true);
    sprite_move(spr, x, y);
}

static int16_t clamp16(int32_t v, int32_t lo, int32_t hi)
{
    return (int16_t)(v < lo ? lo : v > hi ? hi : v);
}

/* Movement input as -127..127 per axis. Keys and the d-pad push full
 * speed; a controller's left stick anything in between. Every connected
 * controller counts, so either of two pads can steer. */
#define STICK_DEADZONE 16       /* sticks don't rest at exactly 0 */

static int32_t clamp127(int32_t v) { return v < -127 ? -127 : v > 127 ? 127 : v; }

void player_frame(const input_t *in, uint32_t n)
{
    static int32_t fx, fy;      /* sub-pixel movement left over, in 1/127 px */
    int32_t ax = 0, ay = 0;

    if (key_held(in, KEY_A) || key_held(in, KEY_LEFT))  ax -= 127;
    if (key_held(in, KEY_D) || key_held(in, KEY_RIGHT)) ax += 127;
    if (key_held(in, KEY_W) || key_held(in, KEY_UP))    ay -= 127;
    if (key_held(in, KEY_S) || key_held(in, KEY_DOWN))  ay += 127;

    for (unsigned p = 0; p < INPUT_PADS; p++) {
        const pad_t *pad = &in->pad[p];
        if (!pad->connected) continue;
        if (pad->held & PAD_LEFT)  ax -= 127;
        if (pad->held & PAD_RIGHT) ax += 127;
        if (pad->held & PAD_UP)    ay -= 127;
        if (pad->held & PAD_DOWN)  ay += 127;
        ax += pad_deadzone(pad->lx, STICK_DEADZONE);
        ay += pad_deadzone(pad->ly, STICK_DEADZONE);
    }
    ax = clamp127(ax);
    ay = clamp127(ay);
    if (!ax && !ay) {
        fx = fy = 0;
        return;
    }

    if (n > 4) n = 4;           /* after a long stall, don't jump far */
    int32_t step = PLAYER_SPEED * (int32_t)n;

    /* a stick pushed a little moves slowly: keep the fraction of a pixel
     * for the next frame instead of rounding it away */
    fx += ax * step;
    fy += ay * step;
    int32_t dx = fx / 127, dy = fy / 127;
    fx -= dx * 127;
    fy -= dy * 127;
    if (!dx && !dy) return;

    const fpga_info_t *scr = fpga_link_info();
    x = clamp16(x + dx, 0, (int32_t)scr->width  - (int32_t)PLAYER_SIZE);
    y = clamp16(y + dy, 0, (int32_t)scr->height - (int32_t)PLAYER_SIZE);
    sprite_move(spr, x, y);                 /* sprite_poll() sends it */
}

int16_t player_x(void) { return x; }
int16_t player_y(void) { return y; }