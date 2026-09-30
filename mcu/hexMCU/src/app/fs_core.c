/* ======================================================================
 * fs_core.c - backend-independent part of fs.h: error strings, exact
 * reads, the line reader, exists, mkdir -p, rm -r, mv and cp.
 * ====================================================================== */
#include <string.h>

#include "fs.h"

const char *fs_strerror(fs_err_t e)
{
    switch (e) {
    case FS_OK:             return "OK";
    case FS_EOF:            return "EOF";
    case FS_ERR_NOT_READY:  return "NOT_READY";
    case FS_ERR_IO:         return "IO";
    case FS_ERR_NO_FS:      return "NO_FS";
    case FS_ERR_NO_FILE:    return "NO_FILE";
    case FS_ERR_NO_PATH:    return "NO_PATH";
    case FS_ERR_EXIST:      return "EXIST";
    case FS_ERR_DENIED:     return "DENIED";
    case FS_ERR_FULL:       return "FULL";
    case FS_ERR_INVALID:    return "INVALID";
    case FS_ERR_TOO_LONG:   return "TOO_LONG";
    case FS_ERR_OTHER:      return "OTHER";
    }
    return "?";
}

fs_err_t fs_read_full(fs_file_t *f, void *buf, uint32_t len, uint32_t *got)
{
    uint8_t  *p     = buf;
    uint32_t  total = 0;

    while (total < len) {
        uint32_t  n;
        fs_err_t  e = fs_read(f, p + total, len - total, &n);
        if (e != FS_OK) { *got = total; return e; }
        if (n == 0) break;                              /* EOF */
        total += n;
    }
    *got = total;
    return FS_OK;
}

/* ---------------------------------------------------------- line reader */

void fs_line_init(fs_lines_t *lr, fs_file_t *f)
{
    lr->f      = f;
    lr->pos    = 0;
    lr->len    = 0;
    lr->lineno = 0;
}

static fs_err_t refill(fs_lines_t *lr)
{
    uint32_t  n = 0;
    fs_err_t  e = fs_read(lr->f, lr->buf, sizeof lr->buf, &n);
    lr->pos = 0;
    lr->len = (uint16_t)((e == FS_OK) ? n : 0);
    return e;
}

static fs_err_t finish(char *out, size_t n, size_t *out_len, fs_err_t e)
{
    out[n] = '\0';
    if (out_len) *out_len = n;
    return e;
}

fs_err_t fs_line_read(fs_lines_t *lr, char *out, size_t cap,
                        size_t *out_len)
{
    size_t n        = 0;
    bool   any      = false;     /* consumed at least one byte */
    bool   overflow = false;

    if (cap == 0) return FS_ERR_INVALID;

    for (;;) {
        if (lr->pos == lr->len) {
            fs_err_t e = refill(lr);
            if (e != FS_OK) return finish(out, n, out_len, e);
            if (lr->len == 0) {                         /* EOF */
                if (!any) return finish(out, 0, out_len, FS_EOF);
                break;                                  /* last line, no \n */
            }
        }

        /* Scan the buffered chunk for '\n' in one go. */
        const char *start = &lr->buf[lr->pos];
        uint16_t    avail = (uint16_t)(lr->len - lr->pos);
        const char *nl    = memchr(start, '\n', avail);
        uint16_t    take  = nl ? (uint16_t)(nl - start) : avail;
        uint16_t    skip  = (uint16_t)(take + (nl ? 1u : 0u));

        /* Drop the '\r' of a CRLF here rather than after copying, so a
         * line that fits exactly is not reported as too long. */
        if (nl && take > 0 && start[take - 1] == '\r') take--;

        any = true;
        if (!overflow) {
            size_t room = cap - 1 - n;
            size_t copy = (take < room) ? take : room;
            memcpy(&out[n], start, copy);
            n += copy;
            if (copy < take) overflow = true;
        }
        lr->pos = (uint16_t)(lr->pos + skip);
        if (nl) break;
    }

    /* A '\r' that ended one chunk, with its '\n' starting the next. */
    if (!overflow && n > 0 && out[n - 1] == '\r') n--;

    lr->lineno++;
    return finish(out, n, out_len, overflow ? FS_ERR_TOO_LONG : FS_OK);
}

