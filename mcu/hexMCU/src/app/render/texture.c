/* ======================================================================
 * texture.c - texture in the low bits of cells (see texture.h).
 * Portable: the world seed goes through storage.h.
 * ====================================================================== */
#define LOG_TAG "texture"

/* A whole room is 14400 cells: this file is worth optimising even in a
 * Debug build (-O0 made a room take ~100 ms on the board). */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("O2")
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "texture.h"
#include "fpga.h"
#include "storage.h"
#include "platform.h"
#include "frame_sync.h"
#include "log.h"

/* ---- what each material's low bits are ----------------------------------------
 * How a material's texture looks. The noise is a mix of two kinds, both
 * 0..255 per cell:
 *   smooth  value noise: random values every `scale` cells, blended
 *           smoothly in between, plus a second layer at half the scale for
 *           detail (two octaves). Big soft shapes; scale 0: none.
 *   grain   a random value per cell: speckle.
 * mixed smooth : grain by their weights, stretched around the middle by
 * `contrast` / 16 (averaging pulls values towards the middle; more than 16
 * brings the extremes back), then cut into `levels` shades starting at
 * `first`, written into the bits of `mask`. */
typedef struct {
    uint8_t mask;           /* the low bits that are texture                    */
    uint8_t first, levels;  /* the shades used: first .. first + levels - 1     */
    uint8_t scale;          /* cells between smooth-noise points (0: no smooth) */
    uint8_t smooth, grain;  /* weights of the two                               */
    uint8_t contrast;       /* 16 = as is                                       */
    uint8_t salt;           /* so materials get different patterns              */
} tex_t;

/* Sand: grains. Mostly per-cell speckle over faint patches. Shades 1..15:
 * 0 is kept for "not textured yet", so a saved grain is never redone. */
static const tex_t tex_sand  = { .mask = 0xF, .first = 1, .levels = 15,
                                 .scale = 6, .smooth = 1, .grain = 3,
                                 .contrast = 20, .salt = 2 };
/* Stone: blotches with some grit, all 8 shades of xxx. */
static const tex_t tex_stone = { .mask = 0x7, .first = 0, .levels = 8,
                                 .scale = 8, .smooth = 3, .grain = 1,
                                 .contrast = 24, .salt = 3 };

/* ---- what a room file keeps ------------------------------------------------------
 * Low bits saved, per material: the rest is made again on load (texture),
 * or is the FPGA's and starts over at 0 (liquids' speed). Stone and igneous
 * rock share MAT_STONE, see texture_strip(). Materials not listed (not in
 * use yet): kept. */
static const uint8_t save_keep[16] = {
    [MAT_AIR]       = 0x0,      /* nothing: air is just air                   */
    [MAT_STONE]     = 0x8,      /* stone: the 1 in 1xxx (igneous: see below)  */
    [MAT_SAND]      = 0xF,      /* texture, saved: grains keep their shade    */
    [MAT_WATER]     = 0x0,      /* the FPGA's: how fast it moves              */
    [MAT_LAVA]      = 0x0,
    [MAT_ACID]      = 0x0,
    [MAT_LIFE]      = 0xF,      /* alive or not                               */
    [MAT_EXPLOSIVE] = 0xF,      /* its power                                  */
    [MAT_COUNT ... 15] = 0xF,
};

/* The texture of this cell's material, or NULL: its low bits aren't ours. */
static const tex_t *tex_of(uint8_t cell)
{
    switch (SIM_MAT(cell)) {
    case MAT_SAND:  return &tex_sand;
    case MAT_STONE: return (cell & SIM_STONE_BIT) ? &tex_stone : NULL;    /* igneous: FPGA's */
    default:        return NULL;
    }
}

uint8_t texture_strip(uint8_t cell)
{
    uint8_t keep = save_keep[SIM_MAT(cell)];
    if (sim_is_igneous(cell)) keep = 0xF;       /* its thickness, all of it */
    return (uint8_t)(cell & (0xF0u | keep));
}

