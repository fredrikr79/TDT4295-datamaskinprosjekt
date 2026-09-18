#define LOG_TAG "log"
#include "log.h"
#include "io.h"
#include "platform.h"
#include <stdio.h>
#include <stdarg.h>

static log_hook_fn hook_before, hook_after;
static int runtime_level = LOG_LEVEL;

static const char level_char[] = { '-', 'E', 'W', 'I', 'D' };

void log_set_hooks(log_hook_fn before, log_hook_fn after)
{
    hook_before = before;
    hook_after  = after;
}

void log_set_level(int level)
{
    if (level > LOG_LEVEL) level = LOG_LEVEL;   /* can't exceed compiled-in */
    if (level < LOG_LEVEL_NONE) level = LOG_LEVEL_NONE;
    runtime_level = level;
}

int log_get_level(void) { return runtime_level; }

void log_emit(int level, const char *tag, const char *fmt, ...)
{
    if (level > runtime_level) return;

    char buf[160];
    int n = snprintf(buf, sizeof buf, "[%8lu] %c %-8s ",
                     (unsigned long)plat_millis(), level_char[level], tag);
    if (n < 0 || n >= (int)sizeof buf) return;

    va_list ap;
    va_start(ap, fmt);
    int m = vsnprintf(buf + n, sizeof buf - n, fmt, ap);
    va_end(ap);
    if (m < 0) return;

    n += (m < (int)(sizeof buf - n)) ? m : (int)(sizeof buf - n - 1);
    if (n > (int)sizeof buf - 3) n = sizeof buf - 3;
    buf[n++] = '\r';
    buf[n++] = '\n';

    if (hook_before) hook_before();
    io_write(buf, (uint16_t)n);
    if (hook_after) hook_after();
}

void log_raw(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof buf) n = sizeof buf - 1;
    io_write(buf, (uint16_t)n);
}