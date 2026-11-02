#include "player.h"
#include "rooms.h"
#include "game.h"
#include <stdlib.h>
#include <math.h>
#include <stdint.h>

float move_force = 0;
bool jump_next_frame = false;

static bool tile_has_collision(uint8_t id)
{
    return id == STONE || id == SAND || id == GOL;
}

static bool tile_has_damage(uint8_t id)
{
    return id == LAVA || id == ACID || id == EXPLOSION;
}

static bool tile_has_raditation(uint8_t id)
{
    return id == GOL;
}

static bool tile_is_swimmable(uint8_t id)
{
    return id == WATER || id == LAVA || id == ACID;
}

static void perform_jump(
    player_t player,
    uint8_t *box,
    bool is_grounded,
    uint16_t in_water,
    uint16_t in_lava,
    uint16_t in_acid
)
{
    jump_next_frame = false;

    if (in_water && -JUMP_WATER < player.velocity_y) player.velocity_y = -JUMP_WATER;
    else if (in_lava && -JUMP_LAVA < player.velocity_y) player.velocity_y = -JUMP_LAVA;
    else if (in_acid && -JUMP_ACID < player.velocity_y) player.velocity_y = -JUMP_ACID;
    else if (is_grounded && -JUMP_AIR < player.velocity_y) player.velocity_y = -JUMP_AIR;
}

velocity_box_t update_player(
    player_t player,
    uint8_t *box,
    velocity_box_t velocity_box,
    portal_t *portals
)
{
    float force_x = 0;
    float force_y = 0;

    // Check consistency of player box
    uint16_t in_air = 0;
    uint16_t in_stone = 0;
    uint16_t in_sand = 0;
    uint16_t in_water = 0;
    uint16_t in_lava = 0;
    uint16_t in_acid = 0;
    uint16_t in_gol = 0;
    uint16_t in_explosion = 0;
    for (uint8_t y = velocity_box.rel_y; y < velocity_box.rel_y + PLAYER_SIZE_Y; y++)
        for (uint8_t x = velocity_box.rel_x; x < velocity_box.rel_x + PLAYER_SIZE_X; x++)
        {
            uint8_t id = box[x + y*velocity_box.box.x2];
            if (id == AIR) in_air++;
            else if (id == STONE) in_air++;
            else if (id == SAND) in_air++;
            else if (id == WATER) in_air++;
            else if (id == LAVA) in_air++;
            else if (id == ACID) in_air++;
            else if (id == GOL) in_air++;
            else if (id == EXPLOSION) in_air++;
        }

    // Check if on ground
    bool on_ground = velocity_box.box.y1 + velocity_box.box.y2 + 1 == ROOM_Y;
    if (!on_ground)
    {
        uint16_t y = velocity_box.rel_y + PLAYER_SIZE_Y;
        for (
            uint16_t x = velocity_box.rel_x;
            x < velocity_box.rel_x + PLAYER_SIZE_X;
            x++
        )
        {
            if (tile_has_collision(box[x + y*velocity_box.box.x2])) on_ground = true;
        }
    }

    // Apply friction
    if (on_ground || in_water || in_acid)
        player.velocity_x *= 0.7;
    else if (in_lava)
        player.velocity_x *= 0.4;
    else
        player.velocity_x *= 0.9;

    // Apply gravity
    if (jump_next_frame) perform_jump(
        player,
        box,
        on_ground,
        in_water,
        in_lava,
        in_acid
    );

    // Accelerate from input
    if (move_force)
    {
        float move_force_from_max = copysignf(fmaxf(fabsf(player.velocity_x) - PLAYER_MAX_MOVE, 0), move_force);
        if (move_force < move_force_from_max) force_x = move_force;
        else force_x = move_force_from_max;
    }
    // Stop if no input and low velocity
    else if (fabsf(player.velocity_x) < 0.3) player.velocity_x = 0;

    // Calculate gravity with bouyancy included
    force_y = GRAVITY_AIR;
    if (in_lava) force_y = GRAVITY_LAVA;
    else if (in_water) force_y = GRAVITY_WATER;
    else if (in_acid) force_y = GRAVITY_ACID;

    // Apply first half of acceleration
    player.velocity_y += force_x / 2;
    player.velocity_y += force_y / 2;

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
    bool horizontal_done = horizontal_end == 0;
    bool vertical_collision = false;
    bool horizontal_collision = false;

    // Set up read direction and origins
    uint8_t base_x = velocity_box.rel_x - 1;
    if (player.velocity_x > 0) base_x += PLAYER_SIZE_X + 1;
    uint8_t base_y = velocity_box.rel_y - 1;
    if (player.velocity_y > 0) base_y += PLAYER_SIZE_Y + 1;
    int dir_x = (player.velocity_x < 0) ? -1 : 1;
    int dir_y = (player.velocity_y < 0) ? -1 : 1;

    // Perform step wise checking for collision
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
                if (tile_has_collision(box[x + (base_y + vertical_progress)*velocity_box.box.x2]))
                {
                    vertical_done = true;
                    vertical_collision = true;
                    vertical_progress -= dir_y;
                    break;
                }
            }
            vertical_progress += dir_y;
            if (vertical_max <= vertical_progress) vertical_collision = true;  // Change to only count as collision if outer walls are hit
        }

        else
        {
            for (
                uint8_t y = velocity_box.rel_y + vertical_progress;
                y < velocity_box.rel_y + PLAYER_SIZE_Y + vertical_progress;
                y++
            )
            {
                if (tile_has_collision(box[base_x + horizontal_progress + y*velocity_box.box.x2]))
                {
                    horizontal_done = true;
                    horizontal_collision = true;
                    horizontal_progress -= dir_x;
                    break;
                }
            }
            horizontal_progress += dir_x;
            if (horizontal_max <= horizontal_progress) horizontal_collision = true;  // Change to only count as collision if outer walls are hit
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

    // Stop player if collided, and apply attained movement
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

    // Ensure the player is within bounds
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

    // Apply second half of velocity
    player.velocity_x += force_x / 2;
    player.velocity_y += force_y / 2;

    // Check if player is overlapping with a portal
    int overlap = pos_in_portal(player.x, player.y, portals);
    if (overlap != -1)
    {
        portal_t portal = portals[overlap];
        player.x = portal.dest_pos_x;
        player.y = portal.dest_pos_y;
        game_change_room(
            portal.dest_pos_x,
            portal.dest_pos_y
        );
    }

    // Create the next velocity box
    return next_velocity_box(player);
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
    int dx = 20;
    int dy = 30;
    int rel_x = (dx - PLAYER_SIZE_X) / 2;
    int rel_y = (dy - PLAYER_SIZE_Y) / 2;

    int x = floorf(player.x + 0.5);
    int y = floorf(player.y + 0.5);

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
