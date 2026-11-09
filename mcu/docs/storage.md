# Storage: files on the SD card

[← back to the overview](README.md)

One small file API for the SD card on the MCU and a plain folder on the PC.
`rooms.c` and `sprite.c` use it.

```
rooms.c, sprite.c            "read 1 KB of sprites/cursor.spr from byte 8"
        │
    storage.h                storage_read / write / size / delete
        │
 ┌──────┴────────┐
storage_filex.c   storage_host.c       only one is compiled into each build
(SD card, FileX)  (a folder on the PC)
```

---

## The API

```c
storage_status_t storage_read(const char *path, uint32_t offset, void *buf,
                              uint32_t max, uint32_t *got);
storage_status_t storage_size(const char *path, uint32_t *size);
storage_status_t storage_write(const char *path, const void *data, uint32_t len);
storage_status_t storage_append(const char *path, const void *data, uint32_t len);
storage_status_t storage_delete(const char *path);
storage_status_t storage_init(void);     /* optional: mount now */
bool             storage_ready(void);    /* mounted right now?  */
const char      *storage_status_str(storage_status_t st);
```

| Status | Meaning |
|---|---|
| `STORAGE_OK` | done |
| `STORAGE_NO_CARD` | no card (or folder), or it couldn't be mounted |
| `STORAGE_NOT_FOUND` | no such file |
| `STORAGE_FULL` | the card is full |
| `STORAGE_ERROR` | anything else: an I/O error, a bad path… |

- **Paths** are relative to the card's root, with `/` between folders:
  `rom/0,0.dat`, `save/0,0.dat`, `sprites/cursor.spr`. At most 63 characters on
  the MCU.
- **`storage_read`** reads up to `max` bytes starting at `offset`; `*got` says how
  many came. Near the end of the file that's fewer than `max`, and past the end
  it's 0 (still `STORAGE_OK`). **Big files are read piece by piece by moving
  `offset` forward**, which is how `sprite.c` streams sprite files.
- **`storage_write`** creates or replaces the whole file and makes any missing
  folders. `storage_write(path, NULL, 0)` leaves an empty file.
- **`storage_append`** adds bytes to the end of the file, making it (and its
  folders) if it's missing. Together with `storage_write` it saves something too
  big for RAM piece by piece:
  ```c
  storage_write(path, NULL, 0);          /* start empty           */
  storage_append(path, piece, n);        /* ...as each piece is ready */
  ```
  Each call is flushed to the card before it returns, so a save cut off halfway
  keeps the pieces written so far.
- **Everything blocks** (about 0.1–1 ms per 512-byte sector on the card). Call it
  from the main loop only, never from an interrupt or an FPGA callback.

## Mounting: lazy, with retries

You never have to set anything up. Every call starts with `mount()`:

1. **Already mounted:** carry on.
2. **Not mounted, and the last try was under a second ago**
   (`STORAGE_RETRY_MS`): return `STORAGE_NO_CARD` at once. A missing card doesn't
   stall the game over and over.
3. **Otherwise, try:**
   - On the MCU, `sd_init()` starts the card, then `fx_media_open()` reads its
     FAT file system.
   - On the PC, it checks that the folder exists.

So a card plugged in later is found within a second. `storage_init()` is just
`mount()`; calling it at startup only gets "SD card mounted" or "no SD card" into
the log early.

## MCU backend: storage_filex.c

### Starting the SD card without hanging

CubeMX's `MX_SDMMC1_SD_Init()` calls `Error_Handler()` when `HAL_SD_Init()`
fails, and with no card it always fails. The board would then hang (interrupts
off, endless loop) before `myMain()` even starts. So:

- In CubeMX → Project Manager → Advanced Settings, `MX_SDMMC1_SD_Init` has
  **Do Not Generate Function Call** ticked and **Visibility (Static)** unticked.
- `MX_FileX_Init()` stays; it only calls `fx_system_initialize()`.
- `FX_STM32_SD_INIT` stays 0 in `fx_stm32_sd_driver.h`. Otherwise the FileX
  driver would call `MX_SDMMC1_SD_Init()` itself.
