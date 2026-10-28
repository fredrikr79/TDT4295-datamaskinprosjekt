/* storage_host.c - storage.h on the PC: files under a local folder laid
 * out like the SD card (rom/, save/, ...).
 *
 * The folder: $HEXMCU_SD if set, else STORAGE_HOST_ROOT (default "sd",
 * relative to where the program runs). Give CMake an absolute one, e.g.
 *   target_compile_definitions(hexmcu_host_common PRIVATE
 *       STORAGE_HOST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/sd")
 * No folder behaves like no card -- handy for testing that path. */

#define LOG_TAG "sd"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#define make_dir(p) mkdir((p), 0777)
#endif

#include "storage.h"
#include "log.h"
#include "platform.h"

#ifndef STORAGE_HOST_ROOT
#define STORAGE_HOST_ROOT "sd"
#endif

#define PATH_MAX_LEN 512

static bool     mounted;
static bool     tried;
static uint32_t last_try;

static const char *root_dir(void)
{
    const char *env = getenv("HEXMCU_SD");
    return env && *env ? env : STORAGE_HOST_ROOT;
}

static storage_status_t mount(void)
{
    if (mounted) return STORAGE_OK;
    if (tried && plat_millis() - last_try < STORAGE_RETRY_MS) return STORAGE_NO_CARD;
    tried    = true;
    last_try = plat_millis();

    struct stat st;
    if (stat(root_dir(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        LOG_WARN("no card: folder '%s' not found", root_dir());
        return STORAGE_NO_CARD;
    }
    LOG_INFO("card: folder '%s'", root_dir());
    mounted = true;
    return STORAGE_OK;
}

/* root + "/" + path; refuses ".." so nothing outside the folder is touched */
static bool full_path(const char *path, char *out)
{
    if (!path || !*path || strstr(path, "..")) return false;
    int n = snprintf(out, PATH_MAX_LEN, "%s/%s", root_dir(), path);
    return n > 0 && n < PATH_MAX_LEN;
}

static storage_status_t from_errno(int e)
{
    switch (e) {
    case ENOENT: return STORAGE_NOT_FOUND;
    case ENOSPC: return STORAGE_FULL;
    default:     return STORAGE_ERROR;
    }
}

storage_status_t storage_init(void) { return mount(); }
bool storage_ready(void)            { return mounted; }

storage_status_t storage_read(const char *path, uint32_t offset, void *buf,
                              uint32_t max, uint32_t *got)
{
    char fp[PATH_MAX_LEN];
    *got = 0;
    storage_status_t st = mount();
    if (st != STORAGE_OK) return st;
    if (!full_path(path, fp)) return STORAGE_ERROR;

    FILE *f = fopen(fp, "rb");
    if (!f) return from_errno(errno);
    if (fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return STORAGE_ERROR; }
    size_t n = fread(buf, 1, max, f);
    bool bad = ferror(f) != 0;
    fclose(f);
    if (bad) return STORAGE_ERROR;
    *got = (uint32_t)n;
    return STORAGE_OK;
}

storage_status_t storage_size(const char *path, uint32_t *size)
{
    char fp[PATH_MAX_LEN];
    *size = 0;
    storage_status_t st = mount();
    if (st != STORAGE_OK) return st;
    if (!full_path(path, fp)) return STORAGE_ERROR;

    struct stat s;
    if (stat(fp, &s) != 0) return from_errno(errno);
    if (!S_ISREG(s.st_mode)) return STORAGE_NOT_FOUND;
    *size = (uint32_t)s.st_size;
    return STORAGE_OK;
}

/* make every folder on the way to the file */
static void make_parents(char *fp)
{
    size_t skip = strlen(root_dir()) + 1;           /* the root exists */
    for (char *p = fp + skip; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        make_dir(fp);                               /* EEXIST is fine */
        *p = '/';
    }
}

storage_status_t storage_write(const char *path, const void *data, uint32_t len)
{
    char fp[PATH_MAX_LEN];
    storage_status_t st = mount();
    if (st != STORAGE_OK) return st;
    if (!full_path(path, fp)) return STORAGE_ERROR;

    make_parents(fp);
    FILE *f = fopen(fp, "wb");
    if (!f) return from_errno(errno);
    size_t n = len ? fwrite(data, 1, len, f) : 0;
    int e = errno;
    if (fclose(f) != 0 || n != len) {
        LOG_ERR("write %s: %s", path, strerror(e));
        return from_errno(e);
    }
    return STORAGE_OK;
}

storage_status_t storage_delete(const char *path)
{
    char fp[PATH_MAX_LEN];
    storage_status_t st = mount();
    if (st != STORAGE_OK) return st;
    if (!full_path(path, fp)) return STORAGE_ERROR;
    return remove(fp) == 0 ? STORAGE_OK : from_errno(errno);
}

const char *storage_status_str(storage_status_t st)
{
    static const char *const names[] =
        { "ok", "no card", "file not found", "card full", "storage error" };
    return (unsigned)st < sizeof names / sizeof names[0] ? names[st] : "?";
}