#ifndef FS_H
#define FS_H

/* ======================================================================
 * fs.h -- file API for everything in app/.
 *
 * Nothing in app/ except the backend itself includes ff.h or fx_api.h.
 * Exactly one backend is linked, chosen in the root CMakeLists.txt
 * (FS_BACKEND; the host build always uses FatFs):
 *
 *   app/fs_fatfs.c         FatFs on whichever diskio_*.c is linked:
 *                          port/host/diskio_host.c   -> card.img
 *                          port/stm32/diskio_sdmmc.c -> SDMMC1
 *   port/stm32/fs_filex.c  FileX (stub), selected with FS_USE_FILEX
 *
 * fs_fatfs.c sits in app/ rather than port/stm32/ because it is portable
 * and the host build needs it too, same as third_party/ff16/ff.c.
 *
 * Each backend has a small header (fs_fatfs.h / fs_filex.h) that fills
 * in struct fs_file / struct fs_dir, so callers can keep handles static
 * or on the stack without knowing which filesystem is underneath.
 *
 * Contract every backend must honour (fs_fatfs.c is the reference):
 *   - Paths use '/', relative to the cwd unless they start with '/'.
 *     That includes BOTH paths of fs_rename.
 *   - fs_getcwd returns an absolute path, "/" for the root, no drive
 *     prefix ("/save", not "0:/save"). fs_move/fs_copy_r rely on it.
 *   - One volume, mounted lazily by fs_mount(); the other calls mount
 *     on demand, so callers never have to.
 *   - fs_read: FS_OK with *got == 0 means end of file. A short read
 *     that is not at EOF is allowed; use fs_read_full for exactly N.
 *   - fs_write: *put < len only ever comes with an error (FS_ERR_FULL
 *     for a full volume), never with FS_OK.
 *   - FS_ERR_IO / FS_ERR_NOT_READY mean the media may be gone. The
 *     backend then marks itself unmounted, so the next access redoes
 *     card init instead of failing forever. Open handles are dead after
 *     that; close them (close on a dead handle is harmless) and reopen.
 *   - Data written with fs_write is on the media once fs_close returns
 *     FS_OK -- someone may pull the card right after.
 *   - fs_open with FS_MKPATH: call fs_mkdir_parents(path) (fs_core.c)
 *     before opening. Everything else in fs_core.c is built on the
 *     backend calls and needs nothing from the backend.
 *   - Not re-entrant, not thread-safe. One caller at a time.
 * ====================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(FS_USE_FILEX)
#include "fs_filex.h"       /* port/stm32 */
#else
#include "fs_fatfs.h"       /* include/   */
#endif

typedef struct fs_file fs_file_t;
typedef struct fs_dir  fs_dir_t;

typedef enum {
    FS_OK = 0,
    FS_EOF,                 /* only from fs_line_read: no more lines    */
    FS_ERR_NOT_READY,       /* no card / not mounted / media went away  */
    FS_ERR_IO,              /* low-level read/write failed              */
    FS_ERR_NO_FS,           /* media present but no valid FAT on it     */
    FS_ERR_NO_FILE,
    FS_ERR_NO_PATH,
    FS_ERR_EXIST,
    FS_ERR_DENIED,          /* read-only, dir not empty, root full, ... */
    FS_ERR_FULL,            /* volume full                              */
    FS_ERR_INVALID,         /* bad name, bad handle, bad argument       */
    FS_ERR_TOO_LONG,        /* line or path did not fit the buffer      */
    FS_ERR_OTHER,
} fs_err_t;

/* Open flags:
 *   FS_READ                  existing file, read only
 *   FS_WRITE | FS_TRUNC      create, or truncate existing to 0
 *   FS_WRITE | FS_APPEND     create if missing, position at end
 *   FS_WRITE                 existing file, overwrite in place from 0
 * FS_READ may be added to any WRITE form.
 *
 * FS_MKPATH (with FS_WRITE) also creates any missing parent directories,
 * so FS_WRITE | FS_TRUNC | FS_MKPATH on "save/save1/index.dat" works on
 * a blank card. Without it, a missing folder gives FS_ERR_NO_PATH. */
enum {
    FS_READ   = 1u << 0,
    FS_WRITE  = 1u << 1,
    FS_TRUNC  = 1u << 2,
    FS_APPEND = 1u << 3,
    FS_MKPATH = 1u << 4,
};

#define FS_NAME_MAX   256           /* incl. NUL; covers FAT LFNs */
#define FS_PATH_MAX   256           /* longest path fs_core.c builds  */

typedef struct {
    char     name[FS_NAME_MAX];     /* "" marks end of directory */
    uint32_t size;
    bool     is_dir;
} fs_dirent_t;

/* ------------------------------------------------ backend-implemented */

fs_err_t    fs_mount(void);                 /* cheap if already mounted */
void        fs_unmount(void);               /* flushes, then releases   */
bool        fs_is_mounted(void);

fs_err_t    fs_open (fs_file_t *f, const char *path, unsigned flags);
fs_err_t    fs_close(fs_file_t *f);
fs_err_t    fs_read (fs_file_t *f, void *buf, uint32_t len, uint32_t *got);
fs_err_t    fs_write(fs_file_t *f, const void *buf, uint32_t len,
                     uint32_t *put);
fs_err_t    fs_seek (fs_file_t *f, uint32_t offset);    /* absolute */
uint32_t    fs_tell (const fs_file_t *f);
uint32_t    fs_size (const fs_file_t *f);

