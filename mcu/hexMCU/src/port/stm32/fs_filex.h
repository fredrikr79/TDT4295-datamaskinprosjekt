#ifndef FS_FILEX_H
#define FS_FILEX_H

/* Handle contents for the FileX backend (port/stm32/fs_filex.c).
 * Include fs.h, not this -- it pulls this in when FS_USE_FILEX is set.
 * Compare include/fs_fatfs.h. */

#include <stdbool.h>

/* TODO(filex): #include "fx_api.h" and add FX_FILE etc. */

struct fs_file {
    bool open;
};

struct fs_dir {
    bool open;
};

#endif /* FS_FILEX_H */
