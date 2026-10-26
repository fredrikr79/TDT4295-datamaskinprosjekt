#include "player.h"
#include "rooms.h"
#include "fpga.h"
#include <math.h>
#include <stdint.h>

void update_player_position(
    float force_x,
    float force_y,
    player_t player,
    uint8_t *box,
    velocity_box_t velocity_box
)
{
    player.velocity_x += force_x / 2;
    player.velocity_y += (force_y + GRAVITY) / 2;

    // TODO: Check for collision

    player.x += player.velocity_x;
    player.y += player.velocity_y;

    if (player.x <= 0)
    {
        player.x = 0;
        player.velocity_x = 0;
    }
    if (player.y <= 0)
    {
        player.y = 0;
        player.velocity_y = 0;
    }
    if (ROOM_X - 1 <= player.x)
    {
        player.x = ROOM_X - 1;
        player.velocity_x = 0;
    }
    if (ROOM_Y - 1 <= player.y)
    {
        player.y = ROOM_Y - 1;
        player.velocity_y = 0;
    }

    player.velocity_x += force_x / 2;
    player.velocity_y += (force_y + GRAVITY) / 2;
}

velocity_box_t next_velocity_box(player_t player)
{
    // TODO: Change function to choose a 25x25 box that aims in the direction of the velocity

    float dx = player.velocity_x;
    float dy = player.velocity_y + GRAVITY / 2;

    float x = player.x;
    float y = player.y;
    if (dx < 0) {x -= dx; dx = -dx;}
    if (dy < 0) {y -= dy; dy = -dy;}

    fpga_box_t box = {
        floorf(x), floorf(y),
        ceilf(dx), ceilf(dy)
    };
    return (velocity_box_t) {box, 12, 8, box.x2*box.y2};
}
