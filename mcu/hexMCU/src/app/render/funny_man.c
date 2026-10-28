/* player.c - the stand-in character (see player.h). */

#include <string.h>

#include "funny_man.h"
#include "fpga.h"
#include "hud.h"

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
    default:  return HUD_CLEAR;                 /* ' ': see-through     */
    }
}

static uint8_t      px[PLAYER_SIZE * PLAYER_SIZE * PX_BYTES];
static hud_image_t  img;
static hud_sprite_t spr;
static int16_t      x, y;

/* Each art pixel becomes a PLAYER_SCALE x PLAYER_SCALE block. */
static void make_image(void)
{
    for (uint16_t ay = 0; ay < PLAYER_ART; ay++)
        for (uint16_t ax = 0; ax < PLAYER_ART; ax++) {
            char c = ax < strlen(art[ay]) ? art[ay][ax] : ' ';
            uint16_t colour = palette(c);
            for (uint16_t sy = 0; sy < PLAYER_SCALE; sy++)
                for (uint16_t sx = 0; sx < PLAYER_SCALE; sx++)
                    hud_set_px(px, PLAYER_SIZE, (uint16_t)(ax * PLAYER_SCALE + sx),
                               (uint16_t)(ay * PLAYER_SCALE + sy), colour);
        }
}

void player_init(int16_t px0, int16_t py0)
{
    static bool made;
    x = px0;
    y = py0;
    if (!made) {
        made = true;
        make_image();
        hud_image_init(&img, px, PLAYER_SIZE, PLAYER_SIZE);
        hud_sprite_init(&spr, &img, x, y, true);
    }
    hud_sprite_move(&spr, x, y);
}

static int16_t clamp16(int32_t v, int32_t lo, int32_t hi)
{
    return (int16_t)(v < lo ? lo : v > hi ? hi : v);
}

void player_frame(const input_t *keys, uint32_t n)
{
    int32_t dx = 0, dy = 0;
    if (key_held(keys, KEY_A) || key_held(keys, KEY_LEFT))  dx--;
    if (key_held(keys, KEY_D) || key_held(keys, KEY_RIGHT)) dx++;
    if (key_held(keys, KEY_W) || key_held(keys, KEY_UP))    dy--;
    if (key_held(keys, KEY_S) || key_held(keys, KEY_DOWN))  dy++;
    if (!dx && !dy) return;

    if (n > 4) n = 4;           /* after a long stall, don't jump far */
    int32_t step = PLAYER_SPEED * (int32_t)n;
    const fpga_info_t *scr = fpga_link_info();
    x = clamp16(x + dx * step, 0, (int32_t)scr->width  - (int32_t)PLAYER_SIZE);
    y = clamp16(y + dy * step, 0, (int32_t)scr->height - (int32_t)PLAYER_SIZE);
    hud_sprite_move(&spr, x, y);            /* hud_poll() sends it */
}

int16_t player_x(void) { return x; }
int16_t player_y(void) { return y; }