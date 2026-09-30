#define LOG_TAG "disp"

/* ======================================================================
 * display_host.c - SIM + HUD layers in an SDL window.
 *
 * The layers are plain arrays that anyone may write at any time. Once per
 * frame (30 Hz) display_poll() composites them into one RGB565 frame --
 * HUD pixel, unless it is DISPLAY_HUD_CLEAR, then the SIM pixel -- and
 * hands that to SDL as an RGB565 texture. 640x360 is ~230k pixels, cheap
 * enough to redo every frame, so nothing tracks what changed.
 * ====================================================================== */

#include "display_host.h"
#include "log.h"

#include <SDL3/SDL.h>
#include <string.h>

#define FRAME_NS (1000000000ull / 30u)   /* present at 30 Hz */
#define NPIX     ((size_t)DISPLAY_W * DISPLAY_H)

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static uint64_t      next_frame_ns;

static uint16_t layers[2][NPIX];         /* [DISPLAY_SIM], [DISPLAY_HUD] */
static uint16_t frame[NPIX];             /* composited, what SDL gets    */

static void sdl_log_to_ours(void *ud, int category,
                            SDL_LogPriority pri, const char *msg)
{
    (void)ud; (void)category;
    int level;
    switch (pri) {
    case SDL_LOG_PRIORITY_CRITICAL:
    case SDL_LOG_PRIORITY_ERROR: level = LOG_LEVEL_ERR;   break;
    case SDL_LOG_PRIORITY_WARN:  level = LOG_LEVEL_WARN;  break;
    case SDL_LOG_PRIORITY_INFO:  level = LOG_LEVEL_INFO;  break;
    default:                     level = LOG_LEVEL_DEBUG; break;
    }
    log_emit(level, "sdl", "%s", msg);
}

/* ---- layers ----------------------------------------------------------- */
uint16_t *display_layer(display_layer_t layer)
{
    return layers[layer == DISPLAY_HUD ? 1 : 0];
}

void display_clear(display_layer_t layer)
{
    uint16_t *p = display_layer(layer);
    uint16_t  v = (layer == DISPLAY_HUD) ? DISPLAY_HUD_CLEAR : 0x0000u;
    for (size_t i = 0; i < NPIX; i++) p[i] = v;
}

static void composite(void)
{
    const uint16_t *sim = layers[DISPLAY_SIM];
    const uint16_t *hud = layers[DISPLAY_HUD];
    for (size_t i = 0; i < NPIX; i++)
        frame[i] = (hud[i] == DISPLAY_HUD_CLEAR) ? sim[i] : hud[i];
}

/* ---- window ----------------------------------------------------------- */
bool display_init(const char *title, int scale)
{
    /* First, so the layers are valid even if there is no window. */
    display_clear(DISPLAY_SIM);
    display_clear(DISPLAY_HUD);

    SDL_SetLogOutputFunction(sdl_log_to_ours, NULL);
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");  /* Ctrl-C stays io_stdio's */
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        LOG_ERR("SDL_Init: %s", SDL_GetError());
        return false;
    }
    if (!SDL_CreateWindowAndRenderer(title, DISPLAY_W * scale, DISPLAY_H * scale,
                                     SDL_WINDOW_RESIZABLE, &win, &ren)) {
        LOG_ERR("window: %s", SDL_GetError());
        return false;
    }

    /* Draw in DISPLAY_W x DISPLAY_H coordinates; SDL scales up by whole
     * multiples and letterboxes the rest. */
    SDL_SetRenderLogicalPresentation(ren, DISPLAY_W, DISPLAY_H,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565,
                            SDL_TEXTUREACCESS_STREAMING, DISPLAY_W, DISPLAY_H);
    if (!tex) {
        LOG_ERR("texture: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    LOG_INFO("display up: %dx%d RGB565, SIM + HUD (clear = 0x%04X), "
             "video driver %s, renderer %s", DISPLAY_W, DISPLAY_H,
             DISPLAY_HUD_CLEAR, SDL_GetCurrentVideoDriver(),
             SDL_GetRendererName(ren));
    return true;
}

bool display_poll(void)
{
    if (!ren) return true;                  /* headless: display_init failed */

    uint64_t now = SDL_GetTicksNS();
    if (now < next_frame_ns) return true;   /* not time for a frame yet */
    next_frame_ns = now + FRAME_NS;

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) return false;
        if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) return false;
    }

    composite();
    SDL_UpdateTexture(tex, NULL, frame, DISPLAY_W * (int)sizeof frame[0]);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);   /* letterbox colour */
    SDL_RenderClear(ren);
    SDL_RenderTexture(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    return true;
}

void display_shutdown(void)
{
    if (tex) SDL_DestroyTexture(tex);
    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    SDL_Quit();
}