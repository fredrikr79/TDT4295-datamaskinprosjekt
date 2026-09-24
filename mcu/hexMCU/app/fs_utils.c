/* ======================================================================
 * fs_utils.c -- ls / cd / cat / echo on the FAT volume.
 *
 * Portable: talks only to ff.h, io.h and log.h. Whichever diskio_*.c is
 * linked decides whether this hits an SD card or card.img.
 *
 * All FatFs objects here are static on purpose. FIL contains a 512-byte
 * sector buffer (FF_FS_TINY == 0), which is more than the firmware's
 * default stack wants to give up. Only one command runs at a time, so
 * sharing them is fine -- but do NOT call two of these re-entrantly.
 * ====================================================================== */
#define LOG_TAG "fs"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "fs_utils.h"
#include "io.h"
#include "log.h"

#define IO_BUF_SIZE   128
#define PATH_MAX_LEN  128

static FATFS   fs;              /* must outlive the mount */
static FIL     file;
static DIR     dir;
static FILINFO fno;
static char    iobuf[IO_BUF_SIZE];
static bool    mounted;

/* ======================================================================
 * Mount
 * ====================================================================== */

const char *fs_result_str(FRESULT fr)
{
    switch (fr) {
    case FR_OK:              return "OK";
    case FR_DISK_ERR:        return "DISK_ERR";
    case FR_INT_ERR:         return "INT_ERR";
    case FR_NOT_READY:       return "NOT_READY";
    case FR_NO_FILE:         return "NO_FILE";
    case FR_NO_PATH:         return "NO_PATH";
    case FR_INVALID_NAME:    return "INVALID_NAME";
    case FR_DENIED:          return "DENIED";
    case FR_EXIST:           return "EXIST";
    case FR_INVALID_OBJECT:  return "INVALID_OBJECT";
    case FR_WRITE_PROTECTED: return "WRITE_PROTECTED";
    case FR_INVALID_DRIVE:   return "INVALID_DRIVE";
    case FR_NOT_ENABLED:     return "NOT_ENABLED";
    case FR_NO_FILESYSTEM:   return "NO_FILESYSTEM";
    case FR_TIMEOUT:         return "TIMEOUT";
    case FR_LOCKED:          return "LOCKED";
    case FR_NOT_ENOUGH_CORE: return "NOT_ENOUGH_CORE";
    case FR_TOO_MANY_OPEN_FILES: return "TOO_MANY_OPEN_FILES";
    case FR_INVALID_PARAMETER:   return "INVALID_PARAMETER";
    default:                 return "?";
    }
}

bool fs_is_mounted(void) { return mounted; }

FRESULT fs_mount(void)
{
    if (mounted) return FR_OK;

    /* "" = default drive, 1 = mount now rather than on first access, so
     * a missing card shows up here instead of inside the first f_open. */
    FRESULT fr = f_mount(&fs, "", 1);
    if (fr == FR_OK) {
        mounted = true;
        LOG_INFO("volume mounted");
    }
    return fr;
}

void fs_unmount(void)
{
    if (!mounted) return;
    f_unmount("");
    mounted = false;
}

static bool ready(void)
{
    FRESULT fr = fs_mount();
    if (fr != FR_OK) {
        log_raw("mount failed: %s (%d)\r\n", fs_result_str(fr), fr);
        return false;
    }
    return true;
}

/* Report an error, and if it looks like the media went away, forget the
 * mount so the next command re-runs f_mount (and with it disk_initialize)
 * instead of failing forever against a card that is no longer there.
 *
 * Note the current directory resets to the root when that happens: cwd
 * lives in the FATFS object, which is re-initialised by the re-mount. */
static void fail(const char *what, const char *path, FRESULT fr)
{
    log_raw("%s '%s': %s (%d)\r\n", what, path, fs_result_str(fr), fr);

    if (fr == FR_DISK_ERR || fr == FR_NOT_READY || fr == FR_INVALID_OBJECT) {
        mounted = false;          /* not fs_unmount(): the card is gone,
                                   * there is nothing to flush to it */
        LOG_WARN("volume dropped, will re-mount on next command");
    }
}

/* ======================================================================
 * ls [path]
 * ====================================================================== */

