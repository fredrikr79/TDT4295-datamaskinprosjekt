/* storage_filex.c - storage.h on the STM32: FileX on the SD card (SDMMC1).
 *
 * SD init without a card: CubeMX's MX_SDMMC1_SD_Init() calls
 * Error_Handler() when HAL_SD_Init() fails -- and with no card it always
 * fails, so the board hangs before myMain(). So main.c must NOT call it:
 *
 *   CubeMX -> Project Manager -> Advanced Settings -> Generated Function
 *   Calls: MX_SDMMC1_SD_Init: tick "Do Not Generate Function Call", untick
 *   "Visibility (Static)" (else: "defined but not used" warning).
 *   Keep MX_FileX_Init() (it only calls fx_system_initialize()), and keep
 *   FX_STM32_SD_INIT at 0 in fx_stm32_sd_driver.h, or the FileX driver
 *   calls MX_SDMMC1_SD_Init() itself on media open.
 *
 * Instead sd_init() below does the same init and returns an error. Its
 * settings must match CubeMX's SDMMC1 page -- change both together.
 *
 * SDMMC1 itself must stay ENABLED in CubeMX (only its init call is turned
 * off): that's what generates HAL_SD_MspInit() in stm32u5xx_hal_msp.c --
 * the SDMMC1 clock, its kernel clock source and the pins. Without it the
 * HAL's empty weak HAL_SD_MspInit() runs, the peripheral stays unclocked,
 * and every init sits in the HAL's 5 s software timeouts. sd_init() checks
 * for that and gives up for good instead (one error in the log).
 *
 * The media stays open between calls. Any I/O error drops it (abort, no
 * flush), and the next call mounts again -- that's how a card pulled out
 * and put back is picked up. */

#define LOG_TAG "sd"

#include <stdio.h>
#include <string.h>

#include "main.h"
#include "app_filex.h"          /* fx_api.h, fx_stm32_sd_driver           */
#include "storage.h"
#include "log.h"
#include "platform.h"

/* The SD handle; the FileX glue (fx_stm32_sd_driver_glue.c) uses it too.
 * main.c defines it while CubeMX generates the SDMMC1 init; with "Do Not
 * Generate Function Call" CubeMX may leave it out. Weak: main.c's wins
 * when it's there, this one fills in when it isn't. */
__attribute__((weak)) SD_HandleTypeDef hsd1;

#define PATH_MAX_LEN 64

static FX_MEDIA media;
static UCHAR    media_memory[FX_STM32_SD_DEFAULT_SECTOR_SIZE] __attribute__((aligned(32)));
static bool     sd_up;          /* HAL_SD_Init() succeeded                */
static bool     mounted;        /* fx_media_open() succeeded              */
static bool     tried;
static uint32_t last_try;
static bool     no_hw;          /* SDMMC1 can't work (not clocked): stop trying */
static char     note[96] = "not tried yet";     /* storage_info(): the last thing that happened */

#define SD_INIT_SLOW_MS 3000u   /* an init this slow is a setup problem, not a card */

/* ---- the card ------------------------------------------------------------ */
static bool sd_init(void)
{
    /* Same as CubeMX's MX_SDMMC1_SD_Init(), minus the Error_Handler(). */
    hsd1.Instance                 = SDMMC1;
    hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.BusWide             = SDMMC_BUS_WIDE_4B;
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd1.Init.ClockDiv            = 0;

    /* What HAL_SD_Init() does first, done here so we can check it worked:
     * with no HAL_SD_MspInit() from CubeMX, the SDMMC1 clock stays off. */
    if (hsd1.State == HAL_SD_STATE_RESET) HAL_SD_MspInit(&hsd1);
    if (!__HAL_RCC_SDMMC1_IS_CLK_ENABLED()) {
        LOG_ERR("SDMMC1 has no clock: HAL_SD_MspInit() isn't generated (enable SDMMC1 "
                "in CubeMX, keep only its init call off). SD card disabled.");
        snprintf(note, sizeof note, "SDMMC1 has no clock (CubeMX): SD card disabled");
        no_hw = true;
        return false;
    }

    uint32_t t0 = plat_millis();
    HAL_StatusTypeDef st = HAL_SD_Init(&hsd1);
    uint32_t ms = plat_millis() - t0, err = HAL_SD_GetError(&hsd1);
    if (st != HAL_OK) {
        HAL_SD_DeInit(&hsd1);   /* back to RESET, so the next try starts clean (clears the error) */
        LOG_WARN("no SD card (init failed after %lu ms, error 0x%08lX)",
                 (unsigned long)ms, (unsigned long)err);
        snprintf(note, sizeof note, "no SD card (init failed after %lu ms, error 0x%08lX)",
                 (unsigned long)ms, (unsigned long)err);
        if (ms >= SD_INIT_SLOW_MS) {
            LOG_ERR("SD init took %lu ms: SDMMC1 clock or pins not set up? SD card disabled.",
                    (unsigned long)ms);
            snprintf(note, sizeof note, "SD init took %lu ms: disabled (clock or pins?)",
                     (unsigned long)ms);
            no_hw = true;
        }
        return false;
    }
    return true;
}

