/* ======================================================================
 * port/stm32/fs_filex.c -- fs.h backend on FileX.          ** STUB **
 *
 * Builds and returns FS_ERR_NOT_READY everywhere, so selecting it in
 * the root CMakeLists.txt (set(FS_BACKEND filex)) links, and the
 * console reports NOT_READY until this is filled in.
 *
 * What each function must do is in include/fs.h (contract at the top).
 * app/fs_fatfs.c does the same job on FatFs and is the reference: the
 * host build runs it, so the host is the definition of correct.
 *
 * FileX replaces third_party/ff16 and diskio_sdmmc.c; it brings its own
 * FAT code and needs its own SD driver.
 * ====================================================================== */

#include "fs.h"

static bool mounted;

fs_err_t fs_mount(void)
{
    if (mounted) return FS_OK;
    return FS_ERR_NOT_READY;
}

void fs_unmount(void)
{
    mounted = false;
}

bool fs_is_mounted(void)
{
    return mounted;
}

fs_err_t fs_open(fs_file_t *f, const char *path, unsigned flags)
{
    (void)path; (void)flags;
    f->open = false;
    return FS_ERR_NOT_READY;
}

fs_err_t fs_close(fs_file_t *f)
{
    f->open = false;
    return FS_OK;
}

fs_err_t fs_read(fs_file_t *f, void *buf, uint32_t len, uint32_t *got)
{
    (void)f; (void)buf; (void)len;
    *got = 0;
    return FS_ERR_NOT_READY;
}

fs_err_t fs_write(fs_file_t *f, const void *buf, uint32_t len,
                  uint32_t *put)
{
    (void)f; (void)buf; (void)len;
    *put = 0;
    return FS_ERR_NOT_READY;
}

fs_err_t fs_seek(fs_file_t *f, uint32_t offset)
{
    (void)f; (void)offset;
    return FS_ERR_NOT_READY;
}

uint32_t fs_tell(const fs_file_t *f)
{
    (void)f;
    return 0;
}

uint32_t fs_size(const fs_file_t *f)
{
    (void)f;
    return 0;
}

fs_err_t fs_opendir(fs_dir_t *d, const char *path)
{
    (void)path;
    d->open = false;
    return FS_ERR_NOT_READY;
}

fs_err_t fs_readdir(fs_dir_t *d, fs_dirent_t *out)
{
    (void)d;
    out->name[0] = '\0';
    return FS_ERR_NOT_READY;
}

fs_err_t fs_closedir(fs_dir_t *d)
{
    d->open = false;
    return FS_OK;
}

fs_err_t fs_chdir(const char *path)
{
    (void)path;
    return FS_ERR_NOT_READY;
}

fs_err_t fs_getcwd(char *buf, size_t len)
{
    if (len) buf[0] = '\0';
    return FS_ERR_NOT_READY;
}

fs_err_t fs_stat(const char *path, fs_dirent_t *out)
{
    (void)path;
    out->name[0] = '\0';
    out->size    = 0;
    out->is_dir  = false;
    return FS_ERR_NOT_READY;
}

fs_err_t fs_mkdir(const char *path)
{
    (void)path;
    return FS_ERR_NOT_READY;
}

fs_err_t fs_remove(const char *path)
{
    (void)path;
    return FS_ERR_NOT_READY;
}

fs_err_t fs_rename(const char *old_path, const char *new_path)
{
    (void)old_path; (void)new_path;
    return FS_ERR_NOT_READY;
}