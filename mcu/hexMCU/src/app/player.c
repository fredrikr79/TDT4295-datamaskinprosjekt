#include "player.h"
#include "log.h"
#include "rooms.h"
#include "game.h"
#include "sprite.h"
#include "assets.h"
#include <math.h>
#include <stdint.h>

static float move_direction = 0;
static bool jump_next_frame = false;
static bool shoot_next_frame = false;

static uint16_t shoot_target_x = 0;
static uint16_t shoot_target_y = 0;

static sprite_t *spr;                   /* the picture, at the player's top-left */

static void sprite_follow(const player_t *player)
{
    sprite_move(spr, (int16_t) (player->x * 4), (int16_t) (player->y * 4));
}

static uint16_t ticks_since_last_damage = 0;
static uint16_t ticks_since_last_heal = 0;

static bool tile_has_damage(uint8_t id)
{
    return sim_is(id, MAT_LAVA) || sim_is(id, MAT_ACID) || sim_is(id, MAT_EXPLOSIVE);
}

static bool tile_has_raditation(uint8_t id)
{
    return sim_is(id, MAT_LIFE);
}

static bool tile_is_swimmable(uint8_t id)
{
    return sim_is(id, MAT_WATER) || sim_is(id, MAT_LAVA) || sim_is(id, MAT_ACID);
}

static void perform_jump(
    player_t *player,
    bool is_grounded,
    uint16_t in_water,
    uint16_t in_lava,
    uint16_t in_acid
)
{
    if (in_water && JUMP_WATER < player->velocity_y) player->velocity_y = JUMP_WATER;
    else if (in_lava && JUMP_LAVA < player->velocity_y) player->velocity_y = JUMP_LAVA;
    else if (in_acid && JUMP_ACID < player->velocity_y) player->velocity_y = JUMP_ACID;
    else if (is_grounded && JUMP_AIR < player->velocity_y) player->velocity_y = JUMP_AIR;
    else if (
        player->fly_meter
        && !in_water && !in_lava && !in_acid
        && MAX_FLY_SPEED < player->velocity_y
    )
    {
        float fly_force = FLY_FORCE;
        if (
            fly_force + player->velocity_y < MAX_FLY_SPEED
            && MAX_FLY_SPEED < player->velocity_y
        )
        {
            player->velocity_y = MAX_FLY_SPEED;
        }
        else if (0 < player->velocity_y)
            player->velocity_y += 2.5*fly_force;
        else player->velocity_y += fly_force;

        player->fly_meter--;
    }
}

// Returns true if this killed the player. game_reset() has then already
// put a fresh player in *player, so the caller must stop using this step's numbers.
bool damage_player(player_t *player, uint16_t amount)
{
    if (amount < player->health)
        player->health -= amount;
    else player->health = 0;
    ticks_since_last_damage = PLAYER_DAMAGE_TO_HEAL_DELAY;

    if (player->health == 0)
    {
        game_reset();
        return true;
    }
    return false;
}

void heal_player(player_t *player, uint16_t amount)
{
    if (player->health == 0) return;

    if (player->health + amount < PLAYER_MAX_HEALTH)
        player->health += amount;
    else player->health = PLAYER_MAX_HEALTH;
}

void keep_player_in_bounds(player_t *player)
{
    if (player->x <= 0)
    {
        player->x = 0;
        player->velocity_x = 0;
    }
    if (player->y <= 0)
    {
        player->y = 0;
        player->velocity_y = 0;
    }
    if (ROOM_X <= player->x + PLAYER_SIZE_X)
    {
        player->x = ROOM_X - PLAYER_SIZE_X;
        player->velocity_x = 0;
    }
    if (ROOM_Y <= player->y + PLAYER_SIZE_Y)
    {
        player->y = ROOM_Y - PLAYER_SIZE_Y;
        player->velocity_y = 0;
    }
}

static bool take_damage(
    player_t *player,
    uint16_t in_collision, uint16_t in_lava, uint16_t in_acid,
    uint16_t in_explosion, bool on_radiation
)
{
    if (player->burn_timer)
    {
        if (damage_player(player, 1)) return true;
        player->burn_timer--;
    }
    if (in_collision && damage_player(player, 1)) return true;
    if (in_lava)
    {
        if (damage_player(player, 3)) return true;
        player->burn_timer = PLAYER_BURN_TIME;
    }
    if (in_acid && damage_player(player, 3)) return true;
    if (in_explosion && damage_player(player, 5)) return true;
    if (on_radiation && damage_player(player, 2)) return true;
    if (player->breath_meter == 0 && damage_player(player, 5)) return true;
    return false;
}

static void shoot_projectile(player_t *player)
{
    if (player->projectile.active) return;

    // TODO: Check if there's room in the fpga command queue to not break more important functions

    // Aim the projectile
    float vel_x = shoot_target_x - player->x - PLAYER_SIZE_X / 2;
    float vel_y = shoot_target_y - player->y - PLAYER_SIZE_Y / 2;
    float magnitude = sqrtf(vel_x*vel_x + vel_y*vel_y);
    vel_x *= PROJECTILE_SPEED / magnitude;
    vel_y *= PROJECTILE_SPEED / magnitude;

    set_projectile(
        &(player->projectile),
        player->x + PLAYER_SIZE_X / 2, player->y + PLAYER_SIZE_Y / 2, 
        vel_x, vel_y
    );
}