static void drop(void)
{
    if (mounted) fx_media_abort(&media);    /* no flush: the card may be gone */
    if (sd_up)   HAL_SD_DeInit(&hsd1);
    mounted = sd_up = false;
}

static storage_status_t mount(void)
{
    if (mounted) return STORAGE_OK;
    if (no_hw) return STORAGE_NO_CARD;
    if (tried && plat_millis() - last_try < STORAGE_RETRY_MS) return STORAGE_NO_CARD;
    tried    = true;
    last_try = plat_millis();

    if (!sd_up) {
        if (!sd_init()) return STORAGE_NO_CARD;     /* logged */
        sd_up = true;
    }
    UINT st = fx_media_open(&media, "HeksSD", fx_stm32_sd_driver, 0,
                            media_memory, sizeof media_memory);
    if (st != FX_SUCCESS) {
        LOG_ERR("SD card: no FAT file system? (fx_media_open 0x%02X)", st);
        snprintf(note, sizeof note, "card found, but no FAT file system (fx_media_open 0x%02X)", st);
        drop();
        return STORAGE_NO_CARD;
    }
    mounted = true;
    LOG_INFO("SD card mounted");
    snprintf(note, sizeof note, "SD card mounted");
    return STORAGE_OK;
}

/* A FileX status -> ours. Errors that mean the card is in trouble drop
 * the media, so the next call mounts again. */
static storage_status_t result(UINT st)
{
    switch (st) {
    case FX_SUCCESS:      return STORAGE_OK;
    case FX_NOT_FOUND:    return STORAGE_NOT_FOUND;
    case FX_NO_MORE_SPACE: return STORAGE_FULL;
    case FX_INVALID_NAME:
    case FX_INVALID_PATH:
    case FX_NOT_A_FILE:
    case FX_NOT_DIRECTORY:
    case FX_DIR_NOT_EMPTY:
    case FX_WRITE_PROTECT:
    case FX_ALREADY_CREATED:
    case FX_ACCESS_ERROR:  return STORAGE_ERROR;     /* our mistake, card fine */
    default:
        LOG_ERR("SD card error 0x%02X -- remounting next time", st);
        snprintf(note, sizeof note, "card error 0x%02X: dropped, mounts again next time", st);
        drop();
        return STORAGE_ERROR;
    }
}

static bool copy_path(const char *path, CHAR *out)
{
    size_t n = path ? strlen(path) : 0;
    if (n == 0 || n >= PATH_MAX_LEN) return false;
    memcpy(out, path, n + 1);
    return true;
}

/* ---- storage.h ------------------------------------------------------------ */
storage_status_t storage_init(void) { return mount(); }
bool storage_ready(void)            { return mounted; }

storage_status_t storage_read(const char *path, uint32_t offset, void *buf,
                              uint32_t max, uint32_t *got)
{
    CHAR p[PATH_MAX_LEN];
    FX_FILE f;
    ULONG n = 0;
    *got = 0;
    storage_status_t s = mount();
    if (s != STORAGE_OK) return s;
    if (!copy_path(path, p)) return STORAGE_ERROR;

    UINT st = fx_file_open(&media, &f, p, FX_OPEN_FOR_READ);
    if (st != FX_SUCCESS) return result(st);
    if (offset) st = fx_file_seek(&f, offset);
    if (st == FX_SUCCESS && max) st = fx_file_read(&f, buf, max, &n);
    if (st == FX_END_OF_FILE) { st = FX_SUCCESS; n = 0; }   /* offset at/after the end */
    UINT cst = fx_file_close(&f);
    if (st == FX_SUCCESS) st = cst;
    if (st != FX_SUCCESS) return result(st);
    *got = (uint32_t)n;
    return STORAGE_OK;
}

