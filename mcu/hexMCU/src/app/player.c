#include "player.h"
#include "log.h"
#include "rooms.h"
#include "game.h"
#include <math.h>
#include <stdint.h>

float move_direction = 0;
bool jump_next_frame = false;

uint16_t ticks_since_last_damage = 0;
uint16_t ticks_since_last_heal = 0;

/*
@brief This function checks if a position overlaps with a portal.

@param x: The x-coordinate of the position to check
@param y: The y-coordinate of the position to check
@param *portals: The array of portals to check against

@return Returns -1 if position doesn't overlap with a portal.
        Otherwise, the index of the portal will be returned.
*/
int player_in_portal(player_t player, portal_t *portals)
{
  // Player edges
  uint16_t px1 = player.x;
  uint16_t px2 = player.x + PLAYER_SIZE_X;
  uint16_t py1 = player.y;
  uint16_t py2 = player.y + PLAYER_SIZE_Y;

  // Check every active portal until hit
  for (uint8_t i = 0; i < MAX_PORTALS; i++)
  {
    if (!portals[i].active) break;

    // Portal edges
    uint16_t x1 = portals[i].x1;
    uint16_t y1 = portals[i].y1;
    uint16_t x2 = portals[i].x2;
    uint16_t y2 = portals[i].y2;

    bool x_overlap = false;
    if (x1 <= px1 || px1 <= x2) x_overlap = true;
    if (x1 <= px2 || px2 <= x2) x_overlap = true;
    if (px1 <= x1 || x1 <= px2) x_overlap = true;
    if (px1 <= x2 || x2 <= px2) x_overlap = true;
    bool y_overlap = false;
    if (y1 <= py1 || py1 <= y2) y_overlap = true;
    if (y1 <= py2 || py2 <= y2) y_overlap = true;
    if (py1 <= y1 || y1 <= py2) y_overlap = true;
    if (py1 <= y2 || y2 <= py2) y_overlap = true;

    if (x_overlap && y_overlap)
    {
      return i;
    }
  }

  return -1;
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
    player_t *player,
    bool is_grounded,
    uint16_t in_water,
    uint16_t in_lava,
    uint16_t in_acid
)
{
    jump_next_frame = false;

    if (in_water && -JUMP_WATER < player->velocity_y) player->velocity_y = -JUMP_WATER;
    else if (in_lava && -JUMP_LAVA < player->velocity_y) player->velocity_y = -JUMP_LAVA;
    else if (in_acid && -JUMP_ACID < player->velocity_y) player->velocity_y = -JUMP_ACID;
    else if (is_grounded && -JUMP_AIR < player->velocity_y) player->velocity_y = -JUMP_AIR;
}

void damage_player(player_t *player, uint16_t amount)
{
    if (amount < player->health)
        player->health -= amount;
    else player->health = 0;
    ticks_since_last_damage = PLAYER_DAMAGE_TO_HEAL_DELAY;

    // TODO: Check if dead
    // if (player->health == 0) reset_game()
}

void heal_player(player_t *player, uint16_t amount)
{
    if (player->health + amount < PLAYER_MAX_HEALTH)
        player->health += amount;
    else player->health = PLAYER_MAX_HEALTH;
}

