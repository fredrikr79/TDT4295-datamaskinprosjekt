#include <stdint.h>
#include <string.h>

#include "fx_api.h"
#include "game.h"
#include "main.h"
#include "rooms.h"
#include "app_filex.h"
#include "fpga.h"

#define CHECK_SUCCESS() if (status != FX_SUCCESS) Error_Handler()

FX_MEDIA media;
UCHAR media_memory[FX_STM32_SD_DEFAULT_SECTOR_SIZE] = {0};

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

void read_room(int x, int y, uint8_t *tileIDs)
{
  unsigned int status;
  FX_FILE file;
  unsigned long bytes_read;
  uint8_t data[DATA_MAX_SIZE] = {0};

  status = fx_media_open(
    &media,
    "HeksSD",
    fx_stm32_sd_driver,
    0,
    (void *) media_memory,
    sizeof(media_memory)
  );
  CHECK_SUCCESS();

  // Open save file is exists, otherwise, open from rom
  char filename_rom[16] = {0};
  snprintf(filename_rom, 16, "rom/%d,%d.dat", x, y);
  char filename_save[16] = {0};
  snprintf(filename_save, 16, "save/%d,%d.dat", x, y);
  status = fx_file_open(
    &media,
    &file,
    filename_save,
    FX_OPEN_FOR_READ
  );
  if (FX_SUCCESS != status)
  {
    status = fx_file_open(
      &media,
      &file,
      filename_rom,
      FX_OPEN_FOR_READ
    );
    CHECK_SUCCESS();
  }

  // Read open file
  unsigned long request_size = file.fx_file_current_file_size;
  if (DATA_MAX_SIZE < file.fx_file_current_file_size) request_size = DATA_MAX_SIZE;
  status = fx_file_read(
    &file,
    data,
    request_size,
    &bytes_read
  );
  CHECK_SUCCESS();

  // Decode file data
  decode_room(data, bytes_read, tileIDs);

  status = fx_file_close(&file);
  CHECK_SUCCESS();

  status = fx_media_close(&media);
  CHECK_SUCCESS();
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

void save_data(int x, int y, uint8_t *data, unsigned long data_size)
{
  unsigned int status;
  FX_FILE file;

  status = fx_media_open(
    &media,
    "HeksSD",
    fx_stm32_sd_driver,
    0,
    (void *) media_memory,
    sizeof(media_memory)
  );
  CHECK_SUCCESS();

  // Create and open save file
  char filename[16] = {0};
  snprintf(filename, 16, "save/%d,%d.dat", x, y);
  status = fx_file_create(&media,filename);
  if (status != FX_SUCCESS)
  {
    if (status == FX_ALREADY_CREATED)
    {
      // Delete old save file and create a new one
      status = fx_file_delete(&media, filename);
      CHECK_SUCCESS();
      status = fx_file_create(&media,filename);
      CHECK_SUCCESS();
    }
    else Error_Handler();
  }
  status = fx_file_open(
    &media,
    &file,
    filename,
    FX_OPEN_FOR_WRITE
  );
  CHECK_SUCCESS();

  status = fx_file_write(
    &file,
    data,
    data_size
  );
  CHECK_SUCCESS();

  status = fx_file_close(&file);
  CHECK_SUCCESS();

  status = fx_media_close(&media);
  CHECK_SUCCESS();
}

void save_room(int x, int y, uint8_t *tileIDs)
{
  uint8_t data[DATA_MAX_SIZE] = {0};
  uint32_t data_size = 0;
  encode_room(tileIDs, data, &data_size);
  save_data(x, y, data, data_size);
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

void save_online_room(struct online_room *room)
{
  // Add the last section
  add_data(
    room->prev_id, room->consecutive,
    room->data, &room->data_size
  );
  room->consecutive = 0;

  save_data(room->x, room->y, room->data, room->data_size);
}

void delete_save(int x, int y)
{
  unsigned int status;

  status = fx_media_open(
    &media,
    "HeksSD",
    fx_stm32_sd_driver,
    0,
    (void *) media_memory,
    sizeof(media_memory)
  );
  CHECK_SUCCESS();

  char filename[16] = {0};
  snprintf(filename, 16, "save/%d,%d.dat", x, y);
  status = fx_file_delete(&media, filename);
  if (status != FX_SUCCESS && status != FX_NOT_FOUND) Error_Handler();

  status = fx_media_close(&media);
  CHECK_SUCCESS();
}

void send_next_line(const fpga_cmd_t *prev_cmd, fpga_err_t err)
{
  fpga_cmd_t cmd = fpga_cmd_send_line(
    0, prev_cmd->hdr.y + 1, ROOM_X, prev_cmd->tx+ROOM_X, ROOM_X
  );
  if (cmd.hdr.y + 1 < ROOM_Y) cmd.done = send_next_line;
  else load_finish();
  fpga_submit(&cmd);
}

void send_room_to_fpga(uint8_t *tileIDs)
{
  fpga_cmd_t cmd = fpga_cmd_send_line(0, 0, ROOM_X, tileIDs, ROOM_X);
  cmd.done = send_next_line;
  fpga_submit(&cmd);
}

void read_next_line(const fpga_cmd_t *prev_cmd, fpga_err_t err)
{
  fpga_cmd_t cmd = fpga_cmd_read_line(
    0, prev_cmd->hdr.y + 1, ROOM_X, prev_cmd->rx+ROOM_X, ROOM_X
  );
  if (cmd.hdr.y + 1 < ROOM_Y) cmd.done = read_next_line;
  else save_finish();
  fpga_submit(&cmd);
}

void read_room_from_fpga(uint8_t *tile_buffer)
{
  fpga_cmd_t cmd = fpga_cmd_read_line(0, 0, ROOM_X, tile_buffer, ROOM_X);
  cmd.done = read_next_line;
  fpga_submit(&cmd);
}

void init_empty_portals(struct portal *portals, uint8_t n_portals)
{
  for (uint8_t i = 0; i < n_portals; i++)
  {
    struct portal prt = {
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
int pos_in_portal(uint8_t x, uint8_t y, struct portal *portals)
{
  for (uint8_t i = 0; i < MAX_PORTALS; i++)
  {
    if (!portals[i].active) break;

    if (
      portals[i].x < x
      && x < portals[i].x + portals[i].dx
      && portals[i].y < y
      && x < portals[i].y + portals[i].dy
    )
    {
      return i;
    }
  }

  return -1;
}
