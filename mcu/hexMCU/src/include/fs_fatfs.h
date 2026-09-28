#ifndef FS_FATFS_H
#define FS_FATFS_H

#include <stdbool.h>

#include "ff.h"

struct fs_file {
    FIL  fil;
    bool open;
};

struct fs_dir {
    DIR     dir;
    FILINFO fno;
    bool    open;
};

#endif /* FS_FATFS_H */