- `sd_init()` does the same setup as CubeMX (4-bit bus, clock divider 0…) but
  returns false instead of hanging. **Its settings must match CubeMX's SDMMC1
  page:** change both together.
- The SD handle `hsd1` is declared weak in this file, so it links whether or not
  CubeMX still generates one in main.c. `HAL_SD_MspInit()` in
  `stm32u5xx_hal_msp.c` (SDMMC1 clock and pins) must still be generated, or the
  card can never answer.

### One call, step by step

`storage_read("rom/0,0.dat", offset, buf, max, &got)`:
1. `mount()`.
2. Copy the path into a local buffer (FileX wants a non-const `CHAR *`).
3. `fx_file_open` → `fx_file_seek(offset)` → `fx_file_read(max)` → `fx_file_close`.

`storage_write`:
1. Create any missing folders, one level at a time (`fx_directory_create`).
2. Create the file, or delete and recreate it if it exists.
3. Write it, close it, then `fx_media_flush()`. FileX keeps file system updates
   in memory; without the flush, losing power or pulling the card can lose the
   file.

`storage_append` is the same, except an existing file is kept: after opening
(which starts at the beginning), it seeks to the end with
`fx_file_relative_seek(&f, 0, FX_SEEK_END)` before writing.

The card stays mounted between calls.

### Errors

Every FileX status goes through `result()`:
- **Our mistake** (not found, bad name, already exists, not a file): just return
  the error.
- **Card trouble** (I/O error, card gone, anything else): `drop()` abandons the
  open file system without writing anything (`fx_media_abort`) and switches the
  card off with `HAL_SD_DeInit`. The next call mounts from scratch. That's how a
  card pulled out and put back is found again.

## Host backend: storage_host.c

- **The folder:** `$HEXMCU_SD` if set, else `STORAGE_HOST_ROOT` (default `sd`,
  relative to where the program runs). It's easiest to give CMake an absolute
  path:
  ```cmake
  target_compile_definitions(hexmcu_host_common PRIVATE
      STORAGE_HOST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/sd")
  ```
- Lay it out like the card: `sd/rom/…`, `sd/save/…`, `sd/sprites/…`.
- **No folder behaves exactly like no card**, which is handy for testing that
  path.
- Paths containing `..` are refused, so nothing outside the folder can be touched.
- Plain `fopen`/`fread`/`fwrite`/`remove` (`"ab"` for append). Missing folders
  are made on write.

## Who uses it

**`rooms.c`:**
- `read_room(x, y, tiles)` reads `save/x,y.dat` if it exists, else `rom/x,y.dat`,
  and decodes the run-length data into tile IDs. If neither can be read, it logs
  an error and gives an empty room (all tile 0), so the game goes on.
- `save_room()` encodes and writes `save/x,y.dat`. `delete_save()` removes it.
- The encoded file is at most `DATA_MAX_SIZE` (~4.9 KB for the 40 × 60 test room),
  held in one static buffer.

**`sprite.c`:** reads an 8-byte header when an image is registered, then the
pixels 1 KB at a time while uploading ([sprite.md](sprite.md)).

## Later: streaming full-size rooms

The real rooms will be a whole frame of simulation cells. At 640 × 360 and one
byte per cell that's ~225 KB decoded, nearly all of the U545's RAM, so a room
can't be held in memory whole. The plan:

- **Two buffers taking turns:** fill one from the card while the FPGA DMA sends
  the other. A buffer is free again in its command's done callback.
- **Read in the main loop.** Callbacks only mark a buffer as free; the poll
  function refills it.
- **Decode as you go.** The run-length decoder keeps its state between pieces:
  the current tile ID, how many are left in the run, and any header bytes cut off
  at the end of a piece. It produces one grid row (one WRITE_CONS) at a time.
- **Save in reverse:** READ_CONS pieces go to the incremental encoder
  (`progress_online_room()`). It only writes out runs that are finished; the run
  still in progress stays in `prev_id`/`consecutive`. So after each batch you can
  `storage_append()` the bytes in `room->data`, set `data_size = 0`, and carry
  on. The last run goes out in `save_online_room()`.

`storage_read` with an offset already supports this. For hundreds of pieces per
room, an open / read-next / close interface would save looking the file up on
the card for every piece.