storage_status_t storage_size(const char *path, uint32_t *size)
{
    CHAR p[PATH_MAX_LEN];
    FX_FILE f;
    *size = 0;
    storage_status_t s = mount();
    if (s != STORAGE_OK) return s;
    if (!copy_path(path, p)) return STORAGE_ERROR;

    UINT st = fx_file_open(&media, &f, p, FX_OPEN_FOR_READ);
    if (st != FX_SUCCESS) return result(st);
    *size = (uint32_t)f.fx_file_current_file_size;
    return result(fx_file_close(&f));
}

/* make every folder on the way to the file */
static UINT make_parents(CHAR *p)
{
    for (CHAR *c = p; *c; c++) {
        if (*c != '/') continue;
        *c = '\0';
        UINT st = fx_directory_create(&media, p);
        *c = '/';
        if (st != FX_SUCCESS && st != FX_ALREADY_CREATED) return st;
    }
    return FX_SUCCESS;
}

/* append: add to the end (file made if missing); else replace the file */
static storage_status_t write_file(const char *path, const void *data, uint32_t len,
                                   bool append)
{
    CHAR p[PATH_MAX_LEN];
    FX_FILE f;
    storage_status_t s = mount();
    if (s != STORAGE_OK) return s;
    if (!copy_path(path, p)) return STORAGE_ERROR;
    if (len && !data) return STORAGE_ERROR;

    UINT st = make_parents(p);
    if (st != FX_SUCCESS) return result(st);
    st = fx_file_create(&media, p);
    if (st == FX_ALREADY_CREATED) {
        if (append) {
            st = FX_SUCCESS;                    /* keep it, write at the end */
        } else {                                /* replace it */
            st = fx_file_delete(&media, p);
            if (st == FX_SUCCESS) st = fx_file_create(&media, p);
        }
    }
    if (st != FX_SUCCESS) return result(st);

    st = fx_file_open(&media, &f, p, FX_OPEN_FOR_WRITE);
    if (st != FX_SUCCESS) return result(st);
    if (append) st = fx_file_relative_seek(&f, 0, FX_SEEK_END);  /* opens at the start */
    if (st == FX_SUCCESS && len) st = fx_file_write(&f, (VOID *)data, len);
    UINT cst = fx_file_close(&f);
    if (st == FX_SUCCESS) st = cst;
    if (st == FX_SUCCESS) st = fx_media_flush(&media);  /* on the card now, not just cached */
    return result(st);
}

storage_status_t storage_write(const char *path, const void *data, uint32_t len)
{
    return write_file(path, data, len, false);
}

storage_status_t storage_append(const char *path, const void *data, uint32_t len)
{
    return write_file(path, data, len, true);
}

storage_status_t storage_delete(const char *path)
{
    CHAR p[PATH_MAX_LEN];
    storage_status_t s = mount();
    if (s != STORAGE_OK) return s;
    if (!copy_path(path, p)) return STORAGE_ERROR;

    UINT st = fx_file_delete(&media, p);
    if (st == FX_SUCCESS) st = fx_media_flush(&media);
    return result(st);
}

/* Delete everything in folder path, then the folder. FileX only lists the
 * default directory, and deleting while listing would confuse its search,
 * so: find the first entry, set the default back to the root, delete that
 * one (a folder: the same, one level down), and start over until the
 * folder is empty. path is a PATH_MAX_LEN buffer, relative to the root. */
static UINT delete_tree(CHAR *path, unsigned depth, uint32_t *files)
{
    static CHAR name[FX_MAX_LONG_NAME_LEN];         /* copied before recursing */

    for (unsigned guard = 0; guard < 100000u; guard++) {
        UINT attr = 0;
        UINT st = fx_directory_default_set(&media, path);
        if (st != FX_SUCCESS) return st;
        st = fx_directory_first_full_entry_find(&media, name, &attr, FX_NULL,
                                                FX_NULL, FX_NULL, FX_NULL, FX_NULL, FX_NULL, FX_NULL);
        while (st == FX_SUCCESS && (!strcmp((char *)name, ".") || !strcmp((char *)name, "..")))
            st = fx_directory_next_full_entry_find(&media, name, &attr, FX_NULL,
                                                   FX_NULL, FX_NULL, FX_NULL, FX_NULL, FX_NULL, FX_NULL);
        fx_directory_default_set(&media, FX_NULL);  /* paths from the root again */
        if (st == FX_NO_MORE_ENTRIES) break;        /* empty: delete the folder below */
        if (st != FX_SUCCESS) return st;

        CHAR child[PATH_MAX_LEN];
        int n = snprintf((char *)child, sizeof child, "%s/%s", (char *)path, (char *)name);
        if (n <= 0 || n >= (int)sizeof child) return FX_INVALID_NAME;
        if (attr & FX_DIRECTORY) {
            if (depth >= STORAGE_DELETE_DEPTH) return FX_DIR_NOT_EMPTY;
            st = delete_tree(child, depth + 1u, files);
        } else {
            st = fx_file_delete(&media, child);
            if (st == FX_SUCCESS && files) (*files)++;
        }
        if (st != FX_SUCCESS) return st;
    }
    return fx_directory_delete(&media, path);
}

