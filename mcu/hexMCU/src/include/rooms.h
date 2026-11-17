#ifndef ROOMS_H
#define ROOMS_H

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "fpga.h"

#define ROOM_X 160
#define ROOM_Y 90
#define MAX_PORTALS 8
#define DATA_MAX_SIZE (1 + 14*MAX_PORTALS + ROOM_X*ROOM_Y*2)

/* false: no card / file / write failed (logged). read_room() then gives
 * an empty room (all tile 0), so the game goes on. */
bool read_room(int x, int y, uint8_t *tileIDs);
bool save_room(int x, int y, uint8_t *tileIDs);

bool delete_save(int x, int y);
void delete_all_saves();

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

// Portals
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
void portals_init();
portal_t *area_in_portal(float x1, float x2, float y1, float y2);

/* Materials: material_t, SIM_CELL(), SIM_MAT() and sim_is() in fpga.h. */

#endif