#include "fpgacon.h"
#include <stddef.h>

/*
 * Every job has the same shape on the bus:
 *
 *   1. SEND     write the command (+ payload for write_line)
 *   2. WAIT     FPGA raises the ready GPIO when its response is prepared
 *   3. READBACK read the response (1 ACK/NACK byte, or len data bytes)
 *   4. DONE     result parked until the main loop calls fpga_check_done()
 *
 * The only difference between jobs is where the response goes and what
 * happens with it, so that's decided by `job`, not by extra states.
 */

typedef enum {
    ST_IDLE,
    ST_SENDING,     /* command handed to backend, waiting for backend->done */
    ST_WAIT_READY,  /* command sent, waiting for the FPGA ready IRQ         */
    ST_READING,     /* readback handed to backend, waiting for done         */
    ST_DONE,        /* finished, `result` waiting to be collected            */
} fpga_state_t;

typedef enum {
    JOB_NONE,
    JOB_WRITE_LINE,  /* response = 1 ACK/NACK byte, handled internally */
    JOB_READ,        /* response = len bytes into the caller's buffer   */
} fpga_job_t;

static spi_backend_t   *backend = NULL;
static fpga_state_t     state   = ST_IDLE;
static fpga_result_t    result  = FPGA_NO_JOB;
static uint32_t         state_t0;              /* HAL_GetTick() when state was entered */
static volatile uint8_t fpga_ready = 0;        /* set by the EXTI interrupt */

/* The current job */
static fpga_job_t       job = JOB_NONE;
static transport_hdr_t  job_hdr;               /* command header (opcode, x, y, len) */
static const uint8_t   *job_tx;                /* write_line: caller's pixels        */
static uint8_t         *job_rx;                /* read: caller's destination buffer  */
static uint8_t          retries_left;
static uint8_t          ack_byte;              /* DMA target for the ACK/NACK byte   */

/* ---- small helpers ------------------------------------------------------ */

static void enter(fpga_state_t s)
{
    state    = s;
    state_t0 = HAL_GetTick();
}

static uint8_t timed_out(uint32_t limit_ms)
{
    return (HAL_GetTick() - state_t0) > limit_ms;   /* wrap-safe unsigned math */
}

static void finish(fpga_result_t r)
{
    result = r;
    state  = ST_DONE;
}

/* Step 1: send the command. Used for the first attempt and for retries. */
static HAL_StatusTypeDef start_command(void)
{
    HAL_StatusTypeDef st;

    /* Clear BEFORE the FPGA can possibly see the command. If we cleared
     * it later (e.g. when entering ST_WAIT_READY), a fast FPGA could
     * raise ready while the TX-complete IRQ is still pending, and we'd
     * wipe out its answer. */
    fpga_ready = 0;

    if (job == JOB_WRITE_LINE) {
        st = backend->write(&job_hdr, job_tx, job_hdr.len);
    } else {
        st = backend->write(&job_hdr, NULL, 0);    /* READ_REQ has no payload */
    }

    if (st == HAL_OK) {
        enter(ST_SENDING);
    }
    return st;
}

/* Step 3: read the response into the right place for this job. */
static void start_readback(void)
{
    const transport_hdr_t rb = { .opcode = FPGA_OP_READBACK, .has_args = 0 };
    uint8_t  *dst = (job == JOB_WRITE_LINE) ? &ack_byte : job_rx;
    uint16_t  n   = (job == JOB_WRITE_LINE) ? 1u        : job_hdr.len;

    if (backend->read(&rb, dst, n) != HAL_OK) {
        finish(FPGA_BUS_ERROR);
        return;
    }
    enter(ST_READING);
}

/* Step 3 finished: decide what the response means. */
static void handle_response(void)
{
    if (job == JOB_READ) {
        finish(FPGA_OK);    /* data is in the caller's buffer */
        return;
    }

    /* JOB_WRITE_LINE. Anything that isn't exactly ACK (NACK or a garbled
     * byte) counts as "didn't arrive intact" and gets retried. */
    if (ack_byte == FPGA_ACK) {
        finish(FPGA_OK);
    } else if (retries_left > 0u) {
        retries_left--;
        if (start_command() != HAL_OK) {
            finish(FPGA_BUS_ERROR);
        }
    } else {
        finish(FPGA_NACK_FAIL);
    }
}

