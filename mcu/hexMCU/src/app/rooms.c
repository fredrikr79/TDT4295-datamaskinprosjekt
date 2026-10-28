#define LOG_TAG "rooms"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game.h"
#include "rooms.h"
#include "storage.h"
#include "log.h"
#include "fpga.h"

/* Room files are DATA_MAX_SIZE (~5 KB): too big for the MCU stack */
static uint8_t file_data[DATA_MAX_SIZE];

#define NAME_LEN 32     /* "save/-1000,-1000.dat" fits */

void decode_room(uint8_t *data, uint32_t data_length, uint8_t *tileIDs)
{
  uint32_t data_progress = 0;
  uint32_t tile_progress = 0;
  uint8_t current_id = 0;

  // Iterate through sections
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
    for (int i = 1; i < varible_number_bytes; i++)
    {
      header_mask |= (header_mask >> 1);
    }
    data[data_progress] &= ~header_mask;


    // Construct number of consecutive IDs
    uint32_t consecutive_IDs = 0;
    for (int i = 0; i < varible_number_bytes && i < 4; i++)
    {
      consecutive_IDs |= ((uint32_t) data[data_progress + varible_number_bytes - 1 - i]) << 8*i;
    }

    // Fill in IDs in array
    current_id = data[data_progress + varible_number_bytes];
    for (int i = 0; i < consecutive_IDs && tile_progress + i < ROOM_X*ROOM_Y; i++)
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
  uint8_t prev_id = tileIDs[0];
  uint32_t consecutive = 1;

  // Iterate through all the tiles
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
    room->consecutive = 1;
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

void send_next_line(const fpga_cmd_t *prev_cmd, fpga_err_t err)
{
  fpga_cmd_t cmd = fpga_cmd_write_cons(
    0, prev_cmd->hdr.y + 1, prev_cmd->tx+ROOM_X, ROOM_X
  );
  if (cmd.hdr.y + 1 < ROOM_Y) cmd.done = send_next_line;
  else load_finish();
  fpga_submit(&cmd);
}

void send_room_to_fpga(uint8_t *tileIDs)
{
  fpga_cmd_t cmd = fpga_cmd_write_cons(0, 0, tileIDs, ROOM_X);
  cmd.done = send_next_line;
  fpga_submit(&cmd);
}

void read_next_line(const fpga_cmd_t *prev_cmd, fpga_err_t err)
{
  fpga_cmd_t cmd = fpga_cmd_read_cons(
    0, prev_cmd->hdr.y + 1, prev_cmd->rx+ROOM_X, ROOM_X
  );
  if (cmd.hdr.y + 1 < ROOM_Y) cmd.done = read_next_line;
  else save_finish();
  fpga_submit(&cmd);
}

void read_room_from_fpga(uint8_t *tile_buffer)
{
  fpga_cmd_t cmd = fpga_cmd_read_cons(0, 0, tile_buffer, ROOM_X);
  cmd.done = read_next_line;
  fpga_submit(&cmd);
}

void init_empty_portals(portal_t *portals, uint8_t n_portals)
{
  for (uint8_t i = 0; i < n_portals; i++)
  {
    portal_t prt = {
      false,
      0, 0,
      0, 0,
      false, false,
      0
    };
    portals[i] = prt;
  }
}

/*
@brief This function checks if a position overlaps with a portal.

@param x: The x-coordinate of the position to check
@param y: The y-coordinate of the position to check
@param *portals: The array of portals to check against

@return Returns -1 if position doesn't overlap with a portal.
        Otherwise, the index of the portal will be returned.
*/
int pos_in_portal(uint8_t x, uint8_t y, portal_t *portals)
{
  for (uint8_t i = 0; i < MAX_PORTALS; i++)
  {
    if (!portals[i].active) break;

    if (
      portals[i].x < x
      && x < portals[i].x + portals[i].dx
      && portals[i].y < y
      && y < portals[i].y + portals[i].dy
    )
    {
      return i;
    }
  }

  return -1;
}