/* Only one directory may be open at a time (FileX lists the media's
 * default directory, so a FileX backend cannot cheaply do better).
 * End of directory is FS_OK with out->name[0] == '\0'. "." and ".."
 * are never returned. */
fs_err_t    fs_opendir (fs_dir_t *d, const char *path);
fs_err_t    fs_readdir (fs_dir_t *d, fs_dirent_t *out);
fs_err_t    fs_closedir(fs_dir_t *d);

fs_err_t    fs_chdir (const char *path);
fs_err_t    fs_getcwd(char *buf, size_t len);

/* Info about a file or directory without opening it. out->name is not
 * filled in. Missing: FS_ERR_NO_FILE (last part missing) or
 * FS_ERR_NO_PATH (a folder on the way is missing). Must also work on
 * "/", "." and "..". */
fs_err_t    fs_stat  (const char *path, fs_dirent_t *out);

/* One level only: the parent must exist. FS_ERR_EXIST if the name is
 * already taken, by a directory or a file. */
fs_err_t    fs_mkdir (const char *path);

/* Rename or move a file or directory (with everything in it) within the
 * volume. FS_ERR_EXIST if new_path is taken -- nothing is overwritten.
 * Raw backend call: use fs_move, which also refuses to move a directory
 * into itself (FatFs does not check that and would corrupt the tree). */
fs_err_t    fs_rename(const char *old_path, const char *new_path);

/* A file, or an EMPTY directory (FS_ERR_DENIED if not empty, or if it is
 * the current directory). For whole trees use fs_remove_r. */
fs_err_t    fs_remove(const char *path);

/* ---------------------------------------- portable, in app/fs_core.c */

const char *fs_strerror(fs_err_t e);

/* Does path exist (file or directory)?
 *   FS_OK with *exists set    answered: true or false
 *   anything else             could NOT tell, *exists is false
 * The tri-state is deliberate. Do not treat an error as "slot is empty":
 * a card glitch would then look like a free save slot, and the new save
 * would be written over the old one. */
fs_err_t    fs_exists(const char *path, bool *exists);

/* mkdir -p: create path and any missing parents. Fine if it already
 * exists as a directory; FS_ERR_EXIST if something on the way is a file.
 * fs_mkdir_parents does the same for everything EXCEPT the last part, for
 * when path names a file you are about to create (what FS_MKPATH uses). */
fs_err_t    fs_mkdir_p(const char *path);
fs_err_t    fs_mkdir_parents(const char *path);

/* rm -r: a file, or a directory and everything under it. Refuses "/",
 * "" and paths ending in "." or ".." (FS_ERR_INVALID), so a typo cannot
 * wipe the card. Not atomic: on error, part of the tree may be gone.
 * Only one directory is open at a time, so it works on FileX too, and
 * it uses no recursion (no stack growth with folder depth). */
fs_err_t    fs_remove_r(const char *path);

/* mv: fs_rename, but refuses to move a directory into itself or its own
 * subtree (FS_ERR_INVALID). Paths are resolved against the cwd for that
 * check, so "save" -> "./save/x" and "/save/x" are all caught. Moving a
 * directory moves everything under it; nothing is copied. */
fs_err_t    fs_move(const char *src, const char *dst);

/* cp of one file. dst must not exist (FS_ERR_EXIST) -- remove it first
 * if you mean to replace it. On failure the partial dst is deleted. */
fs_err_t    fs_copy(const char *src, const char *dst);

/* cp -r: a file, or a directory and everything under it. dst must not
 * exist; its parent must. Refuses to copy a directory into itself.
 * Not atomic: on error, a partial copy is left behind (rm -r it). Up to
 * FS_COPY_DEPTH levels of subfolders, one directory open at a time. */
#ifndef FS_COPY_DEPTH
#define FS_COPY_DEPTH   16
#endif
fs_err_t    fs_copy_r(const char *src, const char *dst);

/* Read exactly len bytes unless EOF comes first. FS_OK with *got < len
 * means EOF was hit. Use this for fixed-size records and headers. */
fs_err_t    fs_read_full(fs_file_t *f, void *buf, uint32_t len,
                         uint32_t *got);

/* Line reader. Built on fs_read rather than f_gets (FileX has no
 * equivalent), so both backends behave identically.
 *
 * It reads ahead FS_LINE_BUF bytes at a time, so while a line reader is
 * in use, do not call fs_read/fs_seek on the same file -- the real file
 * position is ahead of what you have consumed. fs_line_tell() gives the
 * logical position if you need it. */
#ifndef FS_LINE_BUF
#define FS_LINE_BUF   128
#endif

typedef struct {
    fs_file_t  *f;
    uint16_t    pos, len;
    uint32_t    lineno;             /* 1-based number of the last line */
    char        buf[FS_LINE_BUF];
} fs_lines_t;

void        fs_line_init(fs_lines_t *lr, fs_file_t *f);

/* Next line into out (NUL terminated, "\n" or "\r\n" stripped).
 *   FS_OK             a line, possibly empty. A last line without '\n'
 *                     is still returned as FS_OK.
 *   FS_EOF            no more data
 *   FS_ERR_TOO_LONG   line did not fit: out holds the first cap-1 bytes,
 *                     the rest of that line has been skipped, and the
 *                     next call continues with the following line
 *   anything else     error from fs_read */
fs_err_t    fs_line_read(fs_lines_t *lr, char *out, size_t cap,
                         size_t *out_len);
uint32_t    fs_line_tell(const fs_lines_t *lr);

#endif /* FS_H */