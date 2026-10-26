#include "fpga.h"
#include <stdint.h>

#define GRAVITY 0.4

#define PLAYER_SIZE_X 8
#define PLAYER_SIZE_Y 16

typedef struct
{
    float x;
    float y;
    float velocity_x;
    float velocity_y;

    uint8_t health;
} player_t;

typedef struct
{
    fpga_box_t box;
    int rel_x;
    int rel_y;
    uint8_t length;
} velocity_box_t;

void update_player_position(
    float force_x, float force_y,
    player_t player,
    uint8_t *box, velocity_box_t velocity_box
);
velocity_box_t next_velocity_box(player_t player);
