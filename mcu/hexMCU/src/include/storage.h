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

/* Create or replace the whole file. Missing folders are made. */
storage_status_t storage_write(const char *path, const void *data, uint32_t len);

/* STORAGE_NOT_FOUND if it wasn't there. */
storage_status_t storage_delete(const char *path);

const char *storage_status_str(storage_status_t st);

#endif