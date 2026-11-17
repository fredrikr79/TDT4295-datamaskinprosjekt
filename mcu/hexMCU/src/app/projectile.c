#include "projectile.h"
#include "player.h"
#include "log.h"
#include "rooms.h"
#include "game.h"
#include "sprite.h"
#include "assets.h"
#include <math.h>
#include <stdint.h>

static uint8_t explosion_box[EXPLOSION_SIZE_X*EXPLOSION_SIZE_Y] = {112};
static uint8_t gol_box[GOL_SIZE_X*GOL_SIZE_Y] = {
    0, 96, 96, 96, 0,
    96, 96, 96, 96, 96,
    96, 96, 96, 96, 96,
    96, 96, 96, 96, 96,
    0, 96, 96, 96, 0
};

static sprite_t *spr;                   /* the picture, at the projectile's top-left */

static void sprite_follow(const projectile_t *projectile)
{
    sprite_move(spr, (int16_t) (projectile->x * 4), (int16_t) (projectile->y * 4));
}

bool check_bounds(projectile_t *projectile)
{
    if (projectile->x <= 0)
    {
        projectile->x = 0;
    }
    else if (projectile->y <= 0)
    {
        projectile->y = 0;
    }
    else if (ROOM_X <= projectile->x + projectile->size_x)
    {
        projectile->x = ROOM_X - projectile->size_x;
    }
    else if (ROOM_Y <= projectile->y + projectile->size_y)
    {
        projectile->y = ROOM_Y - projectile->size_y;
    }
    else return false;
    return true;
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

static void detonate_projectile(projectile_t *projectile)
{
    uint8_t *box;
    fpga_box_t box_fpga = {0};

    if (!(projectile->active)) return;
    projectile->active = false;

    if (projectile->type == EXPLOSIVE)
    {
        box = explosion_box;
        box_fpga.x1 = (uint16_t) projectile->x - EXPLOSION_SIZE_X;
        box_fpga.y1 = (uint16_t) projectile->y - EXPLOSION_SIZE_Y;
        box_fpga.x2 = EXPLOSION_SIZE_X;
        box_fpga.y2 = EXPLOSION_SIZE_Y;
    }
    else if (projectile->type == GOL)
    {
        box = gol_box;
        box_fpga.x1 = (uint16_t) projectile->x - GOL_SIZE_X;
        box_fpga.y1 = (uint16_t) projectile->y - GOL_SIZE_Y;
        box_fpga.x2 = GOL_SIZE_X;
        box_fpga.y2 = GOL_SIZE_Y;
    }
    else return;

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
    bool collided = perform_object_movement(
        &(projectile->x), &(projectile->y),
        &(projectile->velocity_x), &(projectile->velocity_y),
        projectile->size_x, projectile->size_y,
        0, 0,
        projectile->velocity_box, projectile->box
    );

    collided = collided || check_bounds(projectile);

    if (collided) {
        detonate_projectile(projectile);
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

    LOG_DBG("MAU %u", projectile->type);
    if (projectile->type == EXPLOSIVE)
    {
        LOG_DBG("Mooooooooo");
        projectile->size_x = EXPLOSIVE_SIZE_X;
        projectile->size_y = EXPLOSIVE_SIZE_Y;
    }
    else if (projectile->type == GOL)
    {
        LOG_DBG("Nyu lai");
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
    if (!spr)       /* after sprite_init() and assets_init() */
        spr = sprite_new(assets.player, x, y, true);
    sprite_move(spr, x, y);
}

void projectile_show(bool visible)
{
    sprite_show(spr, visible);
}