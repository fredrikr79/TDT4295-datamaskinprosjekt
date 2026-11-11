/* rooms.c - rooms: tile IDs <-> run-length files <-> the FPGA grid.
 * Portable: the files go through storage.h (SD card on the MCU, a local
 * folder on the PC). */

#include "player.h"
#define LOG_TAG "rooms"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "rooms.h"
#include "storage.h"
#include "log.h"
#include "fpga.h"

/* Room files are DATA_MAX_SIZE (~5 KB): too big for the MCU stack (CubeMX
 * default 1 KB), so they live here. Not re-entrant -- one at a time. */
static uint8_t file_data[DATA_MAX_SIZE];

static portal_t portals[MAX_PORTALS] = {0};

/* The room we're in, as tile IDs (row after row: cell x, y is
 * room_tiles[y * ROOM_X + x]). Loaded from the card, then sent to the FPGA;
 * read back from the FPGA, then saved. A tile is the FPGA's cell byte as
 * it is on the wire (material in the high 4 bits, SIM_CELL() in fpga.h),
 * passed through unchanged, so room files hold those bytes too. 0 = air. */
static uint8_t room_tiles[ROOM_X*ROOM_Y];

#define NAME_LEN 32     /* "save/-1000,-1000.dat" fits */

void decode_room(uint8_t *data, uint32_t data_length, uint8_t *tileIDs)
{
  uint32_t data_progress = 0;

  // Find number of portals
  uint8_t num_portals = data[data_progress];
  if (MAX_PORTALS < num_portals) num_portals = MAX_PORTALS;
  data_progress++;

  // Create portals
  for (uint8_t i = 0; i < num_portals; i++)
  {
    uint16_t from_x = (data[data_progress + 0] << 8) + data[data_progress + 1];
    uint16_t from_y = (data[data_progress + 2] << 8) + data[data_progress + 3];
    uint16_t to_x = (data[data_progress + 4] << 8) + data[data_progress + 5];
    uint16_t to_y = (data[data_progress + 6] << 8) + data[data_progress + 7];
    int room_x = data[data_progress + 8];
    int room_y = data[data_progress + 9];
    uint16_t pos_x = (data[data_progress + 10] << 8) + data[data_progress + 11];
    uint16_t pos_y = (data[data_progress + 12] << 8) + data[data_progress + 13];

    // Post process room's signs
    if (room_x & 128) room_x = -(room_x & 127);
    if (room_y & 128) room_y = -(room_y & 127);

    // Create portal
    portal_t portal = {
      .active=true,
      .x1=from_x, .y1=from_y,
      .x2=to_x, .y2=to_y,
      .dest_room_x=room_x, .dest_room_y=room_y,
      .dest_pos_x=pos_x, .dest_pos_y=pos_y
    };
    portals[i] = portal;

    data_progress += 14;
  }

  // Deactivate the rest of the portals
  for (uint8_t i = num_portals; i < MAX_PORTALS; i++)
  {
    portals[i].active = false;
  }

  // Iterate through sections
  uint32_t tile_progress = 0;
  uint8_t current_id = 0;
  while (data_progress < data_length)
  {
    // Find number of bytes in variable length number
    uint8_t varible_number_bytes = 0;
    uint8_t varible_number_header = data[data_progress];
    while (varible_number_header & 128)
    {
      varible_number_header <<= 1;
      varible_number_bytes++;
    }

    // Verify data length
    if (data_progress + varible_number_bytes >= data_length) break;

    // Remove header from variable length number
    uint8_t header_mask = 128;
    for (uint8_t i = 1; i < varible_number_bytes; i++)
    {
      header_mask |= (header_mask >> 1);
    }
    data[data_progress] &= ~header_mask;


    // Construct number of consecutive IDs
    uint32_t consecutive_IDs = 0;
    for (uint32_t i = 0; i < varible_number_bytes && i < 4; i++)
    {
      consecutive_IDs |= ((uint32_t) data[data_progress + varible_number_bytes - 1 - i]) << 8*i;
    }

    // Fill in IDs in array
    current_id = data[data_progress + varible_number_bytes];
    for (uint32_t i = 0; i < consecutive_IDs && tile_progress + i < ROOM_X*ROOM_Y; i++)
    {
      tileIDs[tile_progress + i] = current_id;
    }
    data_progress += varible_number_bytes + 1;
    tile_progress += consecutive_IDs;
  }

  // Fill in any remaining tiles with the last used ID
  for (int i = tile_progress; i < ROOM_X*ROOM_Y; i++)
  {
    tileIDs[i] = current_id;
  }
}

