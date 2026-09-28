/* ======================================================================
 * fs_fatfs.c - fs.h backend on FatFs (third_party/ff16).
 *
 * Portable: needs only ff.h. Whichever diskio_*.c is linked decides
 * whether this hits card.img (port/host/diskio_host.c) or the SD card
 * (port/stm32/diskio_sdmmc.c). This is also the reference for what a
 * backend has to do -- port/stm32/fs_filex.c should behave the same.
 *
 * Requires in ffconf.h:
 *   FF_FS_RPATH     2   (f_chdir + f_getcwd)
 *   FF_FS_MINIMIZE  0   (f_opendir / f_readdir / f_lseek)
 *   FF_FS_READONLY  0
 *   FF_USE_LFN      >0  if you want long names in fs_readdir
 * ====================================================================== */
#define LOG_TAG "fs"

#include <string.h>

#include "fs.h"
#include "log.h"

static FATFS fs;                /* must outlive the mount */
static bool  mounted;

static fs_err_t map(FRESULT fr)
{
    switch (fr) {
    case FR_OK:                  return FS_OK;
    case FR_DISK_ERR:            return FS_ERR_IO;
    case FR_NOT_READY:           return FS_ERR_NOT_READY;
    case FR_NO_FILESYSTEM:       return FS_ERR_NO_FS;
    case FR_NO_FILE:             return FS_ERR_NO_FILE;
    case FR_NO_PATH:             return FS_ERR_NO_PATH;
    case FR_EXIST:               return FS_ERR_EXIST;
    case FR_DENIED:
    case FR_WRITE_PROTECTED:     return FS_ERR_DENIED;
    case FR_INVALID_NAME:
    case FR_INVALID_OBJECT:
    case FR_INVALID_DRIVE:
    case FR_INVALID_PARAMETER:   return FS_ERR_INVALID;
    case FR_NOT_ENOUGH_CORE:     return FS_ERR_TOO_LONG;   /* getcwd buf */
    default:                     return FS_ERR_OTHER;
    }
}

/* Map, and if the media looks gone, forget the mount so the next
 * fs_mount re-runs f_mount (and with it disk_initialize).
 *
 * FR_INVALID_OBJECT is included because it is what a handle opened
 * before a re-mount returns: the FATFS id it recorded no longer matches.
 * Note the cwd resets to the root after a re-mount -- it lives in FATFS. */
static fs_err_t check(FRESULT fr)
{
    if (mounted &&
        (fr == FR_DISK_ERR || fr == FR_NOT_READY || fr == FR_INVALID_OBJECT)) {
        mounted = false;          /* no f_unmount: nothing to flush to */
        LOG_WARN("volume dropped (%d), will re-mount on next access", fr);
    }
    return map(fr);
}

/* ---------------------------------------------------------------- mount */

fs_err_t fs_mount(void)
{
    if (mounted) return FS_OK;

    /* 1 = mount now, so a missing card shows up here and not inside the
     * first f_open. */
    FRESULT fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) return map(fr);

    mounted = true;
    LOG_INFO("volume mounted");
    return FS_OK;
}

void fs_unmount(void)
{
    if (!mounted) return;
    f_unmount("");
    mounted = false;
}

bool fs_is_mounted(void) { return mounted; }

/* ---------------------------------------------------------------- files */

fs_err_t fs_open(fs_file_t *f, const char *path, unsigned flags)
{
    BYTE mode = 0;

    f->open = false;
    if (!(flags & (FS_READ | FS_WRITE)))           return FS_ERR_INVALID;
    if ((flags & FS_TRUNC) && (flags & FS_APPEND)) return FS_ERR_INVALID;

    if (flags & FS_READ)  mode |= FA_READ;
    if (flags & FS_WRITE) {
        mode |= FA_WRITE;
        if      (flags & FS_TRUNC)  mode |= FA_CREATE_ALWAYS;
        else if (flags & FS_APPEND) mode |= FA_OPEN_APPEND;
        /* WRITE alone: existing file, overwrite in place from offset 0 */
    }

    fs_err_t e = fs_mount();
    if (e != FS_OK) return e;

    if ((flags & FS_MKPATH) && (flags & FS_WRITE)) {
        e = fs_mkdir_parents(path);
        if (e != FS_OK) return e;
    }

    e = check(f_open(&f->fil, path, mode));
    if (e == FS_OK) f->open = true;
    return e;
}

fs_err_t fs_close(fs_file_t *f)
{
    if (!f->open) return FS_OK;
    f->open = false;
    /* f_close flushes the directory entry. Skip it and the data is on
     * the card but the file still reads as 0 bytes. */
    return check(f_close(&f->fil));
}