uint32_t fs_line_tell(const fs_lines_t *lr)
{
    return fs_tell(lr->f) - (uint32_t)(lr->len - lr->pos);
}

/* ======================================================================
 * exists / mkdir -p / rm -r
 *
 * Built only on fs_stat, fs_mkdir, fs_remove and the directory calls,
 * so every backend gets them for free.
 *
 * Scratch objects are static: fs_dir_t holds a FILINFO with an LFN
 * buffer and fs_dirent_t is 260+ bytes, too much for the stack. These
 * functions are not re-entrant (nor is anything else in fs.h).
 * ====================================================================== */

static char        pathbuf[FS_PATH_MAX];
static fs_dir_t    scratch_dir;
static fs_dirent_t scratch_ent;

fs_err_t fs_exists(const char *path, bool *exists)
{
    fs_err_t e = fs_stat(path, &scratch_ent);

    *exists = (e == FS_OK);
    if (e == FS_ERR_NO_FILE || e == FS_ERR_NO_PATH) return FS_OK;
    return e;
}

/* Copy path into pathbuf and drop trailing slashes ("a/b/" -> "a/b"),
 * keeping a lone "/". Returns the new length, or 0 if it did not fit. */
static size_t load_path(const char *path)
{
    size_t len = strlen(path);
    if (len >= sizeof pathbuf) return 0;
    memcpy(pathbuf, path, len + 1);
    while (len > 1 && pathbuf[len - 1] == '/') pathbuf[--len] = '\0';
    return len;
}

/* mkdir that is fine with "already there, and it is a directory". */
static fs_err_t mkdir_one(const char *p)
{
    fs_err_t e = fs_mkdir(p);
    if (e != FS_ERR_EXIST) return e;

    e = fs_stat(p, &scratch_ent);
    if (e != FS_OK) return e;
    return scratch_ent.is_dir ? FS_OK : FS_ERR_EXIST;
}

/* Create every prefix of pathbuf[0..len) that ends at a '/' or at len.
 * Starts at index 1 so the leading '/' of an absolute path is not taken
 * as an empty first component. */
static fs_err_t mkdir_prefixes(size_t len)
{
    for (size_t i = 1; i <= len; i++) {
        if (pathbuf[i] != '/' && pathbuf[i] != '\0') continue;
        if (pathbuf[i - 1] == '/') continue;            /* "a//b" */

        char c = pathbuf[i];
        pathbuf[i] = '\0';
        fs_err_t e = mkdir_one(pathbuf);
        pathbuf[i] = c;
        if (e != FS_OK) return e;
    }
    return FS_OK;
}

fs_err_t fs_mkdir_p(const char *path)
{
    size_t len = load_path(path);
    if (len == 0) return (path[0] == '\0') ? FS_ERR_INVALID : FS_ERR_TOO_LONG;
    return mkdir_prefixes(len);
}

fs_err_t fs_mkdir_parents(const char *path)
{
    size_t len = load_path(path);
    if (len == 0) return (path[0] == '\0') ? FS_ERR_INVALID : FS_ERR_TOO_LONG;

    char *slash = strrchr(pathbuf, '/');
    if (slash == NULL || slash == pathbuf) return FS_OK; /* parent is cwd / root */
    *slash = '\0';
    return mkdir_prefixes((size_t)(slash - pathbuf));
}

/* Last component is "." or "..", or the whole thing is "/". */
static bool is_protected(const char *p, size_t len)
{
    const char *last = strrchr(p, '/');
    last = last ? last + 1 : p;
    return (len == 1 && p[0] == '/')
        || strcmp(last, ".") == 0 || strcmp(last, "..") == 0;
}

/* Walk down to the first child of pathbuf, delete files on the way, and
 * delete each directory once it is empty, then step back up. Only one
 * directory is open at a time and each is closed before anything in it
 * is removed, so no directory is modified while being listed.
 *
 * Every pass re-opens the directory and takes its first entry. That is
 * O(entries^2) for a huge folder, which does not matter for save slots. */
