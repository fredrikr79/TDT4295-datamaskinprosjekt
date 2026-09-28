/* ======================================================================
 * port/stm32/diskio_sdmmc.c -- FatFs disk layer on SDMMC1, 4-bit.
 *
 * Counterpart to port/host/diskio_host.c: same ff.c, same ffconf.h, so
 * whatever FAT behaviour you see on host you get here too.
 *
 * Card init happens HERE, on the first f_mount, not in main(). That is
 * deliberate: MX_SDMMC1_SD_Init() was removed from the generated call
 * list so a missing card no longer drops the board into Error_Handler()
 * before the console exists. A missing card is now FR_NOT_READY on the
 * command that touched it.
 *
 * Pins (from HAL_SD_MspInit): PC8-PC11 D0-D3, PC12 CK, PD2 CMD.
 * Kernel clock: PLL1Q. SDMMC1_IRQn is enabled there too, so the IDMA
 * path below is usable -- see SD_USE_DMA.
 * ====================================================================== */
#define LOG_TAG "sd"

#include <string.h>
#include <stdbool.h>

#include "main.h"          /* hsd1, HAL types, pin defines */
#include "ff.h"
#include "diskio.h"
#include "log.h"

extern SD_HandleTypeDef hsd1;

/* 0 = blocking FIFO transfers, 1 = SDMMC internal DMA + completion IRQ.
 * Both spin until the transfer finishes; DMA just costs far less CPU per
 * byte. Bring up with 0, switch to 1 once the card is known good. */
#define SD_USE_DMA      0

#define SECTOR_SIZE     512u

/* Per-transfer timeout, and how long we wait for the card to leave
 * PROGRAMMING/RECEIVING after a write. Both generous on purpose: a cheap
 * card can stall for a surprisingly long time on an internal erase. */
#define SD_XFER_TIMEOUT_MS   1000u
#define SD_READY_TIMEOUT_MS  2000u

/* SDMMC_CK = sdmmc_ker_ck / (2 * ClockDiv), and ClockDiv == 0 means
 * bypass, i.e. SDMMC_CK == sdmmc_ker_ck.
 *
 * MspInit selects PLL1 as the kernel clock. With the PLL in this project
 * (MSI 48 MHz, M=3, N=10, Q=2) that is 80 MHz, so ClockDiv 0 would clock
 * the card at 80 MHz -- far past the 25 MHz an SD card accepts in default
 * speed, and past the 50 MHz high-speed limit too. ClockDiv 2 gives
 * 20 MHz, which is inside spec and plenty for streaming lines.
 *
 * Confirm the 80 MHz in CubeMX's Clock Configuration tab rather than
 * trusting the arithmetic here, then raise or lower to taste. Card
 * identification runs at 400 kHz regardless; HAL_SD_InitCard handles it. */
#define SD_CLOCK_DIV    2u

static DSTATUS drv_status = STA_NOINIT;

/* HAL's FIFO path does 32-bit accesses and IDMA wants a word-aligned
 * address, so an unaligned user buffer is not safe to hand over either
 * way. FatFs's own window buffers are aligned, but a buffer passed
 * straight to f_read() by app code need not be, so unaligned transfers
 * bounce through here one sector at a time. */
static uint32_t bounce[SECTOR_SIZE / 4];

#define IS_ALIGNED(p)   ((((uintptr_t)(p)) & 3u) == 0u)

/* ---------------------------------------------------------------- utils */

static bool wait_card_ready(uint32_t timeout_ms)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd1) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() - t0 >= timeout_ms) return false;   /* wrap-safe */
    }
    return true;
}

/* Any failed transfer is treated as "the card went away": drop back to
 * STA_NOINIT and release the peripheral, so the next f_mount re-runs the
 * full identification sequence. Without this, pulling the card mid-run
 * leaves the driver permanently wedged -- every later access fails even
 * after a card is put back.
 *
 * The cost is that a one-off CRC glitch also forces a re-init. That is
 * the right trade here: re-init takes a few hundred ms and happens on a
 * path that has already failed. */
static void media_lost(void)
{
    if (drv_status & STA_NOINIT) return;      /* already down */
    LOG_WARN("media lost, will re-init on next access");
    HAL_SD_DeInit(&hsd1);
    drv_status = STA_NOINIT;
}

/* ------------------------------------------------------- transfer paths */

#if SD_USE_DMA

/* These override weak HAL symbols. Keep them in this file so there is
 * exactly one owner of the SD callbacks. */
static volatile bool xfer_done;
static volatile bool xfer_error;

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd) { (void)hsd; xfer_done = true; }
void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd) { (void)hsd; xfer_done = true; }

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    (void)hsd;
    xfer_error = true;
    xfer_done  = true;
}

