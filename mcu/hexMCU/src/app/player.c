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

    uint8_t last_end_x = velocity_box.rel_x;
    uint8_t last_start_x = last_end_x + (int) floorf(player.velocity_x);
    if (player.velocity_x > 0)
    {
        last_start_x = last_end_x + PLAYER_SIZE_X;
        last_end_x = last_start_x + (int) ceilf(player.velocity_x);
        if (last_end_x > velocity_box.box.x2) last_end_x = velocity_box.box.x2;
    }
    uint8_t last_end_y = velocity_box.rel_y;
    uint8_t last_start_y = last_end_y + (int) floorf(player.velocity_y);
    if (player.velocity_y > 0)
    {
        last_start_y = last_end_y + PLAYER_SIZE_Y;
        last_end_y = last_start_y + (int) ceilf(player.velocity_y);
        if (last_end_y > velocity_box.box.y2) last_end_y = velocity_box.box.y2;
    }

    float prio_x = 1;
    float prio_y = 1;
    float y_of_x = fabsf(player.velocity_y / player.velocity_x);
    float x_of_y = fabsf(player.velocity_x / player.velocity_y);
    bool current_axis_x = fabsf(player.velocity_y) < fabsf(player.velocity_x);
    if (current_axis_x) prio_y = y_of_x;
    else prio_x = x_of_y;

    int vertical_progress = 0;
    int horizontal_progress = 0;
    bool vertical_done = false;
    bool horizontal_done = false;
    bool vertical_collision = false;
    bool horizontal_collision = false;
    uint8_t base_x = velocity_box.rel_x - 1;
    if (player.velocity_x > 0) base_x += PLAYER_SIZE_X + 1;
    uint8_t base_y = velocity_box.rel_y - 1;
    if (player.velocity_y > 0) base_y += PLAYER_SIZE_Y + 1;
    int dir_x = (player.velocity_x < 0) ? -1 : 1;
    int dir_y = (player.velocity_y < 0) ? -1 : 1;

    while (vertical_done && horizontal_done)
    {
        if (current_axis_x)
        {
            for (
                uint8_t x = velocity_box.rel_x + horizontal_progress;
                x < velocity_box.rel_x + PLAYER_SIZE_X + horizontal_progress;
                x++
            )
            {
                if (box[x + (base_y + vertical_progress)*velocity_box.box.x2] != 0)
                {
                    vertical_done = true;
                    vertical_collision = true;
                    vertical_progress -= dir_y;
                    break;
                }
            }
            vertical_progress += dir_y;
        }

        else
        {
            for (
                uint8_t y = velocity_box.rel_y + vertical_progress;
                y < velocity_box.rel_y + PLAYER_SIZE_Y + vertical_progress;
                y++
            )
            {
                if (box[base_x + horizontal_progress + y*velocity_box.box.x2] != 0)
                {
                    horizontal_done = true;
                    horizontal_collision = true;
                    horizontal_progress -= dir_x;
                    break;
                }
            }
            horizontal_progress += dir_x;
        }

        float remainder_x = 1 - modff(prio_x, 1);
        float remainder_y = 1 - modff(prio_y, 1);
        current_axis_x = remainder_x / x_of_y < remainder_y / y_of_x;
        if (current_axis_x && !vertical_done)
        {
            prio_x = ceilf(prio_x);
            prio_y += y_of_x * prio_x;
        }
        else
        {
            prio_y = ceilf(prio_y);
            prio_x += x_of_y * prio_y;
        }
    }

    if (horizontal_collision)
    {
        player.velocity_x = 0;
        player.x += horizontal_progress;
    }
    else player.x += player.velocity_x;
    if (vertical_collision)
    {
        player.velocity_y = 0;
        player.y += vertical_progress;
    }
    else player.y += player.velocity_y;
        
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