storage_status_t storage_delete_dir(const char *dir, uint32_t *files)
{
    CHAR p[PATH_MAX_LEN];
    if (files) *files = 0;
    storage_status_t s = mount();
    if (s != STORAGE_OK) return s;
    while (dir && *dir == '/') dir++;               /* "/save" = "save"    */
    if (!copy_path(dir, p)) return STORAGE_ERROR;   /* also refuses the root */
    for (size_t n = strlen((char *)p); n && p[n - 1] == '/'; n--) p[n - 1] = '\0';  /* "save/" = "save" */
    if (!p[0]) return STORAGE_ERROR;

    UINT st = fx_directory_name_test(&media, p);    /* FX_NOT_FOUND / FX_NOT_DIRECTORY */
    if (st != FX_SUCCESS) return result(st);

    st = delete_tree(p, 1u, files);
    UINT fst = fx_media_flush(&media);              /* what was deleted, even after an error */
    if (st == FX_SUCCESS) st = fst;
    return result(st);
}

storage_status_t storage_rename(const char *from, const char *to)
{
    CHAR f[PATH_MAX_LEN], t[PATH_MAX_LEN];
    storage_status_t s = mount();
    if (s != STORAGE_OK) return s;
    if (!copy_path(from, f) || !copy_path(to, t)) return STORAGE_ERROR;

    UINT st = fx_file_rename(&media, f, t);
    if (st == FX_ALREADY_CREATED) {                 /* FileX won't replace: delete first */
        st = fx_file_delete(&media, t);
        if (st == FX_SUCCESS) st = fx_file_rename(&media, f, t);
    }
    if (st == FX_SUCCESS) st = fx_media_flush(&media);
    return result(st);
}

storage_status_t storage_info(storage_info_t *info)
{
    memset(info, 0, sizeof *info);
    tried = false;                  /* asked for: try now, not after the retry wait */
    info->where = "SD card (SDMMC1)";
    storage_status_t s = mount();
    info->mounted = mounted;
    info->note    = note;
    if (s != STORAGE_OK) return s;

    info->total = (uint64_t)media.fx_media_total_clusters * media.fx_media_sectors_per_cluster *
                  media.fx_media_bytes_per_sector;
    ULONG64 avail = 0;
    UINT st = fx_media_extended_space_available(&media, &avail);
    if (st != FX_SUCCESS) return result(st);
    info->free = (uint64_t)avail;
    return STORAGE_OK;
}

/* FileX lists the "default directory": set it to dir, list, set it back to
 * the root (FX_NULL), so every other call keeps working from the root. */
storage_status_t storage_list(const char *dir, storage_list_fn fn, void *ctx)
{
    static CHAR name[FX_MAX_LONG_NAME_LEN];
    CHAR p[PATH_MAX_LEN];
    UINT attr = 0;
    ULONG size = 0;

    storage_status_t s = mount();
    if (s != STORAGE_OK) return s;
    if (!dir || !*dir) dir = "/";
    if (!copy_path(dir, p)) return STORAGE_ERROR;

    UINT st = fx_directory_default_set(&media, p);
    if (st != FX_SUCCESS) return result(st);
    st = fx_directory_first_full_entry_find(&media, name, &attr, &size,
                                            FX_NULL, FX_NULL, FX_NULL, FX_NULL, FX_NULL, FX_NULL);
    while (st == FX_SUCCESS) {
        if (strcmp((char *)name, ".") != 0 && strcmp((char *)name, "..") != 0)
            fn(ctx, (const char *)name, (uint32_t)size, (attr & FX_DIRECTORY) != 0);
        st = fx_directory_next_full_entry_find(&media, name, &attr, &size,
                                               FX_NULL, FX_NULL, FX_NULL, FX_NULL, FX_NULL, FX_NULL);
    }
    fx_directory_default_set(&media, FX_NULL);
    return st == FX_NO_MORE_ENTRIES ? STORAGE_OK : result(st);
}

const char *storage_status_str(storage_status_t st)
{
    static const char *const names[] =
        { "ok", "no card", "file not found", "card full", "storage error" };
    return (unsigned)st < sizeof names / sizeof names[0] ? names[st] : "?";
}