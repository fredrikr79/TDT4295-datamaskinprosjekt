#pragma once

#include "fpga.h"
#include "rooms.h"
#include <stdint.h>

#define GRAVITY_AIR 0.4
#define GRAVITY_WATER 0.2
#define GRAVITY_LAVA 0.1
#define GRAVITY_ACID GRAVITY_WATER
#define JUMP_AIR 5
#define JUMP_WATER 3
#define JUMP_LAVA 1
#define JUMP_ACID 2
#define PLAYER_MAX_MOVE 5
#define PLAYER_MAX_GRAVITY 7

#define PLAYER_SIZE_X 8
#define PLAYER_SIZE_Y 16

#define PLAYER_MAX_HEALTH 500
#define PLAYER_DAMAGE_TO_HEAL_DELAY 1000
#define PLAYER_HEAL_DELAY 1
#define PLAYER_HEAL_AMOUNT 5
#define PLAYER_MAX_BREATH 500

typedef struct
{
    float x;
    float y;
    float velocity_x;
    float velocity_y;

    uint16_t health;
    uint16_t breath_meter;
} player_t;

typedef struct
{
    fpga_box_t box;
    uint8_t rel_x;
    uint8_t rel_y;
    uint16_t length;
} velocity_box_t;

void update_player(
    player_t player,
    uint8_t *box, velocity_box_t velocity_box,
    portal_t *portals
);
void move_left();
void move_right();
void move_stop();
void start_jump();
velocity_box_t next_velocity_box(player_t player);
