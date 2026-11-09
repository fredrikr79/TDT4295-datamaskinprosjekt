#include "collision.h"
#include "player.h"
#include "rooms.h"
#include <stdint.h>
#include <stdlib.h>
#include <math.h>


bool tile_has_collision(uint8_t id)
{
    return id == STONE || id == SAND || id == GOL;
}

velocity_box_t create_velocity_box(
    float pos_x, float pos_y,
    uint8_t size_x, uint8_t size_y
)
{
    int dx = VELOCITY_BOX_X;
    int dy = VELOCITY_BOX_Y;
    int rel_x = (dx - size_x) / 2;
    int rel_y = (dy - size_y) / 2;

    int x = floorf(pos_x + 0.5);
    int y = floorf(pos_y + 0.5);

    if (x - rel_x < 0) rel_x = x;
    else if (x - rel_x + dx >= ROOM_X) rel_x = x + dx - ROOM_X;
    if (y - rel_y < 0) rel_y = y;
    else if (y - rel_y + dy >= ROOM_Y) rel_y = y + dy - ROOM_Y;

    fpga_box_t box = {
        x - rel_x, y - rel_y,
        dx, dy
    };
    return (velocity_box_t) {box, rel_x, rel_y, dx*dy};
}

bool perform_object_movement(
    float *pos_x, float *pos_y,
    float *velocity_x, float *velocity_y,
    uint8_t size_x, uint8_t size_y,
    uint8_t step_allowance, uint8_t slide_ease,
    velocity_box_t velocity_box, uint8_t *box
)
{
    // Set up read direction and origins
    bool x_is_right = 0 < *velocity_x;
    bool y_is_down = 0 < *velocity_y;
    uint8_t base_x = velocity_box.rel_x - 1;
    if (x_is_right) base_x += size_x + 1;
    uint8_t base_y = velocity_box.rel_y - 1;
    if (y_is_down) base_y += size_y + 1;
    int dir_x = (!x_is_right) ? -1 : 1;
    int dir_y = (!y_is_down) ? -1 : 1;

    // Calculate boundaries for collision detection
    uint8_t last_end_x = velocity_box.rel_x;
    uint8_t last_start_x = last_end_x + (int) floorf(*velocity_x);
    if (x_is_right)
    {
        last_start_x = last_end_x + size_x;
        last_end_x = last_start_x + (int) ceilf(*velocity_x);
        if (last_end_x > velocity_box.box.x2) last_end_x = velocity_box.box.x2;
    }
    uint8_t last_end_y = velocity_box.rel_y;
    uint8_t last_start_y = last_end_y + (int) floorf(*velocity_y);
    if (y_is_down)
    {
        last_start_y = last_end_y + size_y;
        last_end_y = last_start_y + (int) ceilf(*velocity_y);
        if (last_end_y > velocity_box.box.y2) last_end_y = velocity_box.box.y2;
    }

    // Data for choosing the next axis
    float prio_x = 1;
    float prio_y = 1;
    float y_of_x = fabsf(*velocity_y / *velocity_x);
    float x_of_y = fabsf(*velocity_x / *velocity_y);
    bool current_axis_x = fabsf(*velocity_y) < fabsf(*velocity_x);
    if (current_axis_x) prio_y = y_of_x;
    else prio_x = x_of_y;

    // Progression data
    int vertical_progress = 0;
    int horizontal_progress = 0;
    int vertical_end = (!y_is_down)
        ? (int) floorf(*velocity_y)
        : (int) ceilf(*velocity_y);
    int vertical_max = (!y_is_down)
        ? -velocity_box.rel_y
        : velocity_box.box.y2 - velocity_box.rel_y - size_y;
    if (abs(vertical_max) < abs(vertical_end)) vertical_end = vertical_max;
    int horizontal_end = (!x_is_right)
        ? (int) floorf(*velocity_x)
        : (int) ceilf(*velocity_x);
    int horizontal_max = (!x_is_right)
        ? -velocity_box.rel_x
        : velocity_box.box.x2 - velocity_box.rel_x - size_x;
    if (abs(horizontal_max) < abs(horizontal_end)) horizontal_end = horizontal_max;
    bool vertical_done = vertical_end == 0;
    bool horizontal_done = horizontal_end == 0;
    bool vertical_collision = false;
    bool horizontal_collision = false;

    // Perform step wise checking for collision
    while (vertical_done && horizontal_done)
    {
        if (current_axis_x)
        {
            uint8_t x_start = velocity_box.rel_x + horizontal_progress;
            uint8_t x_target = velocity_box.rel_x + size_x + horizontal_progress;
            uint8_t slide_start = 0;
            uint8_t slide_end = 0;
            for (uint8_t x = x_start; x < x_target; x++)
            {
                uint8_t id = box[x + (base_y + vertical_progress)*velocity_box.box.x2];
                if (tile_has_collision(id))
                {
                    if (y_is_down && id == SAND)
                    {
                        if (x < slide_ease)
                        {
                            slide_start = x + 1;
                            continue;
                        }
                        else if (x_target - x - 1 < slide_ease)
                        {
                            slide_end = x_target - x;
                            continue;
                        }
                    }
                    vertical_done = true;
                    vertical_collision = true;
                    vertical_progress -= dir_y;
                    break;
                }
            }
            if (!vertical_done && y_is_down)
            {
                if (slide_start && slide_end)
                {
                    vertical_done = true;
                    vertical_collision = true;
                    vertical_progress -= dir_y;
                }
                else if (slide_start ^ slide_end)
                {
                    uint8_t slide_amount = 0;
                    bool hit_wall = false;
                    uint8_t y_start = velocity_box.rel_y + vertical_progress + 1;
                    uint8_t y_end = y_start + size_y;
                    uint8_t x = velocity_box.rel_x - 1 + horizontal_progress;
                    uint8_t x_end;
                    if (slide_start)
                    {
                        x += PLAYER_SIZE_X;
                        x_end = x + slide_start;
                    }
                    else x_end = x - slide_end;
                    while (x == x_end)
                    {
                        for (uint8_t y = y_start; y < y_end; y++)
                        {
                            if (tile_has_collision(box[x + y*velocity_box.box.x2]))
                            {
                                hit_wall = true;
                                break;
                            }
                        }
                        if (hit_wall) break;
                        x += slide_start ? 1 : -1;
                        slide_amount++;
                    }
                    if (0 < slide_amount)
                    {
                        if (!(slide_end ^ x_is_right)) horizontal_done = true;
                        horizontal_progress += slide_start ? slide_amount : -slide_amount;
                    }
                }
            }
            vertical_progress += dir_y;
            if (vertical_max <= vertical_progress)
            {
                if (velocity_box.box.y1 + velocity_box.box.y2 == ROOM_Y)
                    vertical_collision = true;
                else break;
            }
        }

        else
        {
            uint8_t y_start = velocity_box.rel_y + vertical_progress;
            uint8_t y_target = velocity_box.rel_y + size_y + vertical_progress;
            uint8_t step_start = 0;
            uint8_t step_end = 0;
            for (uint8_t y = y_start; y < y_target; y++)
            {
                if (tile_has_collision(box[base_x + horizontal_progress + y*velocity_box.box.x2]))
                {
                    if (y < step_allowance)
                    {
                        step_start = y + 1;
                        continue;
                    }
                    else if (y_target - y - 1 < step_allowance)
                    {
                        step_end = y_target - y;
                        continue;
                    }
                    horizontal_done = true;
                    horizontal_collision = true;
                    horizontal_progress -= dir_x;
                    break;
                }
            }
            if (!horizontal_done)
            {
                if (step_start && step_end)
                {
                    horizontal_done = true;
                    horizontal_collision = true;
                    horizontal_progress -= dir_x;
                }
                else if (step_start == 0 && 0 < step_end)
                {
                    uint8_t step_amount = 0;
                    bool hit_roof = false;
                    uint8_t x_start = velocity_box.rel_x + horizontal_progress - 1;
                    uint8_t x_end = x_start + size_x;
                    if (x_is_right)
                    {
                        x_start += 2;
                        x_end += 2;
                    }
                    uint8_t y = velocity_box.rel_x - 1 + vertical_progress;
                    uint8_t y_end = y - step_end;
                    while (y < y_end)
                    {
                        for (uint8_t x = x_start; x < x_end; x++)
                        {
                            if (tile_has_collision(box[x + y*velocity_box.box.x2]))
                            {
                                hit_roof = true;
                                break;
                            }
                        }
                        if (hit_roof) break;
                        y--;
                        step_amount++;
                    }
                    if (0 < step_amount)
                    {
                        if (y_is_down) vertical_done = true;
                        vertical_progress -= step_amount;
                    }
                }
            }
            horizontal_progress += dir_x;
            if (horizontal_max <= horizontal_progress)
            {
                if (velocity_box.box.x1 + velocity_box.box.x2 == ROOM_X)
                    horizontal_collision = true;
                else break;
            }
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

    // Stop object if collided, and apply movement
    if (horizontal_collision)
    {
        *velocity_x = 0;
        *pos_x += horizontal_progress;
    }
    else *pos_x += *velocity_x;
    if (vertical_collision)
    {
        *velocity_y = 0;
        *pos_y += vertical_progress;
    }
    else *pos_y += *velocity_y;

    // Ensure the object is within bounds
    if (*pos_x <= 0)
    {
        *pos_x = 0;
        *velocity_x = 0;
    }
    if (*pos_y <= 0)
    {
        *pos_y = 0;
        *velocity_y = 0;
    }
    if (ROOM_X - 1 <= *pos_x + size_x)
    {
        *pos_x = ROOM_X - 1 - size_x;
        *velocity_x = 0;
    }
    if (ROOM_Y - 1 <= *pos_y + size_y)
    {
        *pos_y = ROOM_Y - 1 - size_y;
        *velocity_y = 0;
    }

    return vertical_collision || horizontal_collision;
};
