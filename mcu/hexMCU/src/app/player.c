#include "player.h"
#include "log.h"
#include "rooms.h"
#include "game.h"
#include "sprite.h"
#include "assets.h"
#include <math.h>
#include <stdint.h>

float move_direction = 0;
bool jump_next_frame = false;

static sprite_t *spr;                   /* the picture, at the player's top-left */

static void sprite_follow(const player_t *player)
{
    sprite_move(spr, (int16_t)player->x, (int16_t)player->y);
}

uint16_t ticks_since_last_damage = 0;
uint16_t ticks_since_last_heal = 0;

static bool tile_has_damage(uint8_t id)
{
    return id_is(id, LAVA) || id_is(id, ACID) || id_is(id, EXPLOSION);
}

static bool tile_has_raditation(uint8_t id)
{
    return id_is(id, GOL);
}

static bool tile_is_swimmable(uint8_t id)
{
    return id_is(id, WATER) || id_is(id, LAVA) || id_is(id, ACID);
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
    velocity_box_t velocity_box
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
            if (id_is(id, AIR)) in_air++;
            else if (id_is(id, STONE)) in_stone++;
            else if (id_is(id, SAND)) in_sand++;
            else if (id_is(id, WATER)) in_water++;
            else if (id_is(id, LAVA)) in_lava++;
            else if (id_is(id, ACID)) in_acid++;
            else if (id_is(id, GOL)) in_gol++;
            else if (id_is(id, EXPLOSION)) in_explosion++;
        }
    // LOG_DBG("in_air: %u", in_air);
    // LOG_DBG("in_stone: %u", in_stone);
    // LOG_DBG("in_sand: %u", in_sand);
    // LOG_DBG("in_water: %u", in_water);
    // LOG_DBG("in_lava: %u", in_lava);
    // LOG_DBG("in_acid: %u", in_acid);
    // LOG_DBG("in_gol: %u", in_gol);
    // LOG_DBG("in_explosion: %u", in_explosion);
    uint16_t in_collision = in_stone + in_sand + in_gol;
    
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
    if (in_collision) damage_player(player, 1);
    if (in_lava)
    {
        damage_player(player, 3);
        player->burn_timer = PLAYER_BURN_TIME;
    }
    if (in_acid) damage_player(player, 3);
    if (in_explosion) damage_player(player, 5);
    if (on_radiation) damage_player(player, 2);
    if (player->breath_meter == 0) damage_player(player, 5);

    // Check if player is overlapping with a portal
    portal_t *portal = area_in_portal(
        player->x, player->x + PLAYER_SIZE_X,
        player->y, player->y + PLAYER_SIZE_Y
    );
    if (portal != NULL && !player->still_in_portal)
    {
        if (portal->dest_pos_x != 65535)
            player->x = portal->dest_pos_x;
        if (portal->dest_pos_y != 65535)
            player->y = portal->dest_pos_y;
        player->still_in_portal = PLAYER_PORTAL_EXHAUSTION;
        game_change_room(
            portal->dest_room_x,
            portal->dest_room_y
        );
    }
    else if (portal == NULL && 0 < player->still_in_portal) player->still_in_portal--;

    // Move player up if stuck in ground
    if (in_collision)
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
    sprite_follow(player);                  /* sprite_poll() sends it */
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
        .burn_timer = 0,
        .still_in_portal = 0
    };
    set_player(player);
    sprite_follow(&player);

    // Create the next velocity box
    velocity_box_t next_velocity_box = create_velocity_box(
        player.x, player.y,
        PLAYER_SIZE_X, PLAYER_SIZE_Y
    );
    set_next_velocity_box(&next_velocity_box);
}

void player_sprite_init(int16_t x, int16_t y)
{
    if (!spr)       /* after sprite_init() and assets_init() */
        spr = sprite_new(assets.player, x, y, true);
    sprite_move(spr, x, y);
}

void player_show(bool visible)
{
    sprite_show(spr, visible);
}