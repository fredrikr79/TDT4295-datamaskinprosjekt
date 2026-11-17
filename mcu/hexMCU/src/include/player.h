#pragma once

#include "fpga.h"
#include "rooms.h"
#include "collision.h"
#include <stdint.h>
#include <math.h>

// Steps per second
// The player takes PHYS_HZ steps a second of real time (game.c), whatever
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

// Gravity
#define GRAVITY_AIR ACCEL(0.1)
#define GRAVITY_AIR_FALL ACCEL(0.6)
#define GRAVITY_WATER ACCEL(0.05)
#define GRAVITY_LAVA ACCEL(0.025)
#define GRAVITY_ACID GRAVITY_WATER
#define MAX_GRAVITY_AIR SPEED(11)
#define MAX_GRAVITY_WATER SPEED(0.875)
#define MAX_GRAVITY_LAVA SPEED(0.375)
#define MAX_GRAVITY_ACID MAX_GRAVITY_WATER
#define PLAYER_SAND_SLIDE 2

// Jumping
#define JUMP_AIR SPEED(-2.5)
#define JUMP_WATER SPEED(-1)
#define JUMP_LAVA SPEED(-0.25)
#define JUMP_ACID SPEED(-0.75)

// Flying
#define MAX_FLY_TIME STEPS(10)
#define FLY_FORCE ACCEL(-1)
#define MAX_FLY_SPEED SPEED(-1.5)
#define FLY_REGEN_SPEED MAX_FLY_TIME

// Sideways movement
#define PLAYER_MAX_SPEED SPEED(2)
#define PLAYER_STEP_ALLOWANCE 3

// Healing
// Health counts in steps' worth of damage: a damage of 1 is taken every
// step, so health scales with the step rate too (STEPS), and a fight lasts
// as long in seconds as it did. The amounts per step stay as they are.
#define PLAYER_MAX_HEALTH STEPS(500)
#define PLAYER_DAMAGE_TO_HEAL_DELAY STEPS(250)
#define PLAYER_HEAL_DELAY 1
#define PLAYER_HEAL_AMOUNT 5

// Damage
#define PLAYER_MAX_BREATH STEPS(250)
#define PLAYER_BURN_TIME STEPS(125)

// Friction
#define FRICTION_AIR FRICTION(0.9)
#define FRICTION_GROUND_WATER FRICTION(0.7)
#define FRICTION_LAVA FRICTION(0.4)

#define PLAYER_SIZE_X 4
#define PLAYER_SIZE_Y 8

#define PLAYER_PORTAL_EXHAUSTION STEPS(4)

typedef struct
{
    float x;
    float y;
    float velocity_x;
    float velocity_y;

    uint16_t health;
    uint8_t fly_meter;
    uint16_t breath_meter;
    uint16_t burn_timer;
    uint8_t still_in_portal;
} player_t;

void update_player(
    player_t *player,
    uint8_t *box, velocity_box_t velocity_box
);
void move_left();
void move_right();
void move_stop();
void start_jump();

void player_init();
void player_set_default(player_t *player);

/* The player's sprite (the picture is assets.player). player_sprite_init()
 * once, after assets_init(); update_player() and player_init() then keep
 * it on the player, and sprite_poll() sends it. */
void player_sprite_init(int16_t x, int16_t y);
void player_show(bool visible);         /* hidden while drawing */