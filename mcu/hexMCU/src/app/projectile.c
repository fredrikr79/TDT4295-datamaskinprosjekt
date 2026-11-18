#include "projectile.h"
#include "player.h"
#include "log.h"
#include "rooms.h"
#include "game.h"
#include "sprite.h"
#include "assets.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

static uint8_t explosion_box[EXPLOSION_SIZE_X*EXPLOSION_SIZE_Y] = {112};
static uint8_t gol_box[GOL_SIZE_X*GOL_SIZE_Y] = {
    0, 96, 96, 96, 0,
    96, 96, 96, 96, 96,
    96, 96, 96, 96, 96,
    96, 96, 96, 96, 96,
    0, 96, 96, 96, 0
};

static sprite_t *explosion_proj_sprite;
static sprite_t *gol_proj_sprite;

static void sprite_follow(const projectile_t *projectile)
{
    if (projectile->type == EXPLOSIVE)
        sprite_move(
            explosion_proj_sprite,
            (int16_t) (projectile->x * 4),
            (int16_t) (projectile->y * 4)
        );
    else if (projectile->type == GOL)
        sprite_move(
            gol_proj_sprite,
            (int16_t) (projectile->x * 4),
            (int16_t) (projectile->y * 4)
        );
}

bool check_bounds(projectile_t *projectile)
{
    uint8_t collsion_mask = 0;

    if (projectile->x <= 0)
    {
        projectile->x = 0;
        collsion_mask |= 0b0100;
    }
    else if (projectile->y <= 0)
    {
        projectile->y = 0;
        collsion_mask |= 0b0010;
    }
    else if (ROOM_X <= projectile->x + projectile->size_x)
    {
        projectile->x = ROOM_X - projectile->size_x;
        collsion_mask |= 0b0001;
    }
    else if (ROOM_Y <= projectile->y + projectile->size_y)
    {
        projectile->y = ROOM_Y - projectile->size_y;
        collsion_mask |= 0b1000;
    }
    
    return collsion_mask;
}


static void next_step(projectile_t *projectile)
{
    velocity_box_t next_velocity_box = create_velocity_box(
        projectile->x, projectile->y,
        projectile->size_x, projectile->size_y,
        projectile->velocity_x, projectile->velocity_y
    );
    projectile->velocity_box = next_velocity_box;
    sprite_follow(projectile);                  /* sprite_poll() sends it */
}

static void detonate_projectile(
    projectile_t *projectile,
    uint8_t collision_mask
)
{
    uint8_t *box;
    fpga_box_t box_fpga = {0};

    // Guard from accidental detonations
    projectile_show(projectile->type, false);
    if (!(projectile->active)) return;
    projectile->active = false;

    // Determine the size of the result
    if (projectile->type == EXPLOSIVE)
    {
        box = explosion_box;
        box_fpga.x2 = EXPLOSION_SIZE_X;
        box_fpga.y2 = EXPLOSION_SIZE_Y;
    }
    else if (projectile->type == GOL)
    {
        box = gol_box;
        box_fpga.x2 = GOL_SIZE_X;
        box_fpga.y2 = GOL_SIZE_Y;
    }
    else return;

    // Find ideal placement
    float x = projectile->x + projectile->size_x / 2;
    float y = projectile->y + projectile->size_y / 2;
    if (collision_mask & 0b0001)
        x = projectile->x + projectile->size_x - box_fpga.x2 / 2;
    else if (collision_mask & 0b0100)
        x = projectile->x - box_fpga.x2 / 2;

    if (collision_mask & 0b0010)
        y = projectile->y - box_fpga.y2 / 2;
    else if (collision_mask & 0b1000)
        y = projectile->y + projectile->size_y - box_fpga.y2 / 2;

    // Keep result in bounds
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (ROOM_X <= x + box_fpga.x2) x = ROOM_X - box_fpga.x2;
    if (ROOM_Y <= y + box_fpga.y2) y = ROOM_Y - box_fpga.y2;
    box_fpga.x1 = (uint16_t) x;
    box_fpga.y1 = (uint16_t) y;

    // TODO: Check if there's room in the fpga command queue to not break more important functions

    fpga_cmd_t c = fpga_cmd_write_box(
        box_fpga,
        box,
        box_fpga.x2 * box_fpga.y2
    );
    fpga_status_t st = fpga_submit(&c);
    if (st == FPGA_OK) return;

    LOG_DBG(
        "Projectile couldn't send box: %s",
        fpga_status_str(st)
    );
}

bool update_projectile(projectile_t *projectile)
{
    // Move the player
    uint8_t collision_mask = perform_object_movement(
        &(projectile->x), &(projectile->y),
        &(projectile->velocity_x), &(projectile->velocity_y),
        projectile->size_x, projectile->size_y,
        0, 0,
        projectile->velocity_box, projectile->box
    );

    collision_mask |= check_bounds(projectile);

    if (collision_mask) {
        detonate_projectile(projectile, collision_mask);
        return true;
    }

    // Create the next velocity box, and update the player sprite
    next_step(projectile);
    return false;
}

void set_projectile(
    projectile_t *projectile,
    float x, float y,
    float vel_x, float vel_y
)
{
    projectile->active = true;
    projectile->x = x;
    projectile->y = y;
    projectile->velocity_x = vel_x;
    projectile->velocity_y = vel_y;
    projectile->size_x = 1;
    projectile->size_y = 1;

    if (projectile->type == EXPLOSIVE)
    {
        projectile->size_x = EXPLOSION_PROJ_SIZE_X;
        projectile->size_y = EXPLOSION_PROJ_SIZE_Y;
    }
    else if (projectile->type == GOL)
    {
        projectile->size_x = GOL_PROJ_SIZE_X;
        projectile->size_y = GOL_PROJ_SIZE_Y;
    }

    projectile->velocity_box = create_velocity_box(
        x, y,
        projectile->size_x, projectile->size_y,
        vel_x, vel_y
    );
}

projectile_t get_new_projectile(
    float x, float y,
    float vel_x, float vel_y,
    enum projectile_type type
)
{
    projectile_t projectile = (projectile_t) {};
    set_projectile(
        &projectile,
        x, y,
        vel_x, vel_y
    );
    projectile.type = type;

    return projectile;
}

void projectile_sprite_init(int16_t x, int16_t y)
{
    if (!explosion_proj_sprite)
        explosion_proj_sprite = sprite_new(assets.explosive_proj, x, y, false);
    sprite_move(explosion_proj_sprite, x, y);
    if (!gol_proj_sprite)
        gol_proj_sprite = sprite_new(assets.gol_proj, x, y, false);
    sprite_move(gol_proj_sprite, x, y);
}

void projectile_show(enum projectile_type type, bool visible)
{
    if (type == EXPLOSIVE)
    {
        sprite_show(explosion_proj_sprite, visible);
        sprite_show(gol_proj_sprite, false);
    }
    else if (type == GOL)
    {
        sprite_show(gol_proj_sprite, visible);
        sprite_show(explosion_proj_sprite, false);
    }
}
