/* ======================================================================
 * settings.c - tuned values kept between runs (see settings.h).
 * Portable: the file goes through storage.h.
 * ====================================================================== */
#define LOG_TAG "settings"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "settings.h"
#include "storage.h"
#include "log.h"

#define FILE_MAX 2048u              /* the file, read or written in one go */

static struct {
    char    key[SETTINGS_KEY_MAX];
    int32_t value;
} vals[SETTINGS_MAX];
static unsigned nvals;
static bool     loaded;
static char     buf[FILE_MAX + 1];

static int find(const char *key)
{
    for (unsigned i = 0; i < nvals; i++)
        if (!strcmp(vals[i].key, key)) return (int)i;
    return -1;
}

static bool put(const char *key, int32_t value)
{
    int i = find(key);
    if (i >= 0) { vals[i].value = value; return true; }
    if (nvals == SETTINGS_MAX || strlen(key) >= SETTINGS_KEY_MAX) return false;
    snprintf(vals[nvals].key, SETTINGS_KEY_MAX, "%s", key);
    vals[nvals].value = value;
    nvals++;
    return true;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
    return s;
}

/* A bare TOML name: letters, digits, _ and -. */
static bool valid_name(const char *s)
{
    if (!*s) return false;
    for (; *s; s++)
        if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-') return false;
    return true;
}

/* The TOML this file needs: comments, [section], name = whole number. */
static void parse(char *text)
{
    char section[SETTINGS_KEY_MAX] = "";
    unsigned lineno = 0;
    for (char *line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *s = trim(line);
        if (!*s) continue;

        if (*s == '[') {
            char *end = strchr(s, ']');
            if (end && end[1] == '\0') {
                *end = '\0';
                char *name = trim(s + 1);
                if (valid_name(name) && strlen(name) < sizeof section) {
                    snprintf(section, sizeof section, "%s", name);
                    continue;
                }
            }
        } else {
            char *eq = strchr(s, '=');
            if (eq) {
                *eq = '\0';
                char *name = trim(s), *val = trim(eq + 1), *end;
                long v = strtol(val, &end, 0);
                char key[SETTINGS_KEY_MAX];
                int n = section[0] ? snprintf(key, sizeof key, "%s.%s", section, name)
                                   : snprintf(key, sizeof key, "%s", name);
                if (valid_name(name) && *val && *end == '\0' && n > 0 && n < (int)sizeof key) {
                    if (!put(key, (int32_t)v))
                        LOG_WARN("%s line %u: more than %u values, '%s' ignored",
                                 SETTINGS_FILE, lineno, SETTINGS_MAX, key);
                    continue;
                }
            }
        }
        LOG_WARN("%s line %u: not understood (only [section] and name = number), ignored",
                 SETTINGS_FILE, lineno);
    }
}

static void load(void)
{
    loaded = true;
    nvals  = 0;
    uint32_t got = 0;
    storage_status_t st = storage_read(SETTINGS_FILE, 0, buf, FILE_MAX, &got);
    if (st != STORAGE_OK) {
        LOG_INFO("no %s (%s): defaults", SETTINGS_FILE, storage_status_str(st));
        return;
    }
    buf[got] = '\0';
    parse(buf);
    LOG_INFO("%s: %u value%s", SETTINGS_FILE, nvals, nvals == 1 ? "" : "s");
}

int32_t settings_get(const char *key, int32_t def)
{
    if (!loaded) load();
    int i = find(key);
    return i >= 0 ? vals[i].value : def;
}

void settings_reload(void)
{
    load();
}

void settings_list(settings_fn fn, void *ctx)
{
    if (!loaded) load();
    for (unsigned i = 0; i < nvals; i++) fn(ctx, vals[i].key, vals[i].value);
}

/* The whole file again: values without a section first, then each section
 * in the order it first appears. */
static bool write_file(void)
{
    size_t n = (size_t)snprintf(buf, sizeof buf,
                                "# hexmcu settings: written by the console (\"... save\")\n");
    bool done[SETTINGS_MAX] = { false };

    for (unsigned i = 0; i < nvals; i++)
        if (!strchr(vals[i].key, '.')) {
            n += (size_t)snprintf(buf + n, sizeof buf - n, "%s = %ld\n", vals[i].key,
                                  (long)vals[i].value);
            done[i] = true;
        }
    for (unsigned i = 0; i < nvals; i++) {
        if (done[i]) continue;
        size_t slen = (size_t)(strchr(vals[i].key, '.') - vals[i].key);
        n += (size_t)snprintf(buf + n, sizeof buf - n, "\n[%.*s]\n", (int)slen, vals[i].key);
        for (unsigned j = i; j < nvals; j++) {
            if (done[j] || strncmp(vals[j].key, vals[i].key, slen) || vals[j].key[slen] != '.')
                continue;
            n += (size_t)snprintf(buf + n, sizeof buf - n, "%s = %ld\n", vals[j].key + slen + 1,
                                  (long)vals[j].value);
            done[j] = true;
        }
        if (n >= sizeof buf) break;
    }
    if (n >= sizeof buf) {
        LOG_ERR("%s: more than %u bytes, not written", SETTINGS_FILE, FILE_MAX);
        return false;
    }
    storage_status_t st = storage_write(SETTINGS_FILE, buf, (uint32_t)n);
    if (st != STORAGE_OK) {
        LOG_ERR("%s not written: %s", SETTINGS_FILE, storage_status_str(st));
        return false;
    }
    return true;
}

bool settings_save(const char *key, int32_t value)
{
    if (!loaded) load();
    if (!put(key, value)) {
        LOG_ERR("can't keep '%s': more than %u settings, or the name is too long",
                key, SETTINGS_MAX);
        return false;
    }
    return write_file();
}