static bool xfer_wait(void)
{
    uint32_t t0 = HAL_GetTick();
    while (!xfer_done) {
        if (HAL_GetTick() - t0 >= SD_XFER_TIMEOUT_MS) {
            HAL_SD_Abort(&hsd1);
            return false;
        }
    }
    return !xfer_error;
}

static bool sd_read_blocks(uint8_t *dst, uint32_t sector, uint32_t count)
{
    xfer_done = xfer_error = false;
    if (HAL_SD_ReadBlocks_DMA(&hsd1, dst, sector, count) != HAL_OK)
        return false;
    /* On a part with a data cache (the U595, not the U545 on the Nucleo)
     * add SCB_InvalidateDCache_by_Addr(dst, count * SECTOR_SIZE) here:
     * IDMA writes SRAM behind the cache's back. */
    return xfer_wait();
}

static bool sd_write_blocks(const uint8_t *src, uint32_t sector, uint32_t count)
{
    xfer_done = xfer_error = false;
    /* ...and SCB_CleanDCache_by_Addr(src, count * SECTOR_SIZE) here, so
     * IDMA reads what the CPU actually wrote. */
    if (HAL_SD_WriteBlocks_DMA(&hsd1, (uint8_t *)(uintptr_t)src,
                               sector, count) != HAL_OK)
        return false;
    return xfer_wait();
}

#else  /* polled */

static bool sd_read_blocks(uint8_t *dst, uint32_t sector, uint32_t count)
{
    return HAL_SD_ReadBlocks(&hsd1, dst, sector, count,
                             SD_XFER_TIMEOUT_MS) == HAL_OK;
}

static bool sd_write_blocks(const uint8_t *src, uint32_t sector, uint32_t count)
{
    return HAL_SD_WriteBlocks(&hsd1, (uint8_t *)(uintptr_t)src, sector, count,
                              SD_XFER_TIMEOUT_MS) == HAL_OK;
}

#endif /* SD_USE_DMA */

/* ------------------------------------------------------------ FatFs API */

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    return drv_status;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv != 0) return STA_NOINIT;
    if (!(drv_status & STA_NOINIT)) return drv_status;   /* already up */

    /* Same settings CubeMX puts in MX_SDMMC1_SD_Init(), except ClockDiv.
     * That function is static in main.c so it cannot be called from here;
     * if you change the SDMMC config in CubeMX, mirror it below.
     * HAL_SD_Init() still calls the generated HAL_SD_MspInit(), so pins,
     * kernel clock and NVIC stay generated. */
    hsd1.Instance                 = SDMMC1;
    hsd1.Init.ClockEdge           = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave      = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.BusWide             = SDMMC_BUS_WIDE_4B;
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd1.Init.ClockDiv            = SD_CLOCK_DIV;

    if (HAL_SD_Init(&hsd1) != HAL_OK) {
        /* No card, no power, or bad wiring. Not fatal any more.
         * HAL_SD_MspDeInit releases the pins and the IRQ, so a later
         * retry (just run the command again) starts from a clean slate. */
        LOG_ERR("HAL_SD_Init failed (card present?)");
        HAL_SD_DeInit(&hsd1);
        drv_status = STA_NOINIT;
        return drv_status;
    }

    if (!wait_card_ready(SD_READY_TIMEOUT_MS)) {
        LOG_ERR("card never reached TRANSFER state");
        HAL_SD_DeInit(&hsd1);
        drv_status = STA_NOINIT;
        return drv_status;
    }

    HAL_SD_CardInfoTypeDef info;
    if (HAL_SD_GetCardInfo(&hsd1, &info) != HAL_OK) {
        LOG_ERR("HAL_SD_GetCardInfo failed");
        HAL_SD_DeInit(&hsd1);
        drv_status = STA_NOINIT;
        return drv_status;
    }

    if (info.LogBlockSize != SECTOR_SIZE) {
        /* ffconf.h pins FF_MIN_SS == FF_MAX_SS == 512. Anything else here
         * would silently corrupt the filesystem, so refuse the card. */
        LOG_ERR("unexpected block size %lu", (unsigned long)info.LogBlockSize);
        HAL_SD_DeInit(&hsd1);
        drv_status = STA_NOINIT;
        return drv_status;
    }

    drv_status = 0;
    LOG_INFO("card ready: %lu blocks (%lu MiB), type %lu",
             (unsigned long)info.LogBlockNbr,
             (unsigned long)(info.LogBlockNbr / 2048u),
             (unsigned long)info.CardType);
    return drv_status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0)               return RES_PARERR;
    if (drv_status & STA_NOINIT) return RES_NOTRDY;
    if (count == 0)              return RES_PARERR;

    if (!wait_card_ready(SD_READY_TIMEOUT_MS)) {
        LOG_ERR("card busy before read");
        media_lost();
        return RES_NOTRDY;
    }

    if (IS_ALIGNED(buff)) {
        if (!sd_read_blocks(buff, (uint32_t)sector, count)) {
            LOG_ERR("read failed at sector %lu (err 0x%lX)",
                    (unsigned long)sector, (unsigned long)hsd1.ErrorCode);
            media_lost();
            return RES_ERROR;
        }
    } else {
        /* One sector at a time through the aligned bounce buffer. */
        for (UINT i = 0; i < count; i++) {
            if (!sd_read_blocks((uint8_t *)bounce, (uint32_t)sector + i, 1)) {
                LOG_ERR("read failed at sector %lu (err 0x%lX)",
                        (unsigned long)(sector + i),
                        (unsigned long)hsd1.ErrorCode);
                media_lost();
                return RES_ERROR;
            }
            memcpy(buff + i * SECTOR_SIZE, bounce, SECTOR_SIZE);
            if (!wait_card_ready(SD_READY_TIMEOUT_MS)) {
                media_lost();
                return RES_ERROR;
            }
        }
    }

    return RES_OK;
}