void update_player(
    player_t *player,
    uint8_t *box,
    velocity_box_t velocity_box,
    portal_t *portals
)
{
    // LOG_DBG("1 vel_x: %f, vel_y: %f", player->velocity_x, player->velocity_y);

    float force_x = 0;
    float force_y = 0;

    // Check consistency of player box
    uint16_t in_air = 0;
    uint16_t in_stone = 0;  // TODO: Move player up when inside collision
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
            else if (id == STONE) in_stone++;
            else if (id == SAND) in_sand++;
            else if (id == WATER) in_water++;
            else if (id == LAVA) in_lava++;
            else if (id == ACID) in_acid++;
            else if (id == GOL) in_gol++;
            else if (id == EXPLOSION) in_explosion++;
        }
    // LOG_DBG("in_air: %u", in_air);
    // LOG_DBG("in_stone: %u", in_stone);
    // LOG_DBG("in_sand: %u", in_sand);
    // LOG_DBG("in_water: %u", in_water);
    // LOG_DBG("in_lava: %u", in_lava);
    // LOG_DBG("in_acid: %u", in_acid);
    // LOG_DBG("in_gol: %u", in_gol);
    // LOG_DBG("in_explosion: %u", in_explosion);
    
    // Breathing
    if (!in_air && 0 < player->breath_meter) player->breath_meter--;
    else if (in_air && player->breath_meter < 250) player->breath_meter++;

    // Check if on ground
    bool on_ground = velocity_box.box.y1 + velocity_box.box.y2 == ROOM_Y;
    bool on_radiation = false;
    // LOG_DBG("on_ground: %u", on_ground);
    if (!on_ground)
    {
        uint16_t y = velocity_box.rel_y + PLAYER_SIZE_Y;
        for (
            uint16_t x = velocity_box.rel_x;
            x < velocity_box.rel_x + PLAYER_SIZE_X;
            x++
        )
        {
            // LOG_DBG("x: %u, y: %u, id: %u, col: %u", x, y, box[x + y*velocity_box.box.x2], tile_has_collision(box[x + y*velocity_box.box.x2]));
            if (tile_has_collision(box[x + y*velocity_box.box.x2])) on_ground = true;
            if (tile_has_raditation(box[x + y*velocity_box.box.x2])) on_radiation = true;
        }
    }
    // LOG_DBG("on_ground: %u", on_ground);

    // Apply friction
    if (on_ground || in_water || in_acid)
        player->velocity_x *= 0.7;
    else if (in_lava)
        player->velocity_x *= 0.4;
    else
        player->velocity_x *= 0.9;

    // Jump & swim
    if (jump_next_frame) perform_jump(
        player,
        on_ground,
        in_water,
        in_lava,
        in_acid
    );

    // Accelerate from input
    if (move_direction)
    {
        float target_velocity = copysignf(
            fmaxf(PLAYER_MAX_SPEED, 0),
            move_direction
        );
        if (target_velocity < 0 && target_velocity < player->velocity_x)
            player->velocity_x = target_velocity;
        else if (0 < target_velocity && player->velocity_x < target_velocity)
            player->velocity_x = target_velocity;
    }
    // Stop if no input and low velocity
    else if (fabsf(player->velocity_x) < 0.3) player->velocity_x = 0;

    // Calculate gravity with bouyancy included
    float gravity_force = GRAVITY_AIR;
    if (in_lava) gravity_force = GRAVITY_LAVA;
    else if (in_water) gravity_force = GRAVITY_WATER;
    else if (in_acid) gravity_force = GRAVITY_ACID;
    float gravity_force_from_max = PLAYER_MAX_GRAVITY - player->velocity_y;
    if (gravity_force < gravity_force_from_max) force_y = gravity_force;
    else force_y = gravity_force_from_max;
    //LOG_DBG("gravity_force: %f, force_y: %f", gravity_force, force_y);

    // Apply first half of acceleration
    player->velocity_x += force_x / 2;
    player->velocity_y += force_y / 2;
    //LOG_DBG("2 vel_x: %f, vel_y: %f", player->velocity_x, player->velocity_y);

    // Move the player
    perform_object_movement(
        &(player->x), &(player->y),
        &(player->velocity_x), &(player->velocity_y),
        PLAYER_SIZE_X, PLAYER_SIZE_Y,
        PLAYER_STEP_ALLOWANCE, PLAYER_SAND_SLIDE,
        velocity_box, box
    );
    //LOG_DBG("3 vel_x: %f, vel_y: %f", player->velocity_x, player->velocity_y);

    // Apply second half of velocity
    player->velocity_x += force_x / 2;
    player->velocity_y += force_y / 2;
    //LOG_DBG("4 vel_x: %f, vel_y: %f", player->velocity_x, player->velocity_y);

    // Heal
    if (ticks_since_last_damage) ticks_since_last_damage--;
    else if (ticks_since_last_heal) ticks_since_last_heal--;
    else
    {
        ticks_since_last_heal = PLAYER_HEAL_DELAY;
        player->health += PLAYER_HEAL_AMOUNT;
    }

    // Damage
    if (player->burn_timer)
    {
        damage_player(player, 1);
        player->burn_timer--;
    }
    if (in_stone) damage_player(player, 1);
    if (in_sand) damage_player(player, 1);
    if (in_lava)
    {
        damage_player(player, 3);
        player->burn_timer = PLAYER_BURN_TIME;
    }
    if (in_acid) damage_player(player, 3);
    if (in_gol) damage_player(player, 2);
    if (in_explosion) damage_player(player, 5);
    if (on_radiation) damage_player(player, 2);
    if (player->breath_meter == 0) damage_player(player, 5);

    // Check if player is overlapping with a portal
    int overlap = player_in_portal(*player, portals);
    if (overlap != -1)
    {
        portal_t portal = portals[overlap];
        player->x = portal.dest_pos_x;
        player->y = portal.dest_pos_y;
        game_change_room(
            portal.dest_pos_x,
            portal.dest_pos_y
        );
    }

    // Move player up if stuck in ground
    if (in_stone || in_sand || in_gol || in_explosion)
    {
        // Push vertically
        uint8_t y = velocity_box.rel_y + PLAYER_SIZE_Y - 1;
        for (uint8_t x = velocity_box.rel_x; x < velocity_box.rel_x + PLAYER_SIZE_X; x++)
        {
            if (tile_has_collision(box[x + y*velocity_box.box.x2]))
            {
                player->y--;
                if (player->y < 0)
                    player->y = 0;
            }
        }

        // Push horizontally
        uint8_t left = velocity_box.rel_x;
        uint8_t right = velocity_box.rel_x + PLAYER_SIZE_X - 1;
        uint8_t collisions_left = 0;
        uint8_t collisions_right = 0;
        for (uint8_t y = velocity_box.rel_y; y < velocity_box.rel_y + PLAYER_SIZE_Y; y++)
        {
            if (tile_has_collision(box[left + y*velocity_box.box.x2]))
                collisions_left++;
            if (tile_has_collision(box[right + y*velocity_box.box.x2]))
                collisions_right++;
        }
        if (collisions_left < collisions_right)
        {
            player->x--;
            if (player->x < 0)
                player->x = 0;
        }
        else if (collisions_right < collisions_left)
        {
            player->x++;
            if (ROOM_X <= player->x + PLAYER_SIZE_X)
                player->x = ROOM_X - PLAYER_SIZE_X;
        }
    }

    // Create the next velocity box
    velocity_box_t next_velocity_box = create_velocity_box(
        player->x, player->y,
        PLAYER_SIZE_X, PLAYER_SIZE_Y
    );
    set_next_velocity_box(&next_velocity_box);
    //LOG_DBG("end vel_x: %f, vel_y: %f", player->velocity_x, player->velocity_y);
}

void move_left()
{
    move_direction = -1;
}

void move_right()
{
    move_direction = 1;
}

void move_stop()
{
    move_direction = 0;
}

void start_jump()
{
    jump_next_frame = true;
}

void player_init()
{
    // Init the player struct
    player_t player = (player_t) {
        .x = 318, .y = 120,
        .velocity_x = 0, .velocity_y = 0,
        .health = PLAYER_MAX_HEALTH,
        .breath_meter = PLAYER_MAX_BREATH,
        .burn_timer = 0
    };
    set_player(player);

    // Create the next velocity box
    velocity_box_t next_velocity_box = create_velocity_box(
        player.x, player.y,
        PLAYER_SIZE_X, PLAYER_SIZE_Y
    );
    set_next_velocity_box(&next_velocity_box);
}
