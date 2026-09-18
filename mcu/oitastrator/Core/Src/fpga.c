#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "main.h"
#include "transport.h"
#include "fpga.h"

/* ------------------------------------------------------------------ */
/* Configuration                                                       */
/* ------------------------------------------------------------------ */

#define BACKEND_READY_TIMEOUT_MS  100   /* waiting for backend + READY pin  */
#define XFER_DONE_TIMEOUT_MS      100   /* waiting for transfer to finish   */
#define FPGA_IRQ_TIMEOUT_MS       1000  /* waiting for FPGA_READY interrupt */

#define MAX_STEPS 8

/* Opcodes */
#define OP_READ_LINE_REQ  0x55
#define OP_READ_DATA      0xA5
#define OP_READ_STATUS    0xA6   /* TODO: placeholder, match FPGA protocol */

/* Pins: name them FPGA_READY and FPGA_ACK in CubeMX to get these in main.h */
#if !defined(FPGA_READY_Pin) || !defined(FPGA_READY_GPIO_Port)
#error "Define FPGA_READY_Pin / FPGA_READY_GPIO_Port (e.g. via CubeMX pin label)"
#endif
#if !defined(FPGA_ACK_Pin) || !defined(FPGA_ACK_GPIO_Port)
#error "Define FPGA_ACK_Pin / FPGA_ACK_GPIO_Port (e.g. via CubeMX pin label)"
#endif

#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------------------ */
/* FPGA ready interrupt and pins                                       */
/* ------------------------------------------------------------------ */

static volatile uint32_t ready_count;   /* written by ISR only */

void on_fpga_ready_irq(void)
{
    ready_count++;
}

static inline bool ready_high(void)
{
    return HAL_GPIO_ReadPin(FPGA_READY_GPIO_Port, FPGA_READY_Pin) == GPIO_PIN_SET;
}

static inline bool ack_high(void)
{
    return HAL_GPIO_ReadPin(FPGA_ACK_GPIO_Port, FPGA_ACK_Pin) == GPIO_PIN_SET;
}

/* ------------------------------------------------------------------ */
/* Steps                                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    STEP_WRITE,        /* send hdr + buf                                  */
    STEP_READ,         /* read hdr.len bytes into buf                     */
    STEP_WAIT_READY,   /* wait for READY, ignore ACK                      */
    STEP_CHECK_ACK,    /* wait for READY, fail on NACK                    */
    STEP_QUERY_ACK,    /* wait for READY, on NACK read 1 status byte      */
} step_kind_t;

typedef struct {
    step_kind_t kind;
    union {
        transport_hdr_t hdr;        /* STEP_WRITE / STEP_READ       */
        uint32_t        timeout_ms; /* STEP_WAIT_READY / *_ACK      */
    };
} step_t;

static inline bool is_transfer(step_kind_t k)
{
    return k == STEP_WRITE || k == STEP_READ;
}

/* Step builders. Header fields not named are zero. */
#define STEP_WRITE_(op, ...)  { .kind = STEP_WRITE,      .hdr = { .opcode = (op), __VA_ARGS__ } }
#define STEP_READ_(op, ...)   { .kind = STEP_READ,       .hdr = { .opcode = (op), __VA_ARGS__ } }
#define STEP_READY_(ms)       { .kind = STEP_WAIT_READY, .timeout_ms = (ms) }
#define STEP_ACK_(ms)         { .kind = STEP_CHECK_ACK,  .timeout_ms = (ms) }
#define STEP_QUERY_ACK_(ms)   { .kind = STEP_QUERY_ACK,  .timeout_ms = (ms) }

/* ------------------------------------------------------------------ */
/* Job state machine                                                   */
/* ------------------------------------------------------------------ */

typedef enum { JOB_IDLE, JOB_STEP_START, JOB_STEP_BUSY } job_state_t;

static struct {
    job_state_t  state;
    const char  *name;       /* job name, handy for debugging            */
    step_t       steps[MAX_STEPS];
    uint8_t      n_steps;
    uint8_t      idx;
    uint32_t     t0;         /* start time of the current phase          */
    uint32_t     irq_mark;   /* ready_count when the last transfer began  */
    fpga_req_t  *req;        /* caller-owned request (buffer + status)    */
    bool         querying;   /* busy transfer is a NACK status read       */
    uint8_t      status_byte;/* target for the NACK status read           */
} job;

static void job_finish(fpga_err_t err)
{
    job.state    = JOB_IDLE;
    job.querying = false;
    if (job.req) {
        job.req->err   = err;
        job.req->state = (err == FPGA_OK) ? FPGA_REQ_DONE : FPGA_REQ_ERROR;
        job.req        = NULL;
    }
}

static void job_next_step(void)
{
    if (++job.idx >= job.n_steps) {
        job_finish(FPGA_OK);
    } else {
        job.state = JOB_STEP_START;
        job.t0    = HAL_GetTick();
    }
}

/* Starts a transfer. Returns false if the backend refused it. */
static bool start_transfer(step_kind_t kind, const transport_hdr_t *hdr, uint8_t *buf)
{
    /* Take the mark BEFORE starting: an IRQ that fires during the
     * transfer then still counts for a following wait/ack step. */
    job.irq_mark = ready_count;

    HAL_StatusTypeDef st = (kind == STEP_WRITE)
        ? BACKEND->write(hdr, buf)
        : BACKEND->read(hdr, buf);
    if (st != HAL_OK)
        return false;

    job.state = JOB_STEP_BUSY;
    job.t0    = HAL_GetTick();
    return true;
}

