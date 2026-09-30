/* =======================================================================
 * transport_ospi.c - OCTOSPI + DMA backend
 *
 * Requirements outside this file (CubeMX):
 *   - OCTOSPI1 global interrupt ENABLED in NVIC, and HAL_OSPI_IRQHandler()
 *     called from OCTOSPI1_IRQHandler(). The DMA "done" is NOT the end of
 *     the transfer: the HAL waits for the OCTOSPI TC interrupt before it
 *     calls Tx/RxCpltCallback. Without that IRQ, done never goes to 1.
 *   - A GPDMA channel linked to hospi1 (handle_GPDMA1_Channel12), byte
 *     data width. One channel is enough; the HAL flips its direction
 *     for each Transmit_DMA / Receive_DMA call.
 *   - hospi1.Init.DeviceSize = 32. The x/y pair is sent in the address
 *     phase, and in indirect mode an address beyond DeviceSize raises a
 *     transfer error. 32 (= 4 GiB) means no x/y value is ever out of range.
 * ======================================================================= */
#define LOG_TAG "ospi"

#include "transport.h"
#include "log.h"
#include "main.h"
#include <stddef.h>

extern OSPI_HandleTypeDef hospi1;

/* Max time HAL_OSPI_Command() may spin waiting for BUSY to clear. The bus
 * should already be idle when we get here, so this only matters if
 * something is badly wrong. */
#define OSPI_CMD_TIMEOUT_MS  2u

static spi_backend_t ospi_backend;

static transport_status_t from_hal(HAL_StatusTypeDef st)
{
    switch (st) {
    case HAL_OK:   return TRANSPORT_OK;
    case HAL_BUSY: return TRANSPORT_BUSY;
    default:       return TRANSPORT_ERR;
    }
}

/*
 * Build a regular (indirect-mode) command from our header.
 *
 * Why the phases are used like this:
 *   The HAL rejects any command with neither an instruction nor an address
 *   phase (OSPI_ConfigCmd() -> HAL_ERROR, ErrorCode = INVALID_PARAM), and a
 *   read is triggered by writing IR or AR. So a pure "data only" transfer
 *   is impossible through the HAL -- the opcode always goes in the
 *   instruction phase, for reads too. Putting x/y and len in the address / alternate-bytes phases
 *   as well means the pixel payload can be DMA'd straight from the
 *   caller's buffer, with no copy into a "header + payload" scratch buffer.
 */
static void ospi_cmd_common(OSPI_RegularCmdTypeDef *cmd)
{
    *cmd = (OSPI_RegularCmdTypeDef){0};

    cmd->OperationType      = HAL_OSPI_OPTYPE_COMMON_CFG;   /* indirect mode (not memory-mapped) */
    cmd->FlashId            = HAL_OSPI_FLASH_ID_1;          /* only matters in dual-quad, but must be valid */
    cmd->DQSMode            = HAL_OSPI_DQS_DISABLE;         /* FPGA has no data strobe */
    cmd->SIOOMode           = HAL_OSPI_SIOO_INST_EVERY_CMD; /* send the instruction on every transaction */
    cmd->InstructionMode    = HAL_OSPI_INSTRUCTION_8_LINES;
    cmd->InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
    cmd->AddressMode        = HAL_OSPI_ADDRESS_NONE;
    cmd->AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    cmd->DataDtrMode        = HAL_OSPI_DATA_DTR_DISABLE;
}

/* Header phases, shared by write and read:
 *   instruction     = opcode (1 byte)
 *   address         = x, y   (4 bytes, MSB first)       if TRANSPORT_F_XY
 *   alternate bytes = alt    (2 or 4 bytes, MSB first)  if TRANSPORT_F_ALT16/32 */