#if !FF_FS_READONLY

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0)               return RES_PARERR;
    if (drv_status & STA_NOINIT) return RES_NOTRDY;
    if (count == 0)              return RES_PARERR;

    if (!wait_card_ready(SD_READY_TIMEOUT_MS)) {
        LOG_ERR("card busy before write");
        media_lost();
        return RES_NOTRDY;
    }

    if (IS_ALIGNED(buff)) {
        if (!sd_write_blocks(buff, (uint32_t)sector, count)) {
            LOG_ERR("write failed at sector %lu (err 0x%lX)",
                    (unsigned long)sector, (unsigned long)hsd1.ErrorCode);
            media_lost();
            return RES_ERROR;
        }
    } else {
        for (UINT i = 0; i < count; i++) {
            memcpy(bounce, buff + i * SECTOR_SIZE, SECTOR_SIZE);
            if (!sd_write_blocks((const uint8_t *)bounce,
                                 (uint32_t)sector + i, 1)) {
                LOG_ERR("write failed at sector %lu (err 0x%lX)",
                        (unsigned long)(sector + i),
                        (unsigned long)hsd1.ErrorCode);
                media_lost();
                return RES_ERROR;
            }
            if (!wait_card_ready(SD_READY_TIMEOUT_MS)) {
                media_lost();
                return RES_ERROR;
            }
        }
    }

    /* The card is still programming internally when HAL returns. Waiting
     * here means CTRL_SYNC and the next access do not have to. */
    if (!wait_card_ready(SD_READY_TIMEOUT_MS)) {
        LOG_ERR("card stuck programming after write");
        media_lost();
        return RES_ERROR;
    }

    return RES_OK;
}

#endif /* !FF_FS_READONLY */

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0)               return RES_PARERR;
    if (drv_status & STA_NOINIT) return RES_NOTRDY;

    HAL_SD_CardInfoTypeDef info;

    switch (cmd) {
    case CTRL_SYNC:
        /* Nothing is cached on our side; just make sure the card is idle. */
        if (wait_card_ready(SD_READY_TIMEOUT_MS)) return RES_OK;
        media_lost();
        return RES_ERROR;

    case GET_SECTOR_COUNT:
        if (HAL_SD_GetCardInfo(&hsd1, &info) != HAL_OK) {
            media_lost();
            return RES_ERROR;
        }
        *(LBA_t *)buff = (LBA_t)info.LogBlockNbr;
        return RES_OK;

    case GET_SECTOR_SIZE:
        *(WORD *)buff = SECTOR_SIZE;
        return RES_OK;

    case GET_BLOCK_SIZE:
        /* Erase block size in sectors. 1 disables erase-aligned
         * allocation in f_mkfs; we format elsewhere, so it is unused. */
        *(DWORD *)buff = 1;
        return RES_OK;

    case CTRL_TRIM:
        return RES_OK;

    default:
        return RES_PARERR;
    }
}

/* Only referenced when FF_FS_READONLY == 0 && FF_FS_NORTC == 0.
 * No RTC configured in this project, so every file gets the same stamp.
 * Wire this to HAL_RTC_GetTime/GetDate if timestamps start mattering. */
#if !FF_FS_READONLY && !FF_FS_NORTC

DWORD get_fattime(void)
{
    /* 2026-01-01 00:00:00 */
    return ((DWORD)(2026 - 1980) << 25)
         | ((DWORD)1 << 21)
         | ((DWORD)1 << 16);
}

#endif