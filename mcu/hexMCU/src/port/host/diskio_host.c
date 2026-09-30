/* ======================================================================
 * port/host/diskio_host.c -- FatFs disk layer backed by a plain image file.
 *
 * Links in place of port/stm32/diskio_sdmmc.c. Same ffconf.h, same ff.c,
 * so the FAT code path exercised here is the one that runs on the board.
 *
 * Image is a raw, unpartitioned FAT volume (see tools/mkcard.sh).
 * ====================================================================== */

#define _FILE_OFFSET_BITS 64
#define LOG_TAG "disk"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ff.h"
#include "diskio.h"
#include "log.h"
#include "host_hooks.h"

#define SECTOR_SIZE 512u

/* Set by CMake to <source>/host/card.img so a bare ./hexmcu_host works
 * from any directory. The fallback keeps this file self-contained. */
#ifndef DEFAULT_CARD_IMAGE
#define DEFAULT_CARD_IMAGE "card.img"
#endif

static FILE       *img;
static DSTATUS     drv_status = STA_NOINIT;
static LBA_t       sector_count;
static const char *img_path = DEFAULT_CARD_IMAGE;

/* Error injection: next N read/write calls fail. Set from the CLI. */
static int fail_reads;
static int fail_writes;

/* ---------------------------------------------------------- host hooks */

/* argv and the environment outlive us, so keeping the pointer is fine. */
void host_disk_set_image(const char *path)
{
    img_path = path;
}

void host_disk_fail_next_reads(int n)  { fail_reads  = n; }
void host_disk_fail_next_writes(int n) { fail_writes = n; }

const char *host_disk_image_path(void)
{
    return img_path;
}

/* Charge virtual time for SD access so the streaming pipeline on host
 * has the same shape as on hardware. Fill in once vclock is wired up:
 * roughly (sectors * 512) / throughput, plus a fixed per-command cost. */
static void charge_time(UINT sectors)
{
    (void)sectors;
    /* TODO: vclock_advance_us(SD_CMD_OVERHEAD_US +
     *                        sectors * SD_US_PER_SECTOR); */
}

/* ------------------------------------------------------------- FatFs API */

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    return drv_status;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    if (img != NULL) return drv_status;

    img = fopen(img_path, "r+b");
    if (img == NULL) {
        LOG_ERR("cannot open image '%s'", img_path);
        drv_status = STA_NOINIT;
        return drv_status;
    }

    /* No stdio buffering: every disk_read must turn into one real read,
     * otherwise access patterns and timing are meaningless here. */
    setvbuf(img, NULL, _IONBF, 0);

    if (fseeko(img, 0, SEEK_END) != 0) {
        LOG_ERR("seek to end failed");
        fclose(img);
        img = NULL;
        return drv_status;
    }

    off_t size = ftello(img);
    if (size <= 0) {
        LOG_ERR("image is empty");
        fclose(img);
        img = NULL;
        return drv_status;
    }
    if (size % SECTOR_SIZE != 0) {
        LOG_WARN("image size %lld is not a multiple of %u, tail ignored",
                 (long long)size, SECTOR_SIZE);
    }

    sector_count = (LBA_t)(size / SECTOR_SIZE);
    drv_status   = 0;

    LOG_INFO("opened '%s': %llu sectors (%lld KiB)",
             img_path, (unsigned long long)sector_count,
             (long long)(size / 1024));
    return drv_status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || img == NULL)          return RES_NOTRDY;
    if (drv_status & STA_NOINIT)           return RES_NOTRDY;
    if (count == 0)                        return RES_PARERR;
    if (sector + count > sector_count) {
        LOG_ERR("read past end: sector %llu count %u (have %llu)",
                (unsigned long long)sector, count,
                (unsigned long long)sector_count);
        return RES_PARERR;
    }

    if (fail_reads > 0) {
        fail_reads--;
        LOG_WARN("injected read error at sector %llu",
                 (unsigned long long)sector);
        return RES_ERROR;
    }

    if (fseeko(img, (off_t)sector * SECTOR_SIZE, SEEK_SET) != 0)
        return RES_ERROR;

    if (fread(buff, SECTOR_SIZE, count, img) != count)
        return RES_ERROR;

    charge_time(count);
    return RES_OK;
}

#if !FF_FS_READONLY

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0 || img == NULL)          return RES_NOTRDY;
    if (drv_status & STA_NOINIT)           return RES_NOTRDY;
    if (count == 0)                        return RES_PARERR;
    if (sector + count > sector_count)     return RES_PARERR;

    if (fail_writes > 0) {
        fail_writes--;
        LOG_WARN("injected write error at sector %llu",
                 (unsigned long long)sector);
        return RES_ERROR;
    }

    if (fseeko(img, (off_t)sector * SECTOR_SIZE, SEEK_SET) != 0)
        return RES_ERROR;

    if (fwrite(buff, SECTOR_SIZE, count, img) != count)
        return RES_ERROR;

    charge_time(count);
    return RES_OK;
}

#endif /* !FF_FS_READONLY */

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0 || img == NULL) return RES_NOTRDY;

    switch (cmd) {
    case CTRL_SYNC:
        return (fflush(img) == 0) ? RES_OK : RES_ERROR;

    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = sector_count;
        return RES_OK;

    case GET_SECTOR_SIZE:
        *(WORD *)buff = SECTOR_SIZE;
        return RES_OK;

    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;          /* unknown erase block size */
        return RES_OK;

    case CTRL_TRIM:
        return RES_OK;

    default:
        return RES_PARERR;
    }
}

/* Only referenced when FF_FS_READONLY == 0 && FF_FS_NORTC == 0.
 * The stm32 port has its own copy returning a fixed date (or the RTC). */
#if !FF_FS_READONLY && !FF_FS_NORTC

DWORD get_fattime(void)
{
    time_t     now = time(NULL);
    struct tm *t   = localtime(&now);

    return ((DWORD)(t->tm_year - 80) << 25)
         | ((DWORD)(t->tm_mon + 1)   << 21)
         | ((DWORD)t->tm_mday        << 16)
         | ((DWORD)t->tm_hour        << 11)
         | ((DWORD)t->tm_min         <<  5)
         | ((DWORD)t->tm_sec         >>  1);
}

#endif