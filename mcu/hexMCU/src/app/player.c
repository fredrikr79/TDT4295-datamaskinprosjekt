#include "player.h"
#include "rooms.h"
#include "fpga.h"
#include <stdlib.h>
#include <math.h>
#include <stdint.h>

float move_force = 0;
bool jump_next_frame = false;

static void perform_jump(
    player_t player,
    uint8_t *box,
    velocity_box_t velocity_box
)
{
    jump_next_frame = false;
    bool is_grounded = velocity_box.rel_y + PLAYER_SIZE_Y == velocity_box.box.y2;
    if (!is_grounded)
    {
        uint8_t y = velocity_box.rel_y + PLAYER_SIZE_Y;
        for (uint8_t x = velocity_box.rel_x; x < velocity_box.rel_x + PLAYER_SIZE_X; x++)
        {
            if (box[x + y*velocity_box.box.x2] != 0)
            {
                is_grounded = true;
                break;
            }
        }
    }

    if (is_grounded) player.velocity_y = -5;
}

void update_player_position(
    float force_x,
    float force_y,
    player_t player,
    uint8_t *box,
    velocity_box_t velocity_box
)
{
    if (jump_next_frame) perform_jump(player, box, velocity_box);

    bool apply_friction = velocity_box.box.y1 + velocity_box.box.y2 + 1 == ROOM_Y;
    if (!apply_friction)
    {
        uint16_t y = velocity_box.rel_y + PLAYER_SIZE_Y;
        for (
            uint16_t x = velocity_box.rel_x;
            x < velocity_box.rel_x + PLAYER_SIZE_X;
            x++
        )
        {
            if (box[x + y*velocity_box.box.x2] != 0) apply_friction = true;
        }
    }
    if (apply_friction)
    {
        player.velocity_x *= 0.8;
    }

    if (move_force)
    {
        float move_force_from_max = copysignf(fmaxf(fabsf(player.velocity_x) - PLAYER_MAX_MOVE, 0), move_force);
        if (move_force < move_force_from_max) player.velocity_x += move_force;
        else player.velocity_x += move_force_from_max;
    }
    else if (apply_friction && fabsf(player.velocity_x) < 0.4)

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

    // Data for choosing the next axis
    float prio_x = 1;
    float prio_y = 1;
    float y_of_x = fabsf(player.velocity_y / player.velocity_x);
    float x_of_y = fabsf(player.velocity_x / player.velocity_y);
    bool current_axis_x = fabsf(player.velocity_y) < fabsf(player.velocity_x);
    if (current_axis_x) prio_y = y_of_x;
    else prio_x = x_of_y;

    // Progression data
    int vertical_progress = 0;
    int horizontal_progress = 0;
    int vertical_end = (player.velocity_y < 0)
        ? (int) floorf(player.velocity_y)
        : (int) ceilf(player.velocity_y);
    int vertical_max = (player.velocity_y < 0)
        ? -velocity_box.rel_y
        : velocity_box.box.y2 - velocity_box.rel_y - PLAYER_SIZE_Y;
    if (abs(vertical_max) < abs(vertical_end)) vertical_end = vertical_max;
    int horizontal_end = (player.velocity_x < 0)
        ? (int) floorf(player.velocity_x)
        : (int) ceilf(player.velocity_x);
    int horizontal_max = (player.velocity_x < 0)
        ? -velocity_box.rel_x
        : velocity_box.box.x2 - velocity_box.rel_x - PLAYER_SIZE_X;
    if (abs(horizontal_max) < abs(horizontal_end)) horizontal_end = horizontal_max;
    bool vertical_done = vertical_end == 0;
    bool horizontal_done = horizontal_done == 0;
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
            if (vertical_max <= vertical_progress) vertical_collision = true;
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
            if (horizontal_max <= horizontal_progress) horizontal_collision = true;
        }

        float one = 1;
        float remainder_x = 1 - modff(prio_x, &one);
        one = 1;
        float remainder_y = 1 - modff(prio_y, &one);
        vertical_done = abs(vertical_end) <= abs(vertical_progress);
        horizontal_done = abs(horizontal_end) <= abs(horizontal_progress);
        if (!vertical_done && !horizontal_done)
            current_axis_x = remainder_x / x_of_y < remainder_y / y_of_x;
        else current_axis_x = !vertical_done;
        if (current_axis_x)
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

void move_left()
{
    move_force = -1;
}

void move_right()
{
    move_force = 1;
}

void move_stop()
{
    move_force = 0;
}

void start_jump()
{
    jump_next_frame = true;
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
