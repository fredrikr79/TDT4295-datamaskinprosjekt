#ifndef SETTINGS_H
#define SETTINGS_H

/* ======================================================================
 * settings.h - tuned values kept between runs, in SETTINGS_FILE on the SD
 * card (on the PC: in the $HEXMCU_SD folder).
 *
 * The file is a small part of TOML: sections, and whole numbers.
 *
 *   # hexmcu settings
 *   [sprite]
 *   lead_us = 1000
 *
 *   [badapple]
 *   delay_us = 15500
 *
 * A key is "section.name" ("sprite.lead_us"). The file is read once, the
 * first time anything asks; a missing file or card means every setting
 * has its default. Lines it doesn't understand are logged and kept out.
 *
 * The console's tuning commands store their value with "save" instead of a
 * number ("sprite lead save"): settings_save() changes the value and
 * writes the whole file again (comments in it are not kept).
 *
 * Each module reads its settings when it first needs them:
 *   uint32_t lead = (uint32_t)settings_get("sprite.lead_us", SPRITE_SYNC_LEAD_US);
 * ====================================================================== */

#include <stdint.h>
#include <stdbool.h>

#define SETTINGS_FILE     "settings.toml"
#define SETTINGS_MAX      24        /* values in the file              */
#define SETTINGS_KEY_MAX  40        /* "section.name", with its '\0'   */

/* The value from the file, or def if it has none. */
int32_t settings_get(const char *key, int32_t def);

/* Set it and write the file. false: not written (no card, card full...,
 * logged); the value still counts for this run. */
bool settings_save(const char *key, int32_t value);

/* Every value from the file, in file order (the console's "settings"). */
typedef void (*settings_fn)(void *ctx, const char *key, int32_t value);
void settings_list(settings_fn fn, void *ctx);

/* Read the file again (after editing it on the PC, say). */
void settings_reload(void);

#endif