bool read_room(int x, int y, uint8_t *tileIDs)
{
  storage_status_t status;
  uint32_t bytes_read = 0;
  uint8_t *data = file_data;

  // Open save file is exists, otherwise, open from rom
  char filename_rom[NAME_LEN] = {0};
  snprintf(filename_rom, NAME_LEN, "rom/%d,%d.dat", x, y);
  char filename_save[NAME_LEN] = {0};
  snprintf(filename_save, NAME_LEN, "save/%d,%d.dat", x, y);

  // Read the file (at most DATA_MAX_SIZE bytes)
  status = storage_read(filename_save, 0, data, DATA_MAX_SIZE, &bytes_read);
  if (status == STORAGE_NOT_FOUND)
  {
    status = storage_read(filename_rom, 0, data, DATA_MAX_SIZE, &bytes_read);
  }
  if (status != STORAGE_OK)
  {
    LOG_ERR("room %d,%d: %s -- using an empty room", x, y, storage_status_str(status));
    memset(tileIDs, 0, ROOM_X*ROOM_Y);
    return false;
  }

  // Decode file data
  decode_room(data, bytes_read, tileIDs);
  return true;
}

void add_data(
  uint8_t id, uint32_t consecutive,
  uint8_t *data, unsigned long *data_size
)
{
  // Cover the case where 5 bits are used
  if (consecutive > 134217727)
  {
    data[*data_size] = 248;
    data[*data_size + 1] = consecutive >> 24;
    data[*data_size + 2] = consecutive >> 16;
    data[*data_size + 3] = consecutive >>  8;
    data[*data_size + 4] = consecutive;
    data[*data_size + 5] = id;
    *data_size += 6;
    return;
  }

  // Split consecutive uint32 into 4 uint8
  uint8_t consecutive_split[4] = {0};
  consecutive_split[0] = consecutive >> 24;
  consecutive_split[1] = consecutive >> 16;
  consecutive_split[2] = consecutive >>  8;
  consecutive_split[3] = consecutive;

  // Find header information
  uint8_t byte_length = 1;
  uint8_t header = 128;
  if (consecutive > 1048575)  // 20
  {
    byte_length = 4;
    header = 240;
  }
  else if (consecutive > 8191)  // 13
  {
    byte_length = 3;
    header = 224;
  }
  else if (consecutive > 63)  // 6
  {
    byte_length = 2;
    header = 192;
  }

  // Insert header
  consecutive_split[4 - byte_length] =
    (consecutive_split[4 - byte_length] & ~header) | header;
  consecutive_split[4 - byte_length] &= ~(128 >> byte_length);

  // Write to data
  for (uint8_t i = 0; i < byte_length; i++)
  {
    data[*data_size + i] = consecutive_split[4 - byte_length + i];
  }
  data[*data_size + byte_length] = id;
  *data_size += byte_length + 1;
}

void encode_room(uint8_t *tileIDs, uint8_t *data, unsigned long *data_size)
{
  // Find number of portals
  uint8_t num_portals = 0;
  for (uint8_t i = 0; i < MAX_PORTALS; i++)
  {
    if (!portals[i].active) break;
    num_portals++;
  }
  data[0] = num_portals;
  *data_size = 1;

  // Encode portals
  for (uint8_t i = 0; i < num_portals; i++)
  {
    uint16_t lower_mask = 0b0000000011111111;

    // Encode from
    data[*data_size + 0] = portals[i].x1 >> 8;
    data[*data_size + 1] = portals[i].x1 & lower_mask;
    data[*data_size + 2] = portals[i].y1 >> 8;
    data[*data_size + 3] = portals[i].y1 & lower_mask;

    // Encode to
    data[*data_size + 4] = portals[i].x2 >> 8;
    data[*data_size + 5] = portals[i].x2 & lower_mask;
    data[*data_size + 6] = portals[i].y2 >> 8;
    data[*data_size + 7] = portals[i].y2 & lower_mask;

    // Encode room
    data[*data_size + 8] = (portals[i].dest_room_x < 0) << 7;
    data[*data_size + 8] += abs(portals[i].dest_room_x) & 7;
    data[*data_size + 9] = (portals[i].dest_room_y < 0) << 7;
    data[*data_size + 9] += abs(portals[i].dest_room_y) & 7;
  
    // Encode pos
    data[*data_size + 10] = portals[i].dest_pos_x >> 8;
    data[*data_size + 11] = portals[i].dest_pos_x & lower_mask;
    data[*data_size + 12] = portals[i].dest_pos_y >> 8;
    data[*data_size + 13] = portals[i].dest_pos_y & lower_mask;

    *data_size += 14;
  }

  // Iterate through all the tiles
  uint8_t prev_id = tileIDs[0];
  uint32_t consecutive = 1;
  for (uint32_t i = 1; i < ROOM_X*ROOM_Y; i++)
  {
    // Add data if new ID met, or consecutive reaches 5 bytes in length
    if (tileIDs[i] != prev_id || consecutive == 4294967295)
    {
      add_data(
        prev_id, consecutive,
        data, data_size
      );
      prev_id = tileIDs[i];
      consecutive = 0;
    }

    consecutive++;
  }

  // Add the last section
  add_data(
    prev_id, consecutive,
    data, data_size
  );
}