fs_err_t fs_read(fs_file_t *f, void *buf, uint32_t len, uint32_t *got)
{
    UINT br = 0;
    fs_err_t e = f->open ? check(f_read(&f->fil, buf, len, &br))
                         : FS_ERR_INVALID;
    *got = br;
    return e;
}

fs_err_t fs_write(fs_file_t *f, const void *buf, uint32_t len,
                  uint32_t *put)
{
    UINT bw = 0;
    fs_err_t e = f->open ? check(f_write(&f->fil, buf, len, &bw))
                         : FS_ERR_INVALID;
    *put = bw;
    /* FatFs reports a full volume as FR_OK with a short count. */
    if (e == FS_OK && bw < len) e = FS_ERR_FULL;
    return e;
}

fs_err_t fs_seek(fs_file_t *f, uint32_t offset)
{
    if (!f->open) return FS_ERR_INVALID;
    /* In read-only mode f_lseek clamps to the file size; in write mode
     * it extends the file. Both are fine for this contract. */
    return check(f_lseek(&f->fil, offset));
}

uint32_t fs_tell(const fs_file_t *f) { return (uint32_t)f_tell(&f->fil); }
uint32_t fs_size(const fs_file_t *f) { return (uint32_t)f_size(&f->fil); }

/* ---------------------------------------------------------- directories */

fs_err_t fs_opendir(fs_dir_t *d, const char *path)
{
    d->open = false;
    fs_err_t e = fs_mount();
    if (e != FS_OK) return e;

    e = check(f_opendir(&d->dir, path));
    if (e == FS_OK) d->open = true;
    return e;
}

fs_err_t fs_readdir(fs_dir_t *d, fs_dirent_t *out)
{
    out->name[0] = '\0';
    if (!d->open) return FS_ERR_INVALID;

    fs_err_t e = check(f_readdir(&d->dir, &d->fno));
    if (e != FS_OK || d->fno.fname[0] == '\0') return e;

    strncpy(out->name, d->fno.fname, sizeof out->name - 1);
    out->name[sizeof out->name - 1] = '\0';
    out->size   = (uint32_t)d->fno.fsize;
    out->is_dir = (d->fno.fattrib & AM_DIR) != 0;
    return FS_OK;
}

fs_err_t fs_closedir(fs_dir_t *d)
{
    if (!d->open) return FS_OK;
    d->open = false;
    return check(f_closedir(&d->dir));
}

fs_err_t fs_chdir(const char *path)
{
    fs_err_t e = fs_mount();
    if (e != FS_OK) return e;
    return check(f_chdir(path));
}

fs_err_t fs_getcwd(char *buf, size_t len)
{
    fs_err_t e = fs_mount();
    if (e != FS_OK) return e;
    return check(f_getcwd(buf, (UINT)len));
}

/* ------------------------------------------------- stat / mkdir / remove */

fs_err_t fs_stat(const char *path, fs_dirent_t *out)
{
    static FILINFO fno;             /* big with LFN on; keep off the stack */
    static DIR     probe;

    out->name[0] = '\0';
    out->size    = 0;
    out->is_dir  = false;

    fs_err_t e = fs_mount();
    if (e != FS_OK) return e;

    FRESULT fr = f_stat(path, &fno);
    if (fr == FR_OK) {
        out->size   = (uint32_t)fno.fsize;
        out->is_dir = (fno.fattrib & AM_DIR) != 0;
        return FS_OK;
    }

    /* f_stat refuses the root and dot names ("/", ".", "..") with
     * FR_INVALID_NAME, even though they exist. Those are directories,
     * so if opening it as one works, it is one. */
    if (fr == FR_INVALID_NAME && f_opendir(&probe, path) == FR_OK) {
        f_closedir(&probe);
        out->is_dir = true;
        return FS_OK;
    }
    return check(fr);
}

fs_err_t fs_mkdir(const char *path)
{
    fs_err_t e = fs_mount();
    if (e != FS_OK) return e;
    return check(f_mkdir(path));
}

fs_err_t fs_remove(const char *path)
{
    fs_err_t e = fs_mount();
    if (e != FS_OK) return e;
    /* f_unlink: FR_DENIED for a non-empty dir, the cwd, or read-only. */
    return check(f_unlink(path));
}

fs_err_t fs_rename(const char *old_path, const char *new_path)
{
    fs_err_t e = fs_mount();
    if (e != FS_OK) return e;
    /* Moves across directories too, and a directory moves with its
     * whole subtree. FR_EXIST if new_path is taken. */
    return check(f_rename(old_path, new_path));
}