static void ospi_build_hdr(OSPI_RegularCmdTypeDef *cmd,
                           const transport_hdr_t *hdr)
{
    ospi_cmd_common(cmd);

    cmd->Instruction     = hdr->opcode;
    cmd->InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;

    if (hdr->fields & TRANSPORT_F_XY) {
        cmd->Address        = ((uint32_t)hdr->x << 16) | hdr->y;
        cmd->AddressMode    = HAL_OSPI_ADDRESS_8_LINES;
        cmd->AddressSize    = HAL_OSPI_ADDRESS_32_BITS;
        cmd->AddressDtrMode = HAL_OSPI_ADDRESS_DTR_DISABLE;
    }

    unsigned alt = transport_alt_bytes(hdr);
    if (alt) {
        /* Works with or without the address phase in front of it. */
        cmd->AlternateBytes        = (alt == 4u) ? hdr->alt
                                                 : (hdr->alt & 0xFFFFu);
        cmd->AlternateBytesMode    = HAL_OSPI_ALTERNATE_BYTES_8_LINES;
        cmd->AlternateBytesSize    = (alt == 4u)
                                   ? HAL_OSPI_ALTERNATE_BYTES_32_BITS
                                   : HAL_OSPI_ALTERNATE_BYTES_16_BITS;
        cmd->AlternateBytesDtrMode = HAL_OSPI_ALTERNATE_BYTES_DTR_DISABLE;
    }
}

/* Write: header, then n payload bytes (none if n == 0). */
static void ospi_build_write(OSPI_RegularCmdTypeDef *cmd,
                             const transport_hdr_t *hdr, uint16_t n)
{
    ospi_build_hdr(cmd, hdr);

    if (n > 0u) {
        cmd->DataMode = HAL_OSPI_DATA_8_LINES;
        cmd->NbData   = n;                  /* HAL writes NbData - 1 into DLR */
    } else {
        cmd->DataMode = HAL_OSPI_DATA_NONE;
    }
    cmd->DummyCycles = 0u;
}

/* Read: header, turnaround, then n data bytes. The instruction phase is
 * always present, which is also what the HAL needs to start a read. */
static void ospi_build_read(OSPI_RegularCmdTypeDef *cmd,
                            const transport_hdr_t *hdr, uint16_t n)
{
    ospi_build_hdr(cmd, hdr);

    cmd->DataMode    = HAL_OSPI_DATA_8_LINES;
    cmd->NbData      = n;
    cmd->DummyCycles = TRANSPORT_TURNAROUND_CYCLES;   /* bus handover to the FPGA */
}

/* If a start failed halfway (e.g. Command() OK but Transmit_DMA() refused),
 * the HAL is left in CMD_CFG and would reject every later Command() with
 * INVALID_SEQUENCE. Abort puts it back to READY. */
static void ospi_recover(void)
{
    if (HAL_OSPI_GetState(&hospi1) != HAL_OSPI_STATE_READY) {
        __HAL_OSPI_DISABLE_IT(&hospi1, HAL_OSPI_IT_TC | HAL_OSPI_IT_TE | HAL_OSPI_IT_FT);
        (void)HAL_OSPI_Abort(&hospi1);
    }
}

static transport_status_t ospi_backend_init(void)
{
    /* MX_OCTOSPI1_Init() already ran. If it failed (or wasn't called),
     * the handle isn't READY and we'd rather know now than at the first
     * transfer. Put any FPGA reset/handshake GPIO sequence here later. */
    ospi_backend.error = 0;
    ospi_backend.done  = 1;

    if (HAL_OSPI_GetState(&hospi1) != HAL_OSPI_STATE_READY) {
        LOG_ERR("hospi1 not READY -- did MX_OCTOSPI1_Init() succeed?");
        return TRANSPORT_ERR;
    }
    return TRANSPORT_OK;
}

static transport_status_t ospi_backend_write(const transport_hdr_t *hdr,
                                             const uint8_t *data)
{
    OSPI_RegularCmdTypeDef cmd;
    HAL_StatusTypeDef st;

    if (hdr == NULL) return TRANSPORT_ERR;

    uint16_t n = hdr->len;
    if (n > 0u && data == NULL) return TRANSPORT_ERR;
    if (!ospi_backend.done)     return TRANSPORT_BUSY;

    ospi_build_write(&cmd, hdr, n);

    /* Mark busy BEFORE starting, so a fast completion IRQ can't be lost. */
    ospi_backend.error = 0;
    ospi_backend.done  = 0;

    if (n == 0u) {
        /* Header only: the transaction starts as soon as the registers are
         * written. _IT version returns immediately; CmdCpltCallback fires
         * when it's finished. */
        st = HAL_OSPI_Command_IT(&hospi1, &cmd);
    } else {
        /* With a data phase, Command() only configures the registers;
         * nothing moves until Transmit_DMA() feeds the FIFO. */
        st = HAL_OSPI_Command(&hospi1, &cmd, OSPI_CMD_TIMEOUT_MS);
        if (st == HAL_OK) {
            /* DMA only reads the buffer; the HAL prototype just isn't const. */
            st = HAL_OSPI_Transmit_DMA(&hospi1, (uint8_t *)data);
        }
    }

    if (st != HAL_OK) {
        LOG_ERR("write start failed: hal=%d ospi_err=0x%lX", (int)st,
                (unsigned long)HAL_OSPI_GetError(&hospi1));
        ospi_recover();
        ospi_backend.done = 1;   /* nothing in flight */
        return from_hal(st);
    }
    return TRANSPORT_OK;         /* done -> 1 later, from a callback below */
}

