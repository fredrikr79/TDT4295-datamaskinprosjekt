#ifndef ROOMS_H
#define ROOMS_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "fpga.h"

#define ROOM_X 640
#define ROOM_Y 360
#define MAX_PORTALS 8
#define DATA_MAX_SIZE (ROOM_X*ROOM_Y*2 + 8*MAX_PORTALS)

#define AIR 0
#define STONE 16
#define SAND 17
#define WATER 18
#define LAVA 19
#define ACID 20
#define GOL 21
#define EXPLOSION 22

/* false: no card / file / write failed (logged). read_room() then gives
 * an empty room (all tile 0), so the game goes on. */
bool read_room(int x, int y, uint8_t *tileIDs);
bool save_room(int x, int y, uint8_t *tileIDs);

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
bool save_online_room(struct online_room *room);

bool delete_save(int x, int y);

/* ---- moving the current room to / from the FPGA ---------------------------
 *   room_load_start(x, y)   read the room's file; STOP_SIM, send it row by
 *                           row, START_SIM
 *   room_save_start(x, y)   STOP_SIM, read the room back row by row, then save
 *                           it (the simulation stays stopped: a load follows)
 *   every poll:  room_poll() -> ROOM_BUSY, ROOM_DONE, or ROOM_FAILED (an
 *                FPGA command failed: resync, then room_resume())
 * The room is kept in rooms.c (ROOM_X * ROOM_Y bytes). */
typedef enum
{
  ROOM_IDLE,                /* nothing started                           */
  ROOM_BUSY,
  ROOM_DONE,                /* every row came back OK (file problems: see the log) */
  ROOM_FAILED,              /* an FPGA command failed                    */
} room_status_t;

void room_load_start(int x, int y);
void room_save_start(int x, int y);
room_status_t room_poll(void);
/* After a resync that kept the FPGA session: carry on from the first row
 * not done. Call it after fpga_abort_all(). false: nothing to resume. */
bool room_resume(void);

typedef struct
{
    bool active;

    uint16_t x1;
    uint16_t y1;
    uint16_t x2;
    uint16_t y2;

    int dest_room_x;
    int dest_room_y;
    uint16_t dest_pos_x;
    uint16_t dest_pos_y;
} portal_t;
void init_empty_portals(portal_t *portals, uint8_t n_portals);

#endif