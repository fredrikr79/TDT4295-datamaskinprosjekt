/* ======================================================================
 * fs_utils.c -- ls cd cat head echo mkdir rm exist mv cp.
 *
 * Talks only to fs.h, io.h and log.h.
 *
 * Handles are static on purpose: the FatFs FIL holds a 512-byte sector
 * buffer (FF_FS_TINY == 0), more than the firmware stack wants to give
 * up. Only one command runs at a time,
 * so sharing them is fine -- but do NOT call these re-entrantly.
 * ====================================================================== */
#define LOG_TAG "fs"

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "fs_utils.h"
#include "io.h"
#include "log.h"

#define IO_BUF_SIZE   128

static fs_file_t   file;
static fs_dir_t    dir;
static fs_dirent_t ent;
static fs_lines_t  lines;
static char        iobuf[IO_BUF_SIZE];
static char        target[FS_PATH_MAX];

/* Media-loss handling lives in the backend: on IO / NOT_READY it marks
 * itself unmounted and the next command re-mounts. */
static void fail(const char *what, const char *path, fs_err_t e)
{
    log_raw("%s '%s': %s\r\n", what, path, fs_strerror(e));
    if (!fs_is_mounted())
        log_raw("volume dropped, will re-mount on next command\r\n");
}

/* Write a chunk to the console: bare LF becomes CRLF, control bytes
 * become '.', so an accidental `cat SCENE0.BIN` does not spray escape
 * sequences at your terminal. */
static void put_printable(const char *s, uint32_t n)
{
    uint32_t start = 0;
    for (uint32_t i = 0; i < n; i++) {
        char c = s[i];
        bool printable = (c >= 0x20 && c < 0x7F) || c == '\t' || c == '\r';
        if (printable) continue;

        if (i > start) io_write(&s[start], (uint16_t)(i - start));
        io_write(c == '\n' ? "\r\n" : ".", c == '\n' ? 2 : 1);
        start = i + 1;
    }
    if (n > start) io_write(&s[start], (uint16_t)(n - start));
}

/* ======================================================================
 * ls [path]
 * ====================================================================== */

void fs_cmd_ls(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : ".";
    unsigned    n_files = 0, n_dirs = 0;
    uint32_t    total = 0;
    fs_err_t    e;

    e = fs_opendir(&dir, path);
    if (e != FS_OK) { fail("opendir", path, e); return; }

    for (;;) {
        e = fs_readdir(&dir, &ent);
        if (e != FS_OK) { fail("readdir", path, e); break; }
        if (ent.name[0] == '\0') break;             /* end of directory */

        if (ent.is_dir) {
            log_raw("  <DIR>       %s\r\n", ent.name);
            n_dirs++;
        } else {
            log_raw("  %10lu  %s\r\n", (unsigned long)ent.size, ent.name);
            n_files++;
            total += ent.size;
        }
    }
    fs_closedir(&dir);

    log_raw("  %u file(s), %lu bytes, %u dir(s)\r\n",
            n_files, (unsigned long)total, n_dirs);
}

/* ======================================================================
 * cd [path]      no argument goes to the root
 * ====================================================================== */

void fs_cmd_cd(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "/";
    fs_err_t    e;

    e = fs_chdir(path);
    if (e != FS_OK) { fail("cd", path, e); return; }

    e = fs_getcwd(iobuf, sizeof iobuf);
    if (e != FS_OK) { fail("getcwd", path, e); return; }

    log_raw("%s\r\n", iobuf);
}

/* ======================================================================
 * cat <file>          raw chunks through fs_read
 * ====================================================================== */

void fs_cmd_cat(int argc, char **argv)
{
    uint32_t  br;
    fs_err_t  e;

    if (argc != 2) { log_raw("usage: cat <file>\r\n"); return; }

    e = fs_open(&file, argv[1], FS_READ);
    if (e != FS_OK) { fail("open", argv[1], e); return; }

    for (;;) {
        e = fs_read(&file, iobuf, sizeof iobuf, &br);
        if (e != FS_OK) { fail("read", argv[1], e); break; }
        if (br == 0) break;                         /* EOF */
        put_printable(iobuf, br);
    }

    fs_close(&file);
    io_write("\r\n", 2);
}