bool save_data(int x, int y, uint8_t *data, unsigned long data_size)
{
  // Create or replace the save file
  char filename[NAME_LEN] = {0};
  snprintf(filename, NAME_LEN, "save/%d,%d.dat", x, y);
  storage_status_t status = storage_write(filename, data, (uint32_t)data_size);
  if (status != STORAGE_OK)
  {
    LOG_ERR("saving room %d,%d: %s", x, y, storage_status_str(status));
    return false;
  }
  return true;
}

bool save_room(int x, int y, uint8_t *tileIDs)
{
  uint8_t *data = file_data;
  unsigned long data_size = 0;
  encode_room(tileIDs, data, &data_size);
  return save_data(x, y, data, data_size);
}

struct online_room start_online_room(int x, int y, uint8_t *tileIDs)
{
  struct online_room room = {
    x, y,
    tileIDs, 0, 0, 0,
    {0}, 0
  };

  return room;
}

void progress_online_room(struct online_room *room, uint8_t *new_ids, uint16_t id_length)
{
  // Init room progression if first progress
  if (room->tile_progress == 0)
  {
    room->prev_id = new_ids[0];
    room->consecutive = 0;  // the loop below counts new_ids[0] itself
  }

  // Iterate through IDs in received batch
  for (uint16_t i = 0; i < id_length; i++)
  {
    // Add data if new ID met, or consecutive reaches 5 bytes in length
    if (new_ids[i] != room->prev_id || room->consecutive == 4294967295)
    {
      add_data(
        room->prev_id, room->consecutive,
        room->data, &room->data_size
      );
      room->prev_id = new_ids[i];
      room->consecutive = 0;
    }

    room->consecutive++;
  }
  room->tile_progress += id_length;
}

bool save_online_room(struct online_room *room)
{
  // Add the last section
  add_data(
    room->prev_id, room->consecutive,
    room->data, &room->data_size
  );
  room->consecutive = 0;

  return save_data(room->x, room->y, room->data, room->data_size);
}

bool delete_save(int x, int y)
{
  char filename[NAME_LEN] = {0};
  snprintf(filename, NAME_LEN, "save/%d,%d.dat", x, y);
  storage_status_t status = storage_delete(filename);
  if (status != STORAGE_OK && status != STORAGE_NOT_FOUND)
  {
    LOG_ERR("deleting save %d,%d: %s", x, y, storage_status_str(status));
    return false;
  }
  return true;
}

/* ---- moving a room to / from the FPGA ------------------------------------
 *   load:  STOP_SIM -> WRITE_CONS row 0 .. ROOM_Y-1 -> START_SIM
 *   save:  STOP_SIM -> READ_CONS  row 0 .. ROOM_Y-1 -> write the file
 * The simulation is stopped first so the grid holds still while it's
 * written or read; a save leaves it stopped, because the next room's load
 * follows and starts it. One row of room_tiles per command, up to
 * ROOM_ROWS_IN_FLIGHT at a time. The callbacks only note what came back OK
 * (or a failure); room_poll() sends everything and writes the file. */
#define ROOM_ROWS_IN_FLIGHT 4

#if FPGA_SIM_IDS_PER_BYTE != 1
#error "rooms.c sends one material ID per byte; packing two per byte isn't done here yet"
#endif

enum { JOB_IDLE, JOB_LOAD, JOB_SAVE };