fs_err_t fs_remove_r(const char *path)
{
    size_t len = load_path(path);
    if (len == 0) return (path[0] == '\0') ? FS_ERR_INVALID : FS_ERR_TOO_LONG;
    if (is_protected(pathbuf, len)) return FS_ERR_INVALID;

    fs_err_t e = fs_stat(pathbuf, &scratch_ent);
    if (e != FS_OK) return e;
    if (!scratch_ent.is_dir) return fs_remove(pathbuf);

    const size_t base = len;             /* never go above this */

    for (;;) {
        e = fs_opendir(&scratch_dir, pathbuf);
        if (e != FS_OK) return e;
        e = fs_readdir(&scratch_dir, &scratch_ent);
        fs_closedir(&scratch_dir);
        if (e != FS_OK) return e;

        if (scratch_ent.name[0] == '\0') {
            /* Empty directory: remove it and go back up one level. */
            e = fs_remove(pathbuf);
            if (e != FS_OK || len == base) return e;
            len = (size_t)(strrchr(pathbuf, '/') - pathbuf);
            pathbuf[len] = '\0';
            continue;
        }

        size_t nl = strlen(scratch_ent.name);
        if (len + 1 + nl >= sizeof pathbuf) return FS_ERR_TOO_LONG;
        pathbuf[len] = '/';
        memcpy(&pathbuf[len + 1], scratch_ent.name, nl + 1);

        if (scratch_ent.is_dir) {
            len += 1 + nl;               /* descend */
        } else {
            e = fs_remove(pathbuf);
            pathbuf[len] = '\0';
            if (e != FS_OK) return e;
        }
    }
}

/* ======================================================================
 * move / copy
 * ====================================================================== */

static char      srcbuf[FS_PATH_MAX];
static char      dstbuf[FS_PATH_MAX];
static fs_file_t cp_src, cp_dst;
static uint32_t  cp_buf[512 / 4];      /* word-aligned: no SD bounce copy */

/* Resolve path against the cwd into an absolute path without "." or ".."
 * parts or trailing slashes: "../save//a/" from /x -> "/save/a". Purely
 * textual after the cwd lookup; nothing is checked for existence. */
static fs_err_t abs_path(const char *path, char *out, size_t cap)
{
    size_t n = 0;

    if (path[0] != '/') {
        fs_err_t e = fs_getcwd(out, cap);
        if (e != FS_OK) return e;
        n = strlen(out);
        if (n == 1) n = 0;             /* root: segments add their own '/' */
    }
    out[n] = '\0';

    const char *p = path;
    while (*p) {
        while (*p == '/') p++;
        const char *seg = p;
        while (*p && *p != '/') p++;
        size_t sl = (size_t)(p - seg);

        if (sl == 0 || (sl == 1 && seg[0] == '.')) continue;
        if (sl == 2 && seg[0] == '.' && seg[1] == '.') {
            while (n > 0 && out[n - 1] != '/') n--;     /* drop last part */
            if (n > 0) n--;                             /* and its '/'    */
            out[n] = '\0';
            continue;
        }
        if (n + 1 + sl >= cap) return FS_ERR_TOO_LONG;
        out[n++] = '/';
        memcpy(&out[n], seg, sl);
        n += sl;
        out[n] = '\0';
    }
    if (n == 0) { out[0] = '/'; out[1] = '\0'; }
    return FS_OK;
}

/* Resolve both paths into srcbuf/dstbuf and refuse dst == src or dst
 * anywhere below src. */
static fs_err_t resolve_pair(const char *src, const char *dst)
{
    fs_err_t e = abs_path(src, srcbuf, sizeof srcbuf);
    if (e == FS_OK) e = abs_path(dst, dstbuf, sizeof dstbuf);
    if (e != FS_OK) return e;

    size_t sl = strlen(srcbuf);
    if (strcmp(srcbuf, "/") == 0) return FS_ERR_INVALID;
    if (strncmp(dstbuf, srcbuf, sl) == 0 &&
        (dstbuf[sl] == '\0' || dstbuf[sl] == '/'))
        return FS_ERR_INVALID;
    return FS_OK;
}

fs_err_t fs_move(const char *src, const char *dst)
{
    fs_err_t e = resolve_pair(src, dst);
    if (e != FS_OK) return e;
    return fs_rename(srcbuf, dstbuf);
}