/* ======================================================================
 * head <file> [n]     first n lines (default 10), numbered
 *
 * Mostly here to exercise fs_line_read on both backends.
 * ====================================================================== */

void fs_cmd_head(int argc, char **argv)
{
    unsigned long max = 10;
    size_t        n;
    fs_err_t      e;

    if (argc < 2 || argc > 3) { log_raw("usage: head <file> [n]\r\n"); return; }
    if (argc == 3) max = strtoul(argv[2], NULL, 0);

    e = fs_open(&file, argv[1], FS_READ);
    if (e != FS_OK) { fail("open", argv[1], e); return; }

    fs_line_init(&lines, &file);
    while (lines.lineno < max) {
        e = fs_line_read(&lines, iobuf, sizeof iobuf, &n);
        if (e == FS_EOF) break;
        if (e != FS_OK && e != FS_ERR_TOO_LONG) { fail("read", argv[1], e); break; }

        log_raw("%4lu  ", (unsigned long)lines.lineno);
        put_printable(iobuf, (uint32_t)n);
        log_raw("%s\r\n", e == FS_ERR_TOO_LONG ? " [...]" : "");
    }

    fs_close(&file);
}

/* ======================================================================
 * echo [-a] <file> <text...>
 *
 * Without -a the file is truncated. A newline is appended. Missing
 * folders in <file> are created, like a save would do. The CLI
 * tokenizer collapses runs of whitespace, so the text is rejoined with
 * single spaces and there is no quoting.
 * ====================================================================== */

void fs_cmd_echo(int argc, char **argv)
{
    bool      append = false;
    int       i      = 1;
    size_t    n      = 0;
    uint32_t  bw;
    fs_err_t  e;

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

    e = fs_open(&file, path,
                FS_WRITE | FS_MKPATH | (append ? FS_APPEND : FS_TRUNC));
    if (e != FS_OK) { fail("open", path, e); return; }

    e = fs_write(&file, iobuf, (uint32_t)n, &bw);
    if (e != FS_OK)
        fail("write", path, e);        /* FS_ERR_FULL on a full volume */

    /* Close even after a failed write: it is what commits the directory
     * entry for whatever did get written. */
    fs_err_t ec = fs_close(&file);
    if (ec != FS_OK)      fail("close", path, ec);
    else if (e == FS_OK)  log_raw("wrote %lu bytes to %s\r\n",
                                   (unsigned long)bw, path);
}

/* Shared by mkdir and rm: optional recursive flag, then one path. */
static bool parse_r(int argc, char **argv, bool *rec, const char **path)
{
    *rec = argc == 3 && (strcmp(argv[1], "-r") == 0 ||
                         strcmp(argv[1], "-p") == 0);
    if (argc == 2 && argv[1][0] != '-') { *path = argv[1]; return true; }
    if (*rec)                           { *path = argv[2]; return true; }
    return false;
}

/* ======================================================================
 * mkdir [-r] <dir>     -r (or -p): create missing parents too, and do
 *                      not complain if it already exists
 * ====================================================================== */

void fs_cmd_mkdir(int argc, char **argv)
{
    const char *path;
    bool        rec;

    if (!parse_r(argc, argv, &rec, &path)) {
        log_raw("usage: mkdir [-r] <dir>\r\n");
        return;
    }

    fs_err_t e = rec ? fs_mkdir_p(path) : fs_mkdir(path);
    if (e != FS_OK) fail("mkdir", path, e);
}

/* ======================================================================
 * rm [-r] <path>       file or empty dir; -r: dir with everything in it
 * ====================================================================== */