static struct
{
  uint8_t kind;             // JOB_LOAD / JOB_SAVE
  room_status_t status;
  int x, y;                 // the room
  uint16_t next_row;        // next row to send a command for
  uint16_t rows_done;       // rows whose command came back OK
  uint8_t in_flight;        // row commands sent, not back yet
  bool sim_stopped;         // STOP_SIM came back OK
  bool sim_started;         // START_SIM came back OK (load)
  bool sim_in_flight;       // a STOP_SIM / START_SIM is on its way
  bool rows_logged;         // "all rows" is in the log
  bool rows_announced;      // "sending rows" is in the log (since start / resume)
} job;

static void sim_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
  job.sim_in_flight = false;
  if (err != FPGA_ERR_NONE)
  {
    // CANCELLED: never sent, because a command queued before it failed;
    // that one is what the resync line names. Only log real failures.
    if (err != FPGA_ERR_CANCELLED)
      LOG_WARN("room %d, %d: %s failed: %s", job.x, job.y,
               cmd->hdr.opcode == STOP_SIM_OPCODE ? "STOP_SIM" : "START_SIM", fpga_err_str(err));
    job.status = ROOM_FAILED;
    return;
  }
  if (cmd->hdr.opcode == STOP_SIM_OPCODE) job.sim_stopped = true;
  else job.sim_started = true;
  LOG_INFO("room %d, %d: simulation %s", job.x, job.y,
           cmd->hdr.opcode == STOP_SIM_OPCODE ? "stopped" : "started");
}

// Send STOP_SIM (run = false) or START_SIM; the queue being full just
// means "next poll".
static void send_sim(bool run)
{
  if (job.sim_in_flight) return;
  fpga_cmd_t cmd = fpga_cmd_sim_run(run);
  cmd.done = sim_done;
  fpga_status_t st = fpga_submit(&cmd);
  if (st == FPGA_OK)
  {
    job.sim_in_flight = true;
    LOG_DBG("room %d, %d: %s sent", job.x, job.y, run ? "START_SIM" : "STOP_SIM");
  }
  else if (st != FPGA_EFULL)
  {
    LOG_ERR("room: %s refused: %s", run ? "START_SIM" : "STOP_SIM", fpga_status_str(st));
    job.status = ROOM_FAILED;
  }
}

static void row_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
  job.in_flight--;
  if (err != FPGA_ERR_NONE)
  {
    if (job.status != ROOM_FAILED && err != FPGA_ERR_CANCELLED)   // see sim_done()
      LOG_WARN("room %s, row %u: %s", job.kind == JOB_LOAD ? "load" : "save",
               cmd->hdr.y, fpga_err_str(err));
    job.status = ROOM_FAILED;  // the game resyncs, then room_resume()
    return;
  }
  // Rows come back in the order they were sent, and a failure cancels
  // everything behind it, so the rows done are always rows 0..rows_done-1.
  job.rows_done++;
}

static void job_start(uint8_t kind, int x, int y)
{
  job.kind = kind;
  job.status = ROOM_BUSY;
  job.x = x;
  job.y = y;
  job.next_row = 0;
  job.rows_done = 0;
  job.in_flight = 0;
  job.sim_stopped = false;
  job.sim_started = false;
  job.sim_in_flight = false;
  job.rows_logged = false;
  job.rows_announced = false;
}

void room_load_start(int x, int y)
{
  LOG_INFO("loading room %d, %d", x, y);
  read_room(x, y, room_tiles);   // an empty room if there's no file (logged)
  job_start(JOB_LOAD, x, y);
}

void room_save_start(int x, int y)
{
  LOG_INFO("saving room %d, %d", x, y);
  job_start(JOB_SAVE, x, y);
}

bool room_resume(void)
{
  if (job.status != ROOM_BUSY && job.status != ROOM_FAILED) return false;
  // After fpga_abort_all() nothing is in flight. room_tiles still holds the
  // room (load) or the rows read so far (save): carry on after the last
  // row that came back OK.
  // A STOP_SIM that came back OK still holds (same FPGA session).
  job.status = ROOM_BUSY;
  job.next_row = job.rows_done;
  job.in_flight = 0;
  job.sim_in_flight = false;
  job.rows_announced = false;
  LOG_INFO("room %d, %d: %s resumed at row %u", job.x, job.y,
           job.kind == JOB_LOAD ? "load" : "save", job.rows_done);
  return true;
}