/* READY rose: sample ACK while READY is high and act on the step kind. */
static void handle_ready(const step_t *s)
{
    if (s->kind == STEP_WAIT_READY) {
        job_next_step();
        return;
    }

    /* ACK is only valid while READY is high: check before and after. */
    if (!ready_high()) { job_finish(FPGA_ERR_READY_DROPPED); return; }
    bool ack = ack_high();
    if (!ready_high()) { job_finish(FPGA_ERR_READY_DROPPED); return; }

    if (ack) {
        job_next_step();
        return;
    }

    if (s->kind == STEP_CHECK_ACK || !BACKEND->done) {
        job_finish(FPGA_ERR_NACK);
        return;
    }

    /* STEP_QUERY_ACK: ask the FPGA for a one-byte reason. */
    const transport_hdr_t hdr = { .opcode = OP_READ_STATUS, .len = 1, .has_args = 1 };
    job.querying = true;
    if (!start_transfer(STEP_READ, &hdr, &job.status_byte))
        job_finish(FPGA_ERR_NACK);
}

static void job_poll_once(void)
{
    const step_t *s = &job.steps[job.idx];
    uint32_t elapsed = HAL_GetTick() - job.t0;   /* wrap-safe */

    if (job.state == JOB_STEP_START) {
        if (!is_transfer(s->kind)) {
            if (ready_count != job.irq_mark)
                handle_ready(s);
            else if (elapsed >= s->timeout_ms)
                job_finish(FPGA_ERR_READY_TIMEOUT);
            return;
        }

        /* Transfers need a free backend and an FPGA that says it is ready. */
        if (!BACKEND->done || !ready_high()) {
            if (elapsed >= BACKEND_READY_TIMEOUT_MS)
                job_finish(!BACKEND->done ? FPGA_ERR_BACKEND_BUSY
                                          : FPGA_ERR_NOT_READY);
            return;
        }

        if (!start_transfer(s->kind, &s->hdr, job.req->buf))
            job_finish(FPGA_ERR_XFER_START);
        return;
    }

    /* JOB_STEP_BUSY */
    if (BACKEND->done) {
        bool ok = !BACKEND->error;
        if (job.querying) {
            if (ok) {
                job.req->nack_code = job.status_byte;
                job_finish(FPGA_ERR_NACK_CODE);
            } else {
                job_finish(FPGA_ERR_NACK);
            }
        } else if (ok) {
            job_next_step();
        } else {
            job_finish(FPGA_ERR_XFER);
        }
    } else if (elapsed >= XFER_DONE_TIMEOUT_MS) {
        BACKEND->abort();
        job_finish(job.querying ? FPGA_ERR_NACK : FPGA_ERR_XFER_TIMEOUT);
    }
}

/* Keep running while steps complete instantly (e.g. READY already seen),
 * stop as soon as a step waits or a transfer is started. */
static void job_poll(void)
{
    uint8_t prev_idx;
    do {
        if (job.state == JOB_IDLE) return;
        prev_idx = job.idx;
        job_poll_once();
    } while (job.state == JOB_STEP_START && job.idx != prev_idx);
}

static bool job_start(const char *name, const step_t *steps, uint8_t n,
                      fpga_req_t *req)
{
    if (job.state != JOB_IDLE || !req || !req->buf || n == 0 || n > MAX_STEPS)
        return false;

    /* Reject jobs that would overflow the caller's buffer. */
    for (uint8_t i = 0; i < n; i++)
        if (is_transfer(steps[i].kind) && steps[i].hdr.len > req->cap)
            return false;

    job.name     = name;
    memcpy(job.steps, steps, n * sizeof *steps);
    job.n_steps  = n;
    job.idx      = 0;
    job.t0       = HAL_GetTick();
    job.irq_mark = ready_count;
    job.req      = req;
    job.querying = false;
    job.state    = JOB_STEP_START;

    req->state     = FPGA_REQ_PENDING;
    req->err       = FPGA_OK;
    req->nack_code = 0;

    job_poll();   /* try immediately */
    return true;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void fpga_poll(void)
{
    job_poll();
}

const char *fpga_err_str(fpga_err_t err)
{
    switch (err) {
    case FPGA_OK:                return "ok";
    case FPGA_ERR_BACKEND_BUSY:  return "backend not ready (timeout)";
    case FPGA_ERR_NOT_READY:     return "FPGA READY low before transfer";
    case FPGA_ERR_READY_TIMEOUT: return "no FPGA ready irq (timeout)";
    case FPGA_ERR_READY_DROPPED: return "READY dropped while reading ACK";
    case FPGA_ERR_XFER_START:    return "transfer did not start";
    case FPGA_ERR_XFER:          return "transfer error";
    case FPGA_ERR_XFER_TIMEOUT:  return "transfer never completed";
    case FPGA_ERR_NACK:          return "FPGA nack (unknown reason)";
    case FPGA_ERR_NACK_CODE:     return "FPGA nack (see nack_code)";
    }
    return "unknown error";
}

bool fpga_read_line(uint16_t x, uint16_t y, uint16_t len, fpga_req_t *req)
{
    const step_t steps[] = {
        STEP_WRITE_(OP_READ_LINE_REQ, .x = x, .y = y, .len = len, .has_args = 1),
        STEP_ACK_(FPGA_IRQ_TIMEOUT_MS),
        STEP_READ_(OP_READ_DATA, .len = len, .has_args = 1),
        STEP_ACK_(FPGA_IRQ_TIMEOUT_MS),
    };
    return job_start("read line", steps, ARRAY_SIZE(steps), req);
}

bool fpga_write_line(uint16_t x, uint16_t y, uint16_t len, fpga_req_t *req)
{
    const step_t steps[] = {
        STEP_WRITE_(OP_READ_LINE_REQ, .x = x, .y = y, .len = len, .has_args = 1),
        STEP_ACK_(FPGA_IRQ_TIMEOUT_MS),
    };
    return job_start("read line", steps, ARRAY_SIZE(steps), req);
}