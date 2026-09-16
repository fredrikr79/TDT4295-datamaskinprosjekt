#include "transport.h"
#include "fpgacon.h"
#include <stddef.h>

HAL_StatusTypeDef transport_init(spi_backend_t *backend)
{
    if (backend == NULL || backend->init == NULL) {
        return HAL_ERROR;
    }
    return backend->init();
}

#ifdef HAL_OSPI_MODULE_ENABLED
/* =======================================================================
 * OCTOSPI + DMA backend
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

extern OSPI_HandleTypeDef hospi1;

/* Max time HAL_OSPI_Command() may spin waiting for BUSY to clear. The bus
 * should already be idle when we get here, so this only matters if
 * something is badly wrong -- keep it short instead of the HAL's 5 s. */
#define OSPI_CMD_TIMEOUT_MS  2u

/*
 * Build a regular (indirect-mode) command from our header.
 *
 * Why the phases are used like this:
 *   The HAL rejects any command with neither an instruction nor an address
 *   phase (OSPI_ConfigCmd() -> HAL_ERROR, ErrorCode = INVALID_PARAM), and a
 *   read is triggered by writing IR or AR. So a pure "data only" transfer
 *   is impossible through the HAL -- the opcode goes in the instruction
 *   phase. Putting x/y and len in the address / alternate-bytes phases
 *   as well means the pixel payload can be DMA'd straight from the
 *   caller's buffer, with no copy into a "header + payload" scratch buffer.
 */
static void ospi_build_cmd(OSPI_RegularCmdTypeDef *cmd, const transport_hdr_t *hdr,
                           uint16_t n, uint32_t dummy_cycles)
{
    *cmd = (OSPI_RegularCmdTypeDef){0};

    cmd->OperationType = HAL_OSPI_OPTYPE_COMMON_CFG;   /* indirect mode (not memory-mapped) */
    cmd->FlashId       = HAL_OSPI_FLASH_ID_1;          /* only matters in dual-quad, but must be valid */
    cmd->DQSMode       = HAL_OSPI_DQS_DISABLE;         /* FPGA has no data strobe */
    cmd->SIOOMode      = HAL_OSPI_SIOO_INST_EVERY_CMD; /* send the opcode on every transaction */

    if(OSPI_PADING){
        cmd->Instruction     = (OSPI_PAD << 8) | hdr->opcode;
        cmd->InstructionMode = HAL_OSPI_INSTRUCTION_8_LINES;
        cmd->InstructionSize = HAL_OSPI_INSTRUCTION_16_BITS;
        cmd->InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
    } else {
        /* Instruction phase: 1 byte opcode on 8 lines */
        cmd->Instruction        = hdr->opcode;
        cmd->InstructionMode    = HAL_OSPI_INSTRUCTION_8_LINES;
        cmd->InstructionSize    = HAL_OSPI_INSTRUCTION_8_BITS;
        cmd->InstructionDtrMode = HAL_OSPI_INSTRUCTION_DTR_DISABLE;
    }

    if (hdr->has_args) {
        /* Address phase: 4 bytes, MSB first -> x_hi x_lo y_hi y_lo */
        cmd->Address        = ((uint32_t)hdr->x << 16) | hdr->y;
        cmd->AddressMode    = HAL_OSPI_ADDRESS_8_LINES;
        cmd->AddressSize    = HAL_OSPI_ADDRESS_32_BITS;
        cmd->AddressDtrMode = HAL_OSPI_ADDRESS_DTR_DISABLE;

        /* Alternate-bytes phase: 2 bytes, MSB first -> len_hi len_lo */
        cmd->AlternateBytes        = hdr->len;
        cmd->AlternateBytesMode    = HAL_OSPI_ALTERNATE_BYTES_8_LINES;
        cmd->AlternateBytesSize    = HAL_OSPI_ALTERNATE_BYTES_16_BITS;
        cmd->AlternateBytesDtrMode = HAL_OSPI_ALTERNATE_BYTES_DTR_DISABLE;
    } else {
        cmd->AddressMode        = HAL_OSPI_ADDRESS_NONE;
        cmd->AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    }

    if (n > 0u) {
        cmd->DataMode    = HAL_OSPI_DATA_8_LINES;
        cmd->DataDtrMode = HAL_OSPI_DATA_DTR_DISABLE;
        cmd->NbData      = n;               /* HAL writes NbData - 1 into DLR */
        cmd->DummyCycles = dummy_cycles;    /* clocks between header and data */
    } else {
        cmd->DataMode    = HAL_OSPI_DATA_NONE;
        cmd->DummyCycles = 0u;
    }
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

static HAL_StatusTypeDef ospi_backend_init(void)
{
    /* MX_OCTOSPI1_Init() already ran. If it failed (or wasn't called),
     * the handle isn't READY and we'd rather know now than at the first
     * transfer. Put any FPGA reset/handshake GPIO sequence here later. */
    ospi_backend.error = 0;
    ospi_backend.done  = 1;
    return (HAL_OSPI_GetState(&hospi1) == HAL_OSPI_STATE_READY) ? HAL_OK : HAL_ERROR;
}

static HAL_StatusTypeDef ospi_backend_write(const transport_hdr_t *hdr, const uint8_t *data, uint16_t n)
{
    OSPI_RegularCmdTypeDef cmd;
    HAL_StatusTypeDef st;

    if (hdr == NULL || (n > 0u && data == NULL)) {
        return HAL_ERROR;
    }
    if (!ospi_backend.done) {
        return HAL_BUSY;
    }

    ospi_build_cmd(&cmd, hdr, n, 0u);

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
        ospi_recover();
        ospi_backend.done = 1;   /* nothing in flight */
        return HAL_ERROR;
    }
    return HAL_OK;               /* done -> 1 later, from a callback below */
}

