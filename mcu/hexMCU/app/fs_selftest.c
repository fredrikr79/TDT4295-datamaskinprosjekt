/* ======================================================================
 * app/fs_selftest.c -- mount the volume, write a text file, read it back.
 *
 * Portable: identical code runs on the board (SDMMC) and on host
 * (card.img). Whichever diskio_*.c is linked decides where it lands.
 * ====================================================================== */

#define LOG_TAG "fstest"

#include <string.h>

#include "ff.h"
#include "log.h"

/* Must stay alive for as long as the volume is mounted -- f_mount only
 * stores the pointer, it does not copy. Never put this on the stack. */
static FATFS fs;

static const char *fres_str(FRESULT fr)
{
    switch (fr) {
    case FR_OK:                  return "OK";
    case FR_DISK_ERR:            return "DISK_ERR";
    case FR_INT_ERR:             return "INT_ERR";
    case FR_NOT_READY:           return "NOT_READY";
    case FR_NO_FILE:             return "NO_FILE";
    case FR_NO_PATH:             return "NO_PATH";
    case FR_INVALID_NAME:        return "INVALID_NAME";
    case FR_DENIED:              return "DENIED";
    case FR_EXIST:               return "EXIST";
    case FR_WRITE_PROTECTED:     return "WRITE_PROTECTED";
    case FR_INVALID_DRIVE:       return "INVALID_DRIVE";
    case FR_NOT_ENABLED:         return "NOT_ENABLED";
    case FR_NO_FILESYSTEM:       return "NO_FILESYSTEM";
    case FR_TIMEOUT:             return "TIMEOUT";
    case FR_NOT_ENOUGH_CORE:     return "NOT_ENOUGH_CORE";
    default:                     return "?";
    }
}

int fs_selftest(void)
{
    static const char msg[] = "Hello, world!\n";
    char    buf[64];
    FRESULT fr;
    FIL     f;
    UINT    bw = 0, br = 0;
    int     rc = -1;

    /* "" = default drive, 1 = mount now rather than on first access,
     * so a bad card shows up here instead of inside f_open. */
    fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        LOG_ERR("f_mount: %s (%d)", fres_str(fr), fr);
        return -1;
    }

    /* ---- write ---- */

    fr = f_open(&f, "HELLO.TXT", FA_WRITE | FA_CREATE_ALWAYS);
    if (fr != FR_OK) {
        LOG_ERR("f_open write: %s (%d)", fres_str(fr), fr);
        goto out_unmount;
    }

    fr = f_write(&f, msg, sizeof msg - 1, &bw);   /* -1 drops the NUL */
    if (fr != FR_OK) {
        LOG_ERR("f_write: %s (%d)", fres_str(fr), fr);
        f_close(&f);
        goto out_unmount;
    }
    if (bw != sizeof msg - 1) {
        /* Short write with FR_OK means the volume is full. */
        LOG_ERR("short write: %u of %u bytes", bw, (unsigned)(sizeof msg - 1));
        f_close(&f);
        goto out_unmount;
    }

    /* f_close flushes the file and updates the directory entry. Without
     * it the data can be on the card but the file still shows 0 bytes. */
    fr = f_close(&f);
    if (fr != FR_OK) {
        LOG_ERR("f_close: %s (%d)", fres_str(fr), fr);
        goto out_unmount;
    }

    LOG_INFO("wrote %u bytes to HELLO.TXT", bw);

    /* ---- read back ---- */

    fr = f_open(&f, "HELLO.TXT", FA_READ);
    if (fr != FR_OK) {
        LOG_ERR("f_open read: %s (%d)", fres_str(fr), fr);
        goto out_unmount;
    }

    LOG_INFO("size on disk: %lu bytes", (unsigned long)f_size(&f));

    fr = f_read(&f, buf, sizeof buf - 1, &br);
    f_close(&f);
    if (fr != FR_OK) {
        LOG_ERR("f_read: %s (%d)", fres_str(fr), fr);
        goto out_unmount;
    }

    /* br < requested is normal here: it just means EOF. Only treat a
     * short read as an error when you know how much should be there. */
    buf[br] = '\0';
    LOG_INFO("read back %u bytes: \"%s\"", br, buf);

    if (br == sizeof msg - 1 && memcmp(buf, msg, br) == 0) {
        LOG_INFO("selftest OK");
        rc = 0;
    } else {
        LOG_ERR("content mismatch");
    }

out_unmount:
    f_unmount("");
    return rc;
}