room_status_t room_poll(void)
{
  if (job.status != ROOM_BUSY) return job.status;

  // 1. Stop the simulation first, and wait until it has.
  if (!job.sim_stopped)
  {
    send_sim(false);
    return job.status;
  }

  // 3. Every row is in (only now -- not when the last row is sent).
  if (job.rows_done == ROOM_Y)
  {
    if (!job.rows_logged)
    {
      job.rows_logged = true;
      LOG_INFO("room %d, %d: all %u rows %s", job.x, job.y, (unsigned)ROOM_Y,
               job.kind == JOB_LOAD ? "sent" : "read back");
    }
    if (job.kind == JOB_LOAD && !job.sim_started)
    {
      send_sim(true);                  // the new room runs from here
      return job.status;
    }
    if (job.kind == JOB_SAVE) save_room(job.x, job.y, room_tiles);  // no card: logged
    LOG_INFO("room %d, %d %s", job.x, job.y, job.kind == JOB_LOAD ? "loaded" : "saved");
    job.status = ROOM_DONE;
    return job.status;
  }

  // 2. The rows.
  if (!job.rows_announced)
  {
    job.rows_announced = true;
    LOG_INFO("room %d, %d: %s rows %u..%u (%u cells each)", job.x, job.y,
             job.kind == JOB_LOAD ? "sending" : "reading", job.rows_done,
             (unsigned)ROOM_Y - 1u, (unsigned)ROOM_X);
  }
  // Send the next rows, as many as may be in flight.
  while (job.next_row < ROOM_Y && job.in_flight < ROOM_ROWS_IN_FLIGHT)
  {
    uint8_t *row = &room_tiles[job.next_row * ROOM_X];
    fpga_cmd_t cmd = job.kind == JOB_LOAD
      ? fpga_cmd_write_cons(0, job.next_row, row, ROOM_X)
      : fpga_cmd_read_cons(0, job.next_row, row, ROOM_X);
    cmd.done = row_done;

    fpga_status_t st = fpga_submit(&cmd);
    if (st == FPGA_EFULL) break;       // queue full: the rest next poll
    if (st != FPGA_OK)
    {
      LOG_ERR("room row %u refused: %s", job.next_row, fpga_status_str(st));
      job.status = ROOM_FAILED;
      break;
    }
    job.in_flight++;
    job.next_row++;
  }
  return job.status;
}

void portals_init()
{
  for (uint8_t i = 0; i < MAX_PORTALS; i++)
  {
    portal_t portal = {
      .active=false,
      .x1=0, .y1=0,
      .x2=0, .y2=0,
      .dest_room_x=0, .dest_room_y=0,
      .dest_pos_x=0, .dest_pos_y=0
    };
    portals[i] = portal;
  }
}

/*
@brief This function checks if a position overlaps with a portal.

@param x1: The x-coordinate of one side of the area to check
@param x2: The x-coordinate of one side of the area to check
@param y1: The y-coordinate of one side of the area to check
@param y2: The y-coordinate of one side of the area to check

@return Returns -1 if position doesn't overlap with a portal.
        Otherwise, the index of the portal will be returned.
*/
portal_t *area_in_portal(float x1, float x2, float y1, float y2)
{
  // Check every active portal until hit
  for (uint8_t i = 0; i < MAX_PORTALS; i++)
  {
    if (!portals[i].active) break;

    // Portal edges
    uint16_t px1 = portals[i].x1;
    uint16_t py1 = portals[i].y1;
    uint16_t px2 = portals[i].x2;
    uint16_t py2 = portals[i].y2;

    bool x_overlap = false;
    if (px1 <= x1 && x1 <= px2) x_overlap = true;
    if (px1 <= x2 && x2 <= px2) x_overlap = true;
    if (x1 <= px1 && px1 <= x2) x_overlap = true;
    if (x1 <= px2 && px2 <= x2) x_overlap = true;
    bool y_overlap = false;
    if (py1 <= y1 && y1 <= py2) y_overlap = true;
    if (py1 <= y2 && y2 <= py2) y_overlap = true;
    if (y1 <= py1 && py1 <= y2) y_overlap = true;
    if (y1 <= py2 && py2 <= y2) y_overlap = true;

    if (x_overlap && y_overlap)
    {
      return portals+i;
    }
  }

  return NULL;
}

bool id_is(uint8_t id, uint8_t target)
{
  return (id & 0b11110000) == target;
}