static HAL_StatusTypeDef ospi_backend_read(const transport_hdr_t *hdr, uint8_t *buf, uint16_t n)
{
    OSPI_RegularCmdTypeDef cmd;
    HAL_StatusTypeDef st;

    if (hdr == NULL || buf == NULL || n == 0u) {
        return HAL_ERROR;   /* a read needs a data phase */
    }
    if (!ospi_backend.done) {
        return HAL_BUSY;
    }

    ospi_build_cmd(&cmd, hdr, n, TRANSPORT_TURNAROUND_CYCLES);

    ospi_backend.error = 0;
    ospi_backend.done  = 0;

    st = HAL_OSPI_Command(&hospi1, &cmd, OSPI_CMD_TIMEOUT_MS);
    if (st == HAL_OK) {
        st = HAL_OSPI_Receive_DMA(&hospi1, buf);
    }

    if (st != HAL_OK) {
        ospi_recover();
        ospi_backend.done = 1;
        return HAL_ERROR;
    }
    return HAL_OK;
}

static void ospi_backend_abort(void)
{
    ospi_recover();
    ospi_backend.error = 1;
    ospi_backend.done  = 1;
}

spi_backend_t ospi_backend = {
    .init  = ospi_backend_init,
    .write = ospi_backend_write,
    .read  = ospi_backend_read,
    .abort = ospi_backend_abort,
    .done  = 1,
    .error = 0,
};

/* ---- HAL callbacks (override the __weak versions) ----------------------
 * If USE_HAL_OSPI_REGISTER_CALLBACKS is 1 in stm32u5xx_hal_conf.h these
 * are never called -- keep it at 0, or register them instead. */

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

#endif /* HAL_OSPI_MODULE_ENABLED */


#ifdef TRANSPORT_ENABLE_BITBANG
/* =======================================================================
 * Bit-bang backend -- no DMA, blocking, manual GPIO toggling. Produces the
 * same byte sequence as the OCTOSPI backend, so the FPGA can't tell the
 * difference (just slower). Only the pin-level helpers are left to fill in.
 * ======================================================================= */

static void bb_cs(uint8_t active)       { (void)active; /* TODO: drive CS low (1) / high (0) */ }
static void bb_bus_output(uint8_t out)  { (void)out;    /* TODO: switch D0..D7 to output (1) / input (0) */ }
static void bb_clock_pulse(void)        {               /* TODO: CLK high, then low */ }
static void bb_put_byte(uint8_t b)      { (void)b;      /* TODO: set D0..D7 = b, then bb_clock_pulse() */ }
static uint8_t bb_get_byte(void)        { /* TODO: bb_clock_pulse(), sample D0..D7 */ return 0; }

static void bb_send_header(const transport_hdr_t *hdr)
{
    bb_put_byte(hdr->opcode);
    if (hdr->has_args) {
        bb_put_byte((uint8_t)(hdr->x >> 8));
        bb_put_byte((uint8_t)(hdr->x));
        bb_put_byte((uint8_t)(hdr->y >> 8));
        bb_put_byte((uint8_t)(hdr->y));
        bb_put_byte((uint8_t)(hdr->len >> 8));
        bb_put_byte((uint8_t)(hdr->len));
    }
}

static HAL_StatusTypeDef bitbang_backend_init(void)
{
    /* Configure the GPIOs here if MX_GPIO_Init() hasn't already. */
    bitbang_backend.error = 0;
    bitbang_backend.done  = 1;
    return HAL_OK;
}

static HAL_StatusTypeDef bitbang_backend_write(const transport_hdr_t *hdr, const uint8_t *data, uint16_t n)
{
    if (hdr == NULL || (n > 0u && data == NULL)) {
        return HAL_ERROR;
    }
    bitbang_backend.error = 0;
    bitbang_backend.done  = 0;

    bb_bus_output(1);
    bb_cs(1);
    bb_send_header(hdr);
    for (uint16_t i = 0; i < n; i++) {
        bb_put_byte(data[i]);
    }
    bb_cs(0);

    bitbang_backend.done = 1;   /* "callback" fires here, synchronously */
    return HAL_OK;
}

static HAL_StatusTypeDef bitbang_backend_read(const transport_hdr_t *hdr, uint8_t *buf, uint16_t n)
{
    if (hdr == NULL || buf == NULL || n == 0u) {
        return HAL_ERROR;
    }
    bitbang_backend.error = 0;
    bitbang_backend.done  = 0;

    bb_bus_output(1);
    bb_cs(1);
    bb_send_header(hdr);
    bb_bus_output(0);                        /* release the bus... */
    for (uint32_t i = 0; i < TRANSPORT_TURNAROUND_CYCLES; i++) {
        bb_clock_pulse();                    /* ...and give the FPGA time to take it */
    }
    for (uint16_t i = 0; i < n; i++) {
        buf[i] = bb_get_byte();
    }
    bb_cs(0);
    bb_bus_output(1);

    bitbang_backend.done = 1;
    return HAL_OK;
}

static void bitbang_backend_abort(void)
{
    bb_cs(0);
    bitbang_backend.error = 1;
    bitbang_backend.done  = 1;
}

spi_backend_t bitbang_backend = {
    .init  = bitbang_backend_init,
    .write = bitbang_backend_write,
    .read  = bitbang_backend_read,
    .abort = bitbang_backend_abort,
    .done  = 1,
    .error = 0,
};

#endif /* TRANSPORT_ENABLE_BITBANG */