void fs_cmd_rm(int argc, char **argv)
{
    const char *path;
    bool        rec;

    if (!parse_r(argc, argv, &rec, &path)) {
        log_raw("usage: rm [-r] <path>\r\n");
        return;
    }

    fs_err_t e = rec ? fs_remove_r(path) : fs_remove(path);
    if (e == FS_ERR_DENIED && !rec)
        log_raw("rm '%s': DENIED (not empty? use rm -r)\r\n", path);
    else if (e != FS_OK)
        fail("rm", path, e);
}

/* ======================================================================
 * exist <path>         prints true / false, or the error if it cannot tell
 * ====================================================================== */

void fs_cmd_exist(int argc, char **argv)
{
    bool yes;

    if (argc != 2) { log_raw("usage: exist <path>\r\n"); return; }

    fs_err_t e = fs_exists(argv[1], &yes);
    if (e != FS_OK) { fail("exist", argv[1], e); return; }
    log_raw("%s\r\n", yes ? "true" : "false");
}

/* Like the shell: if dst is an existing directory, the result goes
 * inside it under src's own name ("mv a.txt save" -> "save/a.txt").
 * Returns the path to use: dst itself, or target. NULL if too long. */
static const char *into_dir(const char *src, const char *dst)
{
    bool is_dir = fs_stat(dst, &ent) == FS_OK && ent.is_dir;
    if (!is_dir) return dst;

    size_t sl = strlen(src);
    while (sl > 1 && src[sl - 1] == '/') sl--;           /* "a/" -> "a" */
    size_t b = sl;
    while (b > 0 && src[b - 1] != '/') b--;              /* basename   */

    size_t dl = strlen(dst);
    while (dl > 1 && dst[dl - 1] == '/') dl--;
    bool slash = !(dl == 1 && dst[0] == '/');

    if (dl + (slash ? 1 : 0) + (sl - b) >= sizeof target) return NULL;
    memcpy(target, dst, dl);
    if (slash) target[dl++] = '/';
    memcpy(&target[dl], &src[b], sl - b);
    target[dl + sl - b] = '\0';
    return target;
}

/* ======================================================================
 * mv <src> <dst>       rename or move; folders move with their contents
 * ====================================================================== */

void fs_cmd_mv(int argc, char **argv)
{
    if (argc != 3) { log_raw("usage: mv <src> <dst>\r\n"); return; }

    const char *dst = into_dir(argv[1], argv[2]);
    if (dst == NULL) { log_raw("mv: path too long\r\n"); return; }

    fs_err_t e = fs_move(argv[1], dst);
    if (e == FS_ERR_EXIST)
        log_raw("mv '%s': EXIST (rm it first)\r\n", dst);
    else if (e == FS_ERR_INVALID)
        log_raw("mv: cannot move '%s' into itself\r\n", argv[1]);
    else if (e != FS_OK)
        fail("mv", argv[1], e);
}

/* ======================================================================
 * cp [-r] <src> <dst>  copy a file; -r: a folder with everything in it
 * ====================================================================== */

void fs_cmd_cp(int argc, char **argv)
{
    bool rec = argc == 4 && strcmp(argv[1], "-r") == 0;
    if (!(argc == 3 && argv[1][0] != '-') && !rec) {
        log_raw("usage: cp [-r] <src> <dst>\r\n");
        return;
    }
    const char *src = argv[rec ? 2 : 1];

    fs_err_t e = fs_stat(src, &ent);
    if (e != FS_OK) { fail("cp", src, e); return; }
    if (ent.is_dir && !rec) {
        log_raw("cp '%s': is a directory (use cp -r)\r\n", src);
        return;
    }

    const char *dst = into_dir(src, argv[rec ? 3 : 2]);
    if (dst == NULL) { log_raw("cp: path too long\r\n"); return; }

    e = rec ? fs_copy_r(src, dst) : fs_copy(src, dst);
    if (e == FS_ERR_EXIST)
        log_raw("cp '%s': EXIST (rm it first)\r\n", dst);
    else if (e == FS_ERR_INVALID)
        log_raw("cp: cannot copy '%s' into itself\r\n", src);
    else if (e != FS_OK)
        fail("cp", src, e);
}