void fs_cmd_ls(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : ".";
    unsigned    n_files = 0, n_dirs = 0;
    uint32_t    total = 0;
    FRESULT     fr;

    if (!ready()) return;

    fr = f_opendir(&dir, path);
    if (fr != FR_OK) { fail("opendir", path, fr); return; }

    for (;;) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK) { fail("readdir", path, fr); break; }
        if (fno.fname[0] == '\0') break;          /* end of directory */

        if (fno.fattrib & AM_DIR) {
            log_raw("  <DIR>       %s\r\n", fno.fname);
            n_dirs++;
        } else {
            log_raw("  %10lu  %s\r\n", (unsigned long)fno.fsize, fno.fname);
            n_files++;
            total += fno.fsize;
        }
    }
    f_closedir(&dir);

    log_raw("  %u file(s), %lu bytes, %u dir(s)\r\n",
            n_files, (unsigned long)total, n_dirs);
}

/* ======================================================================
 * cd [path]      no argument goes to the root
 * ====================================================================== */

void fs_cmd_cd(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/";
    FRESULT     fr;

    if (!ready()) return;

    fr = f_chdir(path);
    if (fr != FR_OK) { fail("cd", path, fr); return; }

    /* Reuse iobuf: nothing else is in flight while a command runs. */
    fr = f_getcwd(iobuf, sizeof iobuf);
    if (fr != FR_OK) { fail("getcwd", path, fr); return; }

    log_raw("%s\r\n", iobuf);
}

/* ======================================================================
 * cat <file>
 *
 * Bare LF becomes CRLF so the output is readable in a serial terminal,
 * and control bytes are shown as '.' so an accidental `cat SCENE0.BIN`
 * does not spray escape sequences at your console.
 * ====================================================================== */

void fs_cmd_cat(int argc, char **argv)
{
    FRESULT fr;
    UINT    br;

    if (argc != 2) { log_raw("usage: cat <file>\r\n"); return; }
    if (!ready()) return;

    fr = f_open(&file, argv[1], FA_READ);
    if (fr != FR_OK) { fail("open", argv[1], fr); return; }

    for (;;) {
        fr = f_read(&file, iobuf, sizeof iobuf, &br);
        if (fr != FR_OK) { fail("read", argv[1], fr); break; }
        if (br == 0) break;                        /* EOF */

        /* Write in runs so we are not doing one io_write per byte. */
        UINT start = 0;
        for (UINT i = 0; i < br; i++) {
            char c = iobuf[i];
            bool printable = (c >= 0x20 && c < 0x7F) || c == '\t' || c == '\r';
            if (printable) continue;

            if (i > start) io_write(&iobuf[start], (uint16_t)(i - start));
            io_write(c == '\n' ? "\r\n" : ".", c == '\n' ? 2 : 1);
            start = i + 1;
        }
        if (br > start) io_write(&iobuf[start], (uint16_t)(br - start));
    }

    f_close(&file);
    io_write("\r\n", 2);
}

/* ======================================================================
 * echo [-a] <file> <text...>
 *
 * Without -a the file is truncated. A newline is appended. The CLI
 * tokenizer collapses runs of whitespace, so the text is rejoined with
 * single spaces and there is no quoting.
 * ====================================================================== */

void fs_cmd_echo(int argc, char **argv)
{
    bool     append = false;
    int      i      = 1;
    size_t   n      = 0;
    FRESULT  fr;
    UINT     bw;

    if (argc > 1 && strcmp(argv[1], "-a") == 0) { append = true; i = 2; }
    if (argc < i + 2) {
        log_raw("usage: echo [-a] <file> <text...>\r\n");
        return;
    }

    const char *path = argv[i++];

    for (int j = i; j < argc; j++) {
        size_t wl = strlen(argv[j]);
        if (n + wl + 2 > sizeof iobuf) { log_raw("text too long\r\n"); return; }
        if (j > i) iobuf[n++] = ' ';
        memcpy(&iobuf[n], argv[j], wl);
        n += wl;
    }
    iobuf[n++] = '\n';

    if (!ready()) return;

    fr = f_open(&file, path, append ? (FA_WRITE | FA_OPEN_APPEND)
                                    : (FA_WRITE | FA_CREATE_ALWAYS));
    if (fr != FR_OK) { fail("open", path, fr); return; }

    fr = f_write(&file, iobuf, (UINT)n, &bw);
    if (fr != FR_OK) {
        fail("write", path, fr);
    } else if (bw != n) {
        /* FR_OK with a short write means the volume is full. */
        log_raw("short write: %u of %u bytes (disk full?)\r\n",
                bw, (unsigned)n);
    }

    /* f_close flushes the directory entry. Skip it and the data is on
     * the card but the file still reads as 0 bytes. */
    fr = f_close(&file);
    if (fr != FR_OK) fail("close", path, fr);
    else             log_raw("wrote %u bytes to %s\r\n", bw, path);
}