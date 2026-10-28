#include "fpga.h"
#include <stdint.h>

#define GRAVITY 0.4

typedef struct
{
    float x;
    float y;
    float velocity_x;
    float velocity_y;
    uint8_t promised_exit;

    uint8_t health;
} player_t;

void update_player_position(float force_x, float force_y, player_t player, uint8_t *box);
fpga_box_t next_velocity_box(player_t player);