/* Copy one file between two already-resolved paths. */
static fs_err_t copy_file(const char *src, const char *dst)
{
    bool     taken;
    uint32_t got, put;

    fs_err_t e = fs_exists(dst, &taken);
    if (e != FS_OK) return e;
    if (taken)      return FS_ERR_EXIST;

    e = fs_open(&cp_src, src, FS_READ);
    if (e != FS_OK) return e;
    e = fs_open(&cp_dst, dst, FS_WRITE | FS_TRUNC);
    if (e != FS_OK) { fs_close(&cp_src); return e; }

    for (;;) {
        e = fs_read(&cp_src, cp_buf, sizeof cp_buf, &got);
        if (e != FS_OK || got == 0) break;
        e = fs_write(&cp_dst, cp_buf, got, &put);
        if (e != FS_OK) break;
    }

    fs_close(&cp_src);
    fs_err_t ec = fs_close(&cp_dst);    /* commits the directory entry */
    if (e == FS_OK) e = ec;

    if (e != FS_OK) fs_remove(dst);     /* no half-copied files left */
    return e;
}

fs_err_t fs_copy(const char *src, const char *dst)
{
    fs_err_t e = resolve_pair(src, dst);
    if (e != FS_OK) return e;
    return copy_file(srcbuf, dstbuf);
}

/* Cut a path back to before its last '/'. */
static void path_up(char *p, size_t *len)
{
    while (*len > 0 && p[*len - 1] != '/') (*len)--;
    if (*len > 0) (*len)--;
    p[*len] = '\0';
}

/* Walk src depth-first with one directory open at a time. idx[d] is how
 * many entries of the directory at depth d have been handled; each step
 * re-opens that directory and skips that many. O(entries^2) per folder,
 * which is nothing for save slots, and it needs no recursion. */
fs_err_t fs_copy_r(const char *src, const char *dst)
{
    static uint16_t idx[FS_COPY_DEPTH];
    unsigned        depth = 0;

    fs_err_t e = resolve_pair(src, dst);
    if (e != FS_OK) return e;

    e = fs_stat(srcbuf, &scratch_ent);
    if (e != FS_OK) return e;
    if (!scratch_ent.is_dir) return copy_file(srcbuf, dstbuf);

    e = fs_mkdir(dstbuf);                  /* EXIST if taken: no merging */
    if (e != FS_OK) return e;

    size_t sl = strlen(srcbuf), dl = strlen(dstbuf);
    idx[0] = 0;

    for (;;) {
        e = fs_opendir(&scratch_dir, srcbuf);
        for (uint16_t i = 0; e == FS_OK && i <= idx[depth]; i++) {
            e = fs_readdir(&scratch_dir, &scratch_ent);
            if (scratch_ent.name[0] == '\0') break;
        }
        fs_closedir(&scratch_dir);
        if (e != FS_OK) return e;

        if (scratch_ent.name[0] == '\0') {          /* this dir is done */
            if (depth == 0) return FS_OK;
            path_up(srcbuf, &sl);
            path_up(dstbuf, &dl);
            idx[--depth]++;
            continue;
        }

        size_t nl = strlen(scratch_ent.name);
        if (sl + 1 + nl >= sizeof srcbuf || dl + 1 + nl >= sizeof dstbuf)
            return FS_ERR_TOO_LONG;
        srcbuf[sl] = '/'; memcpy(&srcbuf[sl + 1], scratch_ent.name, nl + 1);
        dstbuf[dl] = '/'; memcpy(&dstbuf[dl + 1], scratch_ent.name, nl + 1);
        sl += 1 + nl;
        dl += 1 + nl;

        if (scratch_ent.is_dir) {
            if (depth + 1 >= FS_COPY_DEPTH) return FS_ERR_TOO_LONG;
            e = fs_mkdir(dstbuf);
            if (e != FS_OK) return e;
            idx[++depth] = 0;
        } else {
            e = copy_file(srcbuf, dstbuf);
            if (e != FS_OK) return e;
            path_up(srcbuf, &sl);
            path_up(dstbuf, &dl);
            idx[depth]++;
        }
    }
}