#ifndef STORAGE_H
#define STORAGE_H

/* Files: the SD card on the MCU, a folder on the PC. Portable interface,
 * one backend per port:
 *
 *   port/stm32/storage_filex.c   FileX on the SD card (SDMMC1)
 *   port/host/storage_host.c     files under a local folder (default
 *                                "sd", or $HEXMCU_SD), laid out like the card
 *
 * Paths are relative to the card's root, with '/' between folders:
 * "rom/0,0.dat", "save/0,0.dat", "sprites/cursor.bin".
 *
 * Nothing here stops the program: a missing card or file is an error code,
 * and the caller decides what to do (an empty room, a plain sprite...).
 * The card is mounted on first use; while it is missing, a new try is made
 * at most once per STORAGE_RETRY_MS, so a card plugged in later is found.
 *
 * Blocking, main loop only (never from an interrupt or a done callback
 * that runs from one). */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    STORAGE_OK = 0,
    STORAGE_NO_CARD,        /* no card / folder, or it can't be mounted  */
    STORAGE_NOT_FOUND,      /* no such file                              */
    STORAGE_FULL,           /* no room left on the card                  */
    STORAGE_ERROR,          /* anything else (I/O error, bad path...)    */
} storage_status_t;

#ifndef STORAGE_RETRY_MS
#define STORAGE_RETRY_MS 1000u
#endif

/* Mount now (optional -- every call below mounts if needed). Call it at
 * start-up to get "SD card: ok / missing" in the log early. */
storage_status_t storage_init(void);
bool             storage_ready(void);       /* mounted right now */

/* Read up to max bytes from offset on; *got = bytes read (fewer at the
 * end of the file, 0 past it). Read a big file piece by piece by moving
 * offset. */
storage_status_t storage_read(const char *path, uint32_t offset, void *buf,
                              uint32_t max, uint32_t *got);

storage_status_t storage_size(const char *path, uint32_t *size);

/* Create or replace the whole file. Missing folders are made.
 * storage_write(path, NULL, 0) leaves an empty file. */
storage_status_t storage_write(const char *path, const void *data, uint32_t len);

/* Add len bytes to the end of the file; it (and its folders) is made if
 * missing. For saving something too big for RAM piece by piece:
 *
 *   storage_write(path, NULL, 0);          start empty
 *   storage_append(path, piece, n);        ...as each piece is ready
 *
 * Each call is on the card when it returns (flushed), so a save cut off
 * halfway keeps the pieces written so far. */
storage_status_t storage_append(const char *path, const void *data, uint32_t len);

/* STORAGE_NOT_FOUND if it wasn't there. */
storage_status_t storage_delete(const char *path);

/* Rename from -> to, replacing `to` if it exists. For saving safely: write
 * the new version under a temporary name, then rename it over the old one,
 * so a save that's cut off never replaces the last good file. */
storage_status_t storage_rename(const char *from, const char *to);

const char *storage_status_str(storage_status_t st);

/* ---- status, for the console ("sd", "sd ls") -------------------------------
 * storage_info(): tries to mount right away if not mounted (the console
 * asked: no STORAGE_RETRY_MS wait), then says
 * what's there. `note` is the last thing that happened, in words ("SD card
 * mounted", "no SD card (init failed after 2 ms, error 0x00000004)"...).
 * total / free are bytes, 0 if not known. */
typedef struct {
    bool        mounted;
    uint64_t    total, free;
    const char *where;          /* "SD card" / the host folder              */
    const char *note;
} storage_info_t;

storage_status_t storage_info(storage_info_t *info);

/* Every entry in folder dir ("" = the root), "." and ".." left out. */
typedef void (*storage_list_fn)(void *ctx, const char *name, uint32_t size, bool is_dir);
storage_status_t storage_list(const char *dir, storage_list_fn fn, void *ctx);

#endif