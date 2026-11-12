#pragma once

#include "fpga.h"
#include "rooms.h"
#include "collision.h"
#include <stdint.h>

#define GRAVITY_AIR 0.4
#define GRAVITY_WATER 0.2
#define GRAVITY_LAVA 0.1
#define GRAVITY_ACID GRAVITY_WATER
#define JUMP_AIR 5
#define JUMP_WATER 3
#define JUMP_LAVA 1
#define JUMP_ACID 2
#define PLAYER_MAX_SPEED 5
#define PLAYER_MAX_GRAVITY 7

#define PLAYER_SIZE_X 8
#define PLAYER_SIZE_Y 16
#define PLAYER_STEP_ALLOWANCE 3
#define PLAYER_SAND_SLIDE 2

#define PLAYER_MAX_HEALTH 500
#define PLAYER_DAMAGE_TO_HEAL_DELAY 250
#define PLAYER_HEAL_DELAY 1
#define PLAYER_HEAL_AMOUNT 5
#define PLAYER_MAX_BREATH 250
#define PLAYER_BURN_TIME 125
#define PLAYER_PORTAL_EXHAUSTION 10

typedef struct
{
    float x;
    float y;
    float velocity_x;
    float velocity_y;

    uint16_t health;
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