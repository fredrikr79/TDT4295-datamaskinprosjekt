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
    if (HAL_SD_Init(&hsd1) != HAL_OK) {
        HAL_SD_DeInit(&hsd1);   /* back to RESET, so the next try starts clean */
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
    if (tried && plat_millis() - last_try < STORAGE_RETRY_MS) return STORAGE_NO_CARD;
    tried    = true;
    last_try = plat_millis();

    if (!sd_up) {
        if (!sd_init()) {
            LOG_WARN("no SD card (init failed, error 0x%08lX)",
                     (unsigned long)HAL_SD_GetError(&hsd1));
            return STORAGE_NO_CARD;
        }
        sd_up = true;
    }
    UINT st = fx_media_open(&media, "HeksSD", fx_stm32_sd_driver, 0,
                            media_memory, sizeof media_memory);
    if (st != FX_SUCCESS) {
        LOG_ERR("SD card: no FAT file system? (fx_media_open 0x%02X)", st);
        drop();
        return STORAGE_NO_CARD;
    }
    mounted = true;
    LOG_INFO("SD card mounted");
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
    case FX_ALREADY_CREATED:
    case FX_ACCESS_ERROR:  return STORAGE_ERROR;     /* our mistake, card fine */
    default:
        LOG_ERR("SD card error 0x%02X -- remounting next time", st);
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

storage_status_t storage_write(const char *path, const void *data, uint32_t len)
{
    CHAR p[PATH_MAX_LEN];
    FX_FILE f;
    storage_status_t s = mount();
    if (s != STORAGE_OK) return s;
    if (!copy_path(path, p)) return STORAGE_ERROR;

    UINT st = make_parents(p);
    if (st != FX_SUCCESS) return result(st);
    st = fx_file_create(&media, p);
    if (st == FX_ALREADY_CREATED) {             /* replace it */
        st = fx_file_delete(&media, p);
        if (st == FX_SUCCESS) st = fx_file_create(&media, p);
    }
    if (st != FX_SUCCESS) return result(st);

    st = fx_file_open(&media, &f, p, FX_OPEN_FOR_WRITE);
    if (st != FX_SUCCESS) return result(st);
    if (len) st = fx_file_write(&f, (VOID *)data, len);
    UINT cst = fx_file_close(&f);
    if (st == FX_SUCCESS) st = cst;
    if (st == FX_SUCCESS) st = fx_media_flush(&media);  /* on the card now, not just cached */
    return result(st);
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

const char *storage_status_str(storage_status_t st)
{
    static const char *const names[] =
        { "ok", "no card", "file not found", "card full", "storage error" };
    return (unsigned)st < sizeof names / sizeof names[0] ? names[st] : "?";
}