static void next_step(player_t *player)
{
    velocity_box_t next_velocity_box = create_velocity_box(
        player->x, player->y,
        PLAYER_SIZE_X, PLAYER_SIZE_Y,
        player->velocity_x, player->velocity_y
    );
    set_next_velocity_box(&next_velocity_box);
    sprite_follow(player);                  /* sprite_poll() sends it */
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
            if (sim_is(id, MAT_AIR)) in_air++;
            else if (sim_is(id, MAT_STONE)) in_stone++;
            else if (sim_is(id, MAT_SAND)) in_sand++;
            else if (sim_is(id, MAT_WATER)) in_water++;
            else if (sim_is(id, MAT_LAVA)) in_lava++;
            else if (sim_is(id, MAT_ACID)) in_acid++;
            else if (sim_is(id, MAT_LIFE)) in_gol++;
            else if (sim_is(id, MAT_EXPLOSIVE)) in_explosion++;
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
    else if (in_air && player->breath_meter < PLAYER_MAX_BREATH) player->breath_meter++;

    // Check if on ground
    bool on_ground = (int)floorf(player->y + 0.5f) + PLAYER_SIZE_Y >= ROOM_Y;
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
    {
        player->velocity_x *= FRICTION_GROUND_WATER;
        player->fly_meter += FLY_REGEN_SPEED;
        if (MAX_FLY_TIME < player->fly_meter)
            player->fly_meter = MAX_FLY_TIME;
    }
    else if (in_lava)
        player->velocity_x *= FRICTION_LAVA;
    else
        player->velocity_x *= FRICTION_AIR;

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
    else if (fabsf(player->velocity_x) < SPEED(0.075)) player->velocity_x = 0;

    // Calculate gravity with bouyancy included
    float gravity_force = GRAVITY_AIR;
    if (0 < player->velocity_y || !jump_next_frame) gravity_force = GRAVITY_AIR_FALL;
    if (in_lava) gravity_force = GRAVITY_LAVA;
    else if (in_water) gravity_force = GRAVITY_WATER;
    else if (in_acid) gravity_force = GRAVITY_ACID;

    float max_gravity = MAX_GRAVITY_AIR;
    if (in_lava) max_gravity = MAX_GRAVITY_LAVA;
    if (in_water) max_gravity = MAX_GRAVITY_WATER;
    if (in_acid) max_gravity = MAX_GRAVITY_ACID;

    float gravity_force_from_max = max_gravity - player->velocity_y;
    if (gravity_force_from_max < 0) gravity_force_from_max = 0;
    if (gravity_force < gravity_force_from_max) force_y = gravity_force;
    else force_y = gravity_force_from_max;
    // LOG_DBG("gravity_force: %f, force_y: %f", gravity_force, force_y);

    // Apply terminal velocity
    if (max_gravity < player->velocity_y) player->velocity_y = max_gravity;

    // Jump, fly & swim
    if (jump_next_frame) perform_jump(
        player,
        on_ground,
        in_water,
        in_lava,
        in_acid
    );
    jump_next_frame = false;

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

    // Shoot new projectile
    if (shoot_next_frame && !(player->projectile.active))
    {
        shoot_projectile(player);
    }
    shoot_next_frame = false;

    // Heal
    if (ticks_since_last_damage) ticks_since_last_damage--;
    else if (ticks_since_last_heal) ticks_since_last_heal--;
    else
    {
        ticks_since_last_heal = PLAYER_HEAL_DELAY;
        heal_player(player, PLAYER_HEAL_AMOUNT);    /* stops at PLAYER_MAX_HEALTH */
    }

    // Damage
    if (take_damage(player, in_collision, in_lava, in_acid, in_explosion, on_radiation))
    {
        // Stop the step if the player died (see take_damage())
        next_step(player);
        return;
    }

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
        keep_player_in_bounds(player);
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

    // Create the next velocity box, and update the player sprite
    next_step(player);
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

void queue_shot(int x, int y)
{
    shoot_next_frame = true;
    shoot_target_x = (uint16_t) x / 4;
    shoot_target_y = (uint16_t) y / 4;
}

void player_set_default(player_t *player)
{
    player->x = 78;
    player->y = 30;

    player->velocity_x = 0;
    player->velocity_y = 0;

    player->fly_meter = MAX_FLY_TIME;

    player->health = PLAYER_MAX_HEALTH;
    player->breath_meter = PLAYER_MAX_BREATH;
    player->burn_timer = 0;

    player->still_in_portal = 0;

    ticks_since_last_damage = 0;
    ticks_since_last_heal = 0;

    player->projectile.active = false;
}

void player_init()
{
    // Init the player struct
    player_t player = (player_t) {};
    player_set_default(&player);
    sprite_follow(&player);

    // Create the player's projectile
    player.projectile = get_new_projectile(
        player.x + PLAYER_SIZE_X / 2, player.y + PLAYER_SIZE_Y / 2, 
        4, 0,
        EXPLOSIVE
    );
    (&(player.projectile))->active = false;

    set_player(player);

    // Create the next velocity box
    velocity_box_t next_velocity_box = create_velocity_box(
        player.x, player.y,
        PLAYER_SIZE_X, PLAYER_SIZE_Y,
        player.velocity_x, player.velocity_y
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