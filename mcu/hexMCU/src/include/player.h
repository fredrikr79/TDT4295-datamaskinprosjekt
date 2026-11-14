#pragma once

#include "fpga.h"
#include "rooms.h"
#include "collision.h"
#include <stdint.h>

// Gravity
#define GRAVITY_AIR 0.1
#define GRAVITY_AIR_FALL 0.6
#define GRAVITY_WATER 0.05
#define GRAVITY_LAVA 0.025
#define GRAVITY_ACID GRAVITY_WATER
#define MAX_GRAVITY_AIR 11
#define MAX_GRAVITY_WATER 0.875
#define MAX_GRAVITY_LAVA 0.375
#define MAX_GRAVITY_ACID MAX_GRAVITY_WATER
#define PLAYER_SAND_SLIDE 2

// Jumping
#define JUMP_AIR -2.5
#define JUMP_WATER -1
#define JUMP_LAVA -0.25
#define JUMP_ACID -0.75

// Flying
#define MAX_FLY_TIME 10
#define FLY_FORCE -1
#define MAX_FLY_SPEED -1.5
#define FLY_REGEN_SPEED MAX_FLY_TIME

// Sideways movement
#define PLAYER_MAX_SPEED 2
#define PLAYER_STEP_ALLOWANCE 3

// Healing
#define PLAYER_MAX_HEALTH 500
#define PLAYER_DAMAGE_TO_HEAL_DELAY 250
#define PLAYER_HEAL_DELAY 1
#define PLAYER_HEAL_AMOUNT 5

// Damage
#define PLAYER_MAX_BREATH 250
#define PLAYER_BURN_TIME 125

// Friction
#define FRICTION_AIR 0.9
#define FRICTION_GROUND_WATER 0.7
#define FRICTION_LAVA 0.4

#define PLAYER_SIZE_X 4
#define PLAYER_SIZE_Y 8

#define PLAYER_PORTAL_EXHAUSTION 4

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

/* The player's sprite (the picture is assets.player). player_sprite_init()
 * once, after assets_init(); update_player() and player_init() then keep
 * it on the player, and sprite_poll() sends it. */
void player_sprite_init(int16_t x, int16_t y);
void player_show(bool visible);         /* hidden while drawing */