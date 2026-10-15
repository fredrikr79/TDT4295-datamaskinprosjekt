#include <stdio.h>
#include <stdint.h>

#include "fpga.h"

#define ROOM_X 40
#define ROOM_Y 60
#define MAX_PORTALS 8
#define MAX_PORTAL_EXITS 8
#define PORTAL_THICKNESS 2
#define DATA_MAX_SIZE ROOM_X*ROOM_Y*2 + 8*MAX_PORTALS

void read_room(int x, int y, uint8_t *tileIDs);
void save_room(int x, int y, uint8_t *tileIDs);

struct online_room
{
    int x;
    int y;

    uint8_t *tileIDs;
    uint32_t tile_progress;
    uint8_t prev_id;
    uint32_t consecutive;

    uint8_t data[DATA_MAX_SIZE];
    unsigned long data_size;
};
struct online_room start_online_room(int x, int y, uint8_t *tileIDs);
void progress_online_room(struct online_room *room, uint8_t *new_ids, uint16_t id_length);
void save_online_room(struct online_room *room);

void delete_save(int x, int y);

void send_room_to_fpga(uint8_t *tileIDs);
void read_room_from_fpga(uint8_t *tile_buffer);

struct portal
{
    bool active;

    uint16_t x;
    uint16_t y;
    uint8_t dx;
    uint8_t dy;

    int dest_x;
    int dest_y;
    uint8_t dest_exit;
};
struct portal_exit
{
    bool active;

    uint16_t x;
    uint16_t y;
    uint8_t dx;
    uint8_t dy;
};
void init_empty_portals(struct portal *portals, uint8_t n_portals);
int pos_in_portal(uint8_t x, uint8_t y, struct portal *portals);