void texture_strip_room(uint8_t *cells, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) cells[i] = texture_strip(cells[i]);
}

/* ---- noise ------------------------------------------------------------------------- */
/* A good 32-bit mixer (Chris Wellons' lowbias32): every input bit changes
 * about half the output bits. */
static uint32_t mix(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static uint32_t hash2(uint32_t x, uint32_t y, uint32_t seed)
{
    return mix(seed ^ mix(x * 0x9E3779B1u ^ mix(y * 0x85EBCA77u)));
}

/* t 0..256 -> 0..256 along 3t^2 - 2t^3: no hard edges where cells of the
 * lattice meet. */
static uint32_t smoothstep256(uint32_t t)
{
    return (t * t * (768u - 2u * t)) >> 16;
}

/* The four lattice values around a cell. Every cell between the same four
 * lattice points needs the same ones, so the last few are kept: going
 * along a row, s - 1 of every s cells find theirs here instead of
 * hashing four times. One entry per (scale, seed) slot, since the
 * octaves and materials take turns. */
typedef struct { uint32_t gx, gy, s, seed; uint8_t v[4]; bool ok; } corners_t;
static corners_t corner_cache[8];

static const uint8_t *corners(uint32_t gx, uint32_t gy, uint32_t s, uint32_t seed)
{
    corners_t *c = &corner_cache[(s + seed) & 7u];
    if (!c->ok || c->gx != gx || c->gy != gy || c->s != s || c->seed != seed) {
        c->gx = gx; c->gy = gy; c->s = s; c->seed = seed; c->ok = true;
        c->v[0] = (uint8_t)hash2(gx, gy, seed);
        c->v[1] = (uint8_t)hash2(gx + 1u, gy, seed);
        c->v[2] = (uint8_t)hash2(gx, gy + 1u, seed);
        c->v[3] = (uint8_t)hash2(gx + 1u, gy + 1u, seed);
    }
    return c->v;
}

/* Value noise, 0..255: random values on a lattice every s cells, blended. */
static uint32_t value_noise(uint32_t x, uint32_t y, uint32_t s, uint32_t seed)
{
    uint32_t gx = x / s, gy = y / s;
    uint32_t tx = smoothstep256((x % s) * 256u / s), ty = smoothstep256((y % s) * 256u / s);
    const uint8_t *v = corners(gx, gy, s, seed);
    uint32_t a = v[0], b = v[1], c = v[2], d = v[3];
    uint32_t top = a * (256u - tx) + b * tx;            /* x 256 */
    uint32_t bot = c * (256u - tx) + d * tx;
    return (top * (256u - ty) + bot * ty) >> 16;
}

/* The two octaves of the smooth noise: scale and seed of each. */
#define OCTAVE2_SALT 0x5bd1e995u
static uint32_t octave_scale(const tex_t *t, unsigned o)
{
    return o == 0 ? t->scale : (t->scale >= 2u ? t->scale / 2u : 1u);
}
static uint32_t octave_seed(uint32_t seed, unsigned o)
{
    return o == 0 ? seed : seed ^ OCTAVE2_SALT;
}

/* The shade (0 .. levels-1, + first) of the cell at (x, y), given its
 * smooth noise n = 2 * octave 1 + octave 2 (unused if the texture has none)
 * and the texture's seed. */
static uint8_t finish(const tex_t *t, uint32_t n, uint32_t x, uint32_t y, uint32_t seed)
{
    int32_t v = 0, wsum = 0;
    if (t->scale && t->smooth) {
        v    += (int32_t)(n / 3u) * t->smooth;
        wsum += t->smooth;
    }
    if (t->grain) {
        v    += (int32_t)(hash2(x, y, seed ^ 0x27d4eb2fu) & 255u) * t->grain;
        wsum += t->grain;
    }
    v = wsum ? v / wsum : 128;
    v = 128 + (v - 128) * t->contrast / 16;                             /* stretch */
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    return (uint8_t)(t->first + (uint32_t)v * t->levels / 256u);
}

/* One cell, from scratch (drawing: a few cells at a time). */
static uint8_t shade(const tex_t *t, uint32_t x, uint32_t y, uint32_t seed)
{
    seed = mix(seed + t->salt);
    uint32_t n = 0;
    if (t->scale && t->smooth)
        n = 2u * value_noise(x, y, octave_scale(t, 0), octave_seed(seed, 0))
               + value_noise(x, y, octave_scale(t, 1), octave_seed(seed, 1));
    return finish(t, n, x, y, seed);
}

/* ---- seeds -------------------------------------------------------------------------- */
#define SEED_FILE "save/seed"

static uint32_t world_seed, room_seed;
static bool     have_world;

/* Something different every start: the microsecond clock when the first
 * room loads (the handshake, the card and USB make that vary) and the
 * SYNCs so far. */
static uint32_t new_seed(void)
{
    return mix(plat_micros() ^ mix(frame_sync_count() + 0x6a09e667u));
}

/* The world seed from save/seed. No file on a card that's there: a new
 * world (the first start, or the saves were deleted), so a new seed, kept
 * in the file. No card: one seed for as long as the MCU runs. */
static void world_seed_check(void)
{
    char buf[24];
    uint32_t got = 0;
    storage_status_t st = storage_read(SEED_FILE, 0, buf, sizeof buf - 1u, &got);
    if (st == STORAGE_OK) {
        buf[got] = '\0';
        uint32_t s = (uint32_t)strtoul(buf, NULL, 0);
        if (!have_world || s != world_seed)
            LOG_INFO("world seed 0x%08lX (%s)", (unsigned long)s, SEED_FILE);
        world_seed = s;
        have_world = true;
        return;
    }
    if (st != STORAGE_NOT_FOUND) {                  /* no card or so: keep this run's */
        if (!have_world) {
            world_seed = new_seed();
            have_world = true;
            LOG_INFO("world seed 0x%08lX (not kept: %s)", (unsigned long)world_seed,
                     storage_status_str(st));
        }
        return;
    }
    world_seed = new_seed();
    have_world = true;
    int n = snprintf(buf, sizeof buf, "0x%08lX\n", (unsigned long)world_seed);
    st = storage_write(SEED_FILE, buf, (uint32_t)n);
    LOG_INFO("new world seed 0x%08lX%s%s", (unsigned long)world_seed,
             st == STORAGE_OK ? ", kept in " SEED_FILE : ", not kept: ",
             st == STORAGE_OK ? "" : storage_status_str(st));
}

/* ---- a whole room at once ------------------------------------------------------------
 * The same numbers as shade(), worked out faster: per texture and octave,
 * the lattice values are hashed once for the room, and each column's and
 * row's lattice cell and fraction go in tables, so a cell is a few
 * lookups and blends instead of hashes and divisions. ~3 KB per octave,
 * two octaves for each of the two textures. */
#define FAST_W_MAX   256u               /* the grid is 160 x 90 */
#define FAST_H_MAX   256u
#define FAST_LAT_MAX 2048u              /* lattice values per octave (scale 3: 55 x 32) */

typedef struct {
    uint32_t lw;                        /* lattice values per row            */
    uint8_t  lat[FAST_LAT_MAX];
    uint8_t  gx[FAST_W_MAX], tx[FAST_W_MAX];    /* per column: lattice cell, fraction */
    uint8_t  gy[FAST_H_MAX], ty[FAST_H_MAX];    /* per row                            */
} octave_t;

typedef struct {
    uint32_t seed;                      /* mix(room seed + salt)             */
    bool     ready;                     /* built for this room               */
    octave_t oct[2];
} plane_t;

#define FAST_TEX 2u
static const tex_t *const fast_tex[FAST_TEX] = { &tex_sand, &tex_stone };
static plane_t planes[FAST_TEX];

static bool octave_build(octave_t *o, uint32_t s, uint32_t seed, unsigned w, unsigned h)
{
    uint32_t lw = (w - 1u) / s + 2u, lh = (h - 1u) / s + 2u;
    if (w > FAST_W_MAX || h > FAST_H_MAX || lw * lh > FAST_LAT_MAX) return false;
    o->lw = lw;
    for (uint32_t gy = 0; gy < lh; gy++)
        for (uint32_t gx = 0; gx < lw; gx++)
            o->lat[gy * lw + gx] = (uint8_t)hash2(gx, gy, seed);
    /* fractions stay below 256: (x % s) * 256 / s <= 255, and smoothstep
     * of that too, so they fit a byte */
    for (uint32_t x = 0; x < w; x++) {
        o->gx[x] = (uint8_t)(x / s);
        o->tx[x] = (uint8_t)smoothstep256((x % s) * 256u / s);
    }
    for (uint32_t y = 0; y < h; y++) {
        o->gy[y] = (uint8_t)(y / s);
        o->ty[y] = (uint8_t)smoothstep256((y % s) * 256u / s);
    }
    return true;
}

/* = value_noise() at (x, y), from the tables */
static uint32_t octave_at(const octave_t *o, unsigned x, unsigned y)
{
    const uint8_t *r0 = &o->lat[o->gy[y] * o->lw + o->gx[x]], *r1 = r0 + o->lw;
    uint32_t tx = o->tx[x], ty = o->ty[y];
    uint32_t top = r0[0] * (256u - tx) + r0[1] * tx;
    uint32_t bot = r1[0] * (256u - tx) + r1[1] * tx;
    return (top * (256u - ty) + bot * ty) >> 16;
}

/* The tables for texture t, built the first time this room needs them.
 * NULL: too big for them (then a cell is done from scratch). */
static const plane_t *plane_of(const tex_t *t, unsigned w, unsigned h)
{
    unsigned i = 0;
    while (i < FAST_TEX && fast_tex[i] != t) i++;
    if (i == FAST_TEX) return NULL;
    plane_t *p = &planes[i];
    if (!p->ready) {
        p->seed = mix(room_seed + t->salt);
        if (t->scale && t->smooth)
            for (unsigned o = 0; o < 2u; o++)
                if (!octave_build(&p->oct[o], octave_scale(t, o), octave_seed(p->seed, o), w, h))
                    return NULL;
        p->ready = true;
    }
    return p;
}

/* ---- the hooks ---------------------------------------------------------------------- */
uint8_t texture_cell(uint8_t cell, unsigned x, unsigned y)
{
    const tex_t *t = tex_of(cell);
    if (!t) return cell;
    /* Sand from a save already has its shade (never 0, see tex_sand). */
    if (t->first && (cell & t->mask)) return cell;
    return (uint8_t)((cell & ~t->mask) | (shade(t, x, y, room_seed) & t->mask));
}

void texture_room(uint8_t *cells, unsigned w, unsigned h, int room_x, int room_y)
{
    world_seed_check();
    room_seed = hash2((uint32_t)room_x, (uint32_t)room_y, world_seed);
    for (unsigned i = 0; i < FAST_TEX; i++) planes[i].ready = false;   /* a new room's */

    uint32_t t0 = plat_micros();
    for (unsigned y = 0; y < h; y++)
        for (unsigned x = 0; x < w; x++) {
            uint8_t *c = &cells[y * w + x];
            const tex_t *t = tex_of(*c);
            if (!t || (t->first && (*c & t->mask))) continue;      /* see texture_cell() */
            const plane_t *p = plane_of(t, w, h);
            if (!p) { *c = texture_cell(*c, x, y); continue; }
            uint32_t n = 0;
            if (t->scale && t->smooth)
                n = 2u * octave_at(&p->oct[0], x, y) + octave_at(&p->oct[1], x, y);
            *c = (uint8_t)((*c & ~t->mask) | (finish(t, n, x, y, p->seed) & t->mask));
        }
    LOG_INFO("room %d, %d textured in %lu us", room_x, room_y,
             (unsigned long)(plat_micros() - t0));
}