static transport_status_t ospi_backend_read(const transport_hdr_t *hdr,
                                            uint8_t *buf)
{
    OSPI_RegularCmdTypeDef cmd;
    HAL_StatusTypeDef st;

    if (hdr == NULL || buf == NULL) return TRANSPORT_ERR;

    uint16_t n = hdr->len;
    if (n == 0u)            return TRANSPORT_ERR;   /* a read needs a data phase */
    if (!ospi_backend.done) return TRANSPORT_BUSY;

    ospi_build_read(&cmd, hdr, n);

    ospi_backend.error = 0;
    ospi_backend.done  = 0;

    st = HAL_OSPI_Command(&hospi1, &cmd, OSPI_CMD_TIMEOUT_MS);
    if (st == HAL_OK) {
        st = HAL_OSPI_Receive_DMA(&hospi1, buf);
    }

    if (st != HAL_OK) {
        LOG_ERR("read start failed: hal=%d ospi_err=0x%lX", (int)st,
                (unsigned long)HAL_OSPI_GetError(&hospi1));
        ospi_recover();
        ospi_backend.done = 1;
        return from_hal(st);
    }
    return TRANSPORT_OK;
}

/* Called from the READY EXTI ISR, while READY is high.
 * TODO: read the 3 FPGA status GPIOs here, e.g.
 *       return (uint8_t)((STATUS_GPIO_Port->IDR >> STATUS0_Pin_Pos) & 0x7u);
 * Until the pins and codes are agreed, every command reports OK (0). */
static uint8_t ospi_read_status(void)
{
    return 0u;
}

static void ospi_backend_abort(void)
{
    ospi_recover();
    ospi_backend.error = 1;
    ospi_backend.done  = 1;
}

static spi_backend_t ospi_backend = {
    .init  = ospi_backend_init,
    .write = ospi_backend_write,
    .read  = ospi_backend_read,
    .abort = ospi_backend_abort,
    .poll  = NULL,          /* driven by DMA + interrupts */
    .read_status = ospi_read_status,
    .done  = 1,
    .error = 0,
};

spi_backend_t *const BACKEND = &ospi_backend;

/* ---- HAL callbacks (override the __weak versions) ----------------------
 * If USE_HAL_OSPI_REGISTER_CALLBACKS is 1 in stm32u5xx_hal_conf.h these
 * are never called -- keep it at 0, or register them instead.
 *
 * No LOG_* from here: log_emit() formats into a 160-byte buffer and calls
 * io_write(), which on this target is a blocking HAL_UART_Transmit. Doing
 * that from an ISR would stall the link for milliseconds. Set flags here,
 * print from the main loop. */

void HAL_OSPI_TxCpltCallback(OSPI_HandleTypeDef *hospi)    /* write with payload finished */
{
    if (hospi->Instance == hospi1.Instance) {
        ospi_backend.done = 1;
    }
}

void HAL_OSPI_CmdCpltCallback(OSPI_HandleTypeDef *hospi)   /* header-only write finished */
{
    if (hospi->Instance == hospi1.Instance) {
        ospi_backend.done = 1;
    }
}

void HAL_OSPI_RxCpltCallback(OSPI_HandleTypeDef *hospi)    /* read finished, buf is valid */
{
    if (hospi->Instance == hospi1.Instance) {
        ospi_backend.done = 1;
    }
}

void HAL_OSPI_ErrorCallback(OSPI_HandleTypeDef *hospi)     /* transfer or DMA error */
{
    if (hospi->Instance == hospi1.Instance) {
        ospi_backend.error = 1;  /* set error BEFORE done, so a poller that */
        ospi_backend.done  = 1;  /* sees done == 1 also sees the error      */
    }
}