/* Common front half of every API call. */
static HAL_StatusTypeDef start_job(fpga_job_t kind, uint8_t opcode,
                                   uint16_t x, uint16_t y, uint16_t len)
{
    job              = kind;
    job_hdr.opcode   = opcode;
    job_hdr.has_args = 1;
    job_hdr.x        = x;
    job_hdr.y        = y;
    job_hdr.len      = len;
    retries_left     = FPGA_MAX_RETRIES;

    if (start_command() != HAL_OK) {
        job   = JOB_NONE;
        state = ST_IDLE;    /* nothing started: caller gets HAL_ERROR, no result to collect */
        return HAL_ERROR;
    }
    return HAL_OK;
}

static HAL_StatusTypeDef check_can_start(uint16_t len, const void *buf)
{
    if (backend == NULL || buf == NULL || len == 0u) {
        return HAL_ERROR;
    }
    if (state != ST_IDLE) {
        return HAL_BUSY;    /* also covers ST_DONE: collect the old result first */
    }
    return HAL_OK;
}

/* ---- public API --------------------------------------------------------- */

HAL_StatusTypeDef fpga_init(spi_backend_t *chosen_backend)
{
    if (chosen_backend == NULL) {
        return HAL_ERROR;
    }
    backend = chosen_backend;
    state   = ST_IDLE;
    job     = JOB_NONE;
    return transport_init(backend);
}

HAL_StatusTypeDef fpga_write_line(uint16_t x, uint16_t y, uint16_t len, const uint8_t *pixels)
{
    HAL_StatusTypeDef st = check_can_start(len, pixels);
    if (st != HAL_OK) {
        return st;
    }
    job_tx = pixels;
    return start_job(JOB_WRITE_LINE, FPGA_OP_WRITE_LINE, x, y, len);
}

HAL_StatusTypeDef fpga_read(uint16_t x, uint16_t y, uint16_t len, uint8_t *dst)
{
    HAL_StatusTypeDef st = check_can_start(len, dst);
    if (st != HAL_OK) {
        return st;
    }
    job_rx = dst;
    return start_job(JOB_READ, FPGA_OP_READ_REQ, x, y, len);
}

HAL_StatusTypeDef fpga_read_line(uint16_t y, uint16_t len, uint8_t *dst)
{
    return fpga_read(0u, y, len, dst);
}

void fpga_poll(void)
{
    fpga_state_t before;

    if (backend == NULL) {
        return;
    }

    /* Keep stepping while progress is being made, so e.g. "TX done" and
     * "ready already arrived" are both handled in the same tick instead
     * of costing one main-loop round each. Ends as soon as a step has to
     * wait for hardware, so this can't spin. */
    do {
        before = state;

        switch (state) {
        case ST_SENDING:
            if (!backend->done) {
                if (timed_out(FPGA_XFER_TIMEOUT_MS)) {
                    backend->abort();
                    finish(FPGA_TIMEOUT);
                }
            } else if (backend->error) {
                finish(FPGA_BUS_ERROR);
            } else {
                enter(ST_WAIT_READY);
            }
            break;

        case ST_WAIT_READY:
            if (fpga_ready) {
                fpga_ready = 0;
                start_readback();
            } else if (timed_out(FPGA_READY_TIMEOUT_MS)) {
                finish(FPGA_TIMEOUT);
            }
            break;

        case ST_READING:
            if (!backend->done) {
                if (timed_out(FPGA_XFER_TIMEOUT_MS)) {
                    backend->abort();
                    finish(FPGA_TIMEOUT);
                }
            } else if (backend->error) {
                finish(FPGA_BUS_ERROR);
            } else {
                handle_response();
            }
            break;

        case ST_IDLE:
        case ST_DONE:
        default:
            break;
        }
    } while (state != before && state != ST_DONE);
}

fpga_result_t fpga_check_done(void)
{
    switch (state) {
    case ST_DONE:
        state = ST_IDLE;    /* result collected -> ready for the next job */
        job   = JOB_NONE;
        return result;
    case ST_IDLE:
        return FPGA_NO_JOB;
    default:
        return FPGA_PENDING;
    }
}

void on_fpga_ready_irq(void)
{
    fpga_ready = 1;
}
