#define LOG_TAG "disp"

#include "display_host.h"
#include "log.h"

#include <SDL3/SDL.h>
#include <string.h>

#define FRAME_NS (1000000000ull / 30u)   /* present at 30 Hz */

static SDL_Window   *win;
static SDL_Renderer *ren;
static SDL_Texture  *tex;
static uint32_t      fb[DISPLAY_W * DISPLAY_H];
static uint64_t      next_frame_ns;

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

bool display_init(const char *title, int scale)
{   
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

    /* Draw in 640x480 coordinates; SDL scales up by whole multiples. */
    SDL_SetRenderLogicalPresentation(ren, DISPLAY_W, DISPLAY_H,
                                     SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);

    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_XRGB8888,
                            SDL_TEXTUREACCESS_STREAMING, DISPLAY_W, DISPLAY_H);
    if (!tex) {
        LOG_ERR("texture: %s", SDL_GetError());
        return false;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    memset(fb, 0, sizeof fb);
    LOG_INFO("display up: %dx%d, video driver %s, renderer %s",
             DISPLAY_W, DISPLAY_H, SDL_GetCurrentVideoDriver(),
             SDL_GetRendererName(ren));
    return true;
}

uint32_t *display_framebuffer(void)
{
    return fb;
}

bool display_load_bmp(const char *path)
{
    SDL_Surface *raw = SDL_LoadBMP(path);
    if (!raw) {
        LOG_ERR("load %s: %s", path, SDL_GetError());
        return false;
    }

    /* Whatever the BMP's format, convert it to ours. */
    SDL_Surface *s = SDL_ConvertSurface(raw, SDL_PIXELFORMAT_XRGB8888);
    SDL_DestroySurface(raw);
    if (!s) {
        LOG_ERR("convert: %s", SDL_GetError());
        return false;
    }

    /* Copy row by row: the surface pitch can be wider than w*4. */
    int w = s->w < DISPLAY_W ? s->w : DISPLAY_W;
    int h = s->h < DISPLAY_H ? s->h : DISPLAY_H;
    memset(fb, 0, sizeof fb);
    for (int y = 0; y < h; y++) {
        const uint8_t *row = (const uint8_t *)s->pixels + (size_t)y * (size_t)s->pitch;
        memcpy(&fb[(size_t)y * DISPLAY_W], row, (size_t)w * 4u);
    }

    LOG_INFO("loaded %s (%dx%d)", path, s->w, s->h);
    SDL_DestroySurface(s);
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

    SDL_UpdateTexture(tex, NULL, fb, DISPLAY_W * (int)sizeof fb[0]);
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