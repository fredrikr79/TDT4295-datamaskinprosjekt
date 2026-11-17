#pragma once

#include "fpga.h"
#include "rooms.h"
#include "collision.h"
#include <stdint.h>
#include <math.h>

// Steps per second
// The object takes PHYS_HZ steps a second of real time (game.c), whatever
// the FPGA's frame rate. The numbers below were tuned at TUNED_HZ steps a
// second (the emulator's old 25 Hz SYNC); these turn them into per-step
// values for PHYS_HZ, so the game plays as it was tuned:
//   SPEED(v)     cells per step        x TUNED_HZ / PHYS_HZ
//   ACCEL(a)     cells per step^2      x (TUNED_HZ / PHYS_HZ)^2
//   STEPS(n)     a time in steps       x PHYS_HZ / TUNED_HZ (rounded)
//   FRICTION(f)  a multiplier per step to the power TUNED_HZ / PHYS_HZ
// Sizes in cells (sand slide, step allowance) don't change. To retune,
// change the numbers inside the macros, as if it still ran at 25 a second.
#define PHYS_HZ 60
#define TUNED_HZ 25
#define TUNED_K ((float)TUNED_HZ / (float)PHYS_HZ)
#define SPEED(v) ((float)(v) * TUNED_K)
#define ACCEL(a) ((float)(a) * TUNED_K * TUNED_K)
#define STEPS(n) (((n) * PHYS_HZ + TUNED_HZ / 2) / TUNED_HZ)
#define FRICTION(f) powf((float)(f), TUNED_K)

#define EXPLOSIVE_SIZE_X 2
#define EXPLOSIVE_SIZE_Y 2
#define EXPLOSION_SIZE_X 1
#define EXPLOSION_SIZE_Y 1

#define GOL_PROJ_SIZE_X 3
#define GOL_PROJ_SIZE_Y 3
#define GOL_SIZE_X 5
#define GOL_SIZE_Y 5

#define PROJECTILE_TYPES 2
enum projectile_type {
    EXPLOSIVE, GOL
};

typedef struct
{
    bool active;

    float x;
    float y;
    float velocity_x;
    float velocity_y;
    float size_x;
    float size_y;

    enum projectile_type type;

    velocity_box_t velocity_box;
    uint8_t *box;
} projectile_t;

bool update_projectile(projectile_t *projectile);

projectile_t get_new_projectile(
    float x, float y,
    float vel_x, float vel_y,
    enum projectile_type type
);
void set_projectile(
    projectile_t *projectile,
    float x, float y,
    float vel_x, float vel_y
);

/* The projectile's sprite. projectile_sprite_init()
 * once, after assets_init(); update_projectile() and projectile_init() then keep
 * it on the projectile, and sprite_poll() sends it. */
void projectile_sprite_init(int16_t x, int16_t y);
void projectile_show(bool visible);         /* hidden while drawing */