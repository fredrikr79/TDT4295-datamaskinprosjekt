#pragma once
#include <stdint.h>

#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERR   1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#ifndef LOG_TAG
#define LOG_TAG "app"
#endif

void log_emit(int level, const char *tag, const char *fmt, ...);
void log_raw(const char *fmt, ...);   /* no prefix, no newline: prompts, echo */

/* Redraw hooks so log output doesn't trample a half-typed command line. */
typedef void (*log_hook_fn)(void);
void log_set_hooks(log_hook_fn before, log_hook_fn after);

/* Runtime level, capped by the compile-time LOG_LEVEL above. */
void log_set_level(int level);
int  log_get_level(void);

#define LOG_NOOP(...) do { if (0) log_emit(0, LOG_TAG, __VA_ARGS__); } while (0)

#if LOG_LEVEL >= LOG_LEVEL_ERR
#  define LOG_ERR(...)  log_emit(LOG_LEVEL_ERR,  LOG_TAG, __VA_ARGS__)
#else
#  define LOG_ERR(...)  LOG_NOOP(__VA_ARGS__)
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
#  define LOG_WARN(...) log_emit(LOG_LEVEL_WARN, LOG_TAG, __VA_ARGS__)
#else
#  define LOG_WARN(...) LOG_NOOP(__VA_ARGS__)
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
#  define LOG_INFO(...) log_emit(LOG_LEVEL_INFO, LOG_TAG, __VA_ARGS__)
#else
#  define LOG_INFO(...) LOG_NOOP(__VA_ARGS__)
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
#  define LOG_DBG(...)  log_emit(LOG_LEVEL_DEBUG, LOG_TAG, __VA_ARGS__)
#else
#  define LOG_DBG(...)  LOG_NOOP(__VA_ARGS__)
#endif