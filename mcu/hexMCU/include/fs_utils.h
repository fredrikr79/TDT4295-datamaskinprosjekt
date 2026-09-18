#ifndef FS_UTILS_H
#define FS_UTILS_H

/* ======================================================================
 * fs_utils.h -- small filesystem commands for the console.
 *
 * Portable: same code on the board (SDMMC) and on host (card.img).
 * The volume is mounted lazily on first use, so nothing here runs
 * until you actually type a command.
 *
 * Requires in ffconf.h:
 *   FF_FS_RPATH     2   (f_chdir + f_getcwd)
 *   FF_FS_MINIMIZE  0   (f_opendir / f_readdir / f_stat)
 *   FF_FS_READONLY  0   (fs_cmd_echo writes)
 * ====================================================================== */

#include "ff.h"

/* Mount the default volume. Safe to call repeatedly; only the first
 * call does work. Also called automatically by the commands below. */
FRESULT     fs_mount(void);
void        fs_unmount(void);
bool        fs_is_mounted(void);

/* FRESULT -> short name, for log messages. */
const char *fs_result_str(FRESULT fr);

/* Console commands. Signatures match cli.c's cmd_fn so they can go
 * straight into the command table. All output goes through log_raw /
 * io_write, and they never block for more than one file operation. */
void fs_cmd_ls  (int argc, char **argv);
void fs_cmd_cd  (int argc, char **argv);
void fs_cmd_cat (int argc, char **argv);
void fs_cmd_echo(int argc, char **argv);

#endif /* FS_UTILS_H */
