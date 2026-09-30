#ifndef FS_UTILS_H
#define FS_UTILS_H

/* ======================================================================
 * fs_utils.h - ls cd cat head echo mkdir rm exist mv cp.
 * ====================================================================== */

#include "fs.h"

void fs_cmd_ls   (int argc, char **argv);
void fs_cmd_cd   (int argc, char **argv);
void fs_cmd_cat  (int argc, char **argv);
void fs_cmd_head (int argc, char **argv);
void fs_cmd_echo (int argc, char **argv);
void fs_cmd_mkdir(int argc, char **argv);
void fs_cmd_rm   (int argc, char **argv);
void fs_cmd_exist(int argc, char **argv);
void fs_cmd_mv   (int argc, char **argv);
void fs_cmd_cp   (int argc, char **argv);

#endif /* FS_UTILS_H */