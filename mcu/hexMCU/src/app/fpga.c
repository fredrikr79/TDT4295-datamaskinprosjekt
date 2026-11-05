/* ======================================================================
 * fpga.c - async, queued command layer for the FPGA link.
 *
 * fpga_submit() copies a command into a ring buffer of FPGA_QUEUE_LEN
 * slots and returns. fpga_poll() (main loop) runs the command at the head
 * as a short list of steps, advancing as the OCTOSPI / DMA hardware
 * finishes each one, then pops it and calls its done callback.
 *
 * Steps:
 *   WRITE     header (+ payload straight from the caller's tx buffer)
 *   WAIT_IRQ  an FPGA_READY edge since the WRITE was started; also checks
 *             the ACK line. On a NACK the rest of the command is replaced
 *             by one READ of the error code the FPGA queued.
 *   READ      READ_OPCODE, turnaround, rx_len bytes into the caller's rx
 *
 * All commands are [WRITE, WAIT_IRQ(, READ)], except the console's raw
 * read, which is a single READ. ECHO compares rx with tx at
 * the end, INFO parses its answer.
 *
 * The sprite commands carry their parameters and their two-byte answer
 * inside the command (param[], answer[]). fpga_submit() points tx / rx at
 * the copy in the queue slot, which stays put until the command is done,
 * so DMA can read and write it there.
 *
 * Once INFO has succeeded, every WRITE is checked against the FPGA's N at
 * submit time and refused with FPGA_ETOOBIG if it would not fit.
 * ====================================================================== */
#define LOG_TAG "fpga"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "log.h"
#include "platform.h"
#include "transport.h"
#include "fpga.h"

#define BACKEND_READY_TIMEOUT_MS  100   /* waiting for backend to be free   */
#define XFER_DONE_TIMEOUT_MS      100   /* waiting for transfer to finish   */
#define FPGA_IRQ_TIMEOUT_MS       100   /* waiting for FPGA_READY interrupt */

typedef enum { STEP_WRITE, STEP_WAIT_IRQ, STEP_READ } step_kind_t;
static const char *const step_names[] = { "write", "wait-irq", "read" };

/* ---- queue: q[q_head] is the running / next command ------------------ */
static fpga_cmd_t q[FPGA_QUEUE_LEN];
static uint8_t    q_head, q_count;

/* ---- the running command --------------------------------------------- */
static struct {
    bool        active;
    step_kind_t steps[3];
    uint8_t     n, idx;
    bool        in_flight;      /* current step's transfer has been started */
    bool        nack;           /* got NACK: the READ step fetches the code */
    uint32_t    t0;             /* start of the current wait                */
    uint32_t    irq_mark;       /* ready count when the last transfer began */
} run;

static bool        inside;          /* in fpga_poll() or an abort: callbacks may run */
static bool        abort_pending;   /* fpga_abort_all() called from a callback */
static uint8_t     last_status;     /* ACK line at the last READY used      */
static uint8_t     fail_rx[1];      /* error code after a NACK              */
static uint8_t     last_fail;       /* ...as fpga_fail_code() reports it    */
static uint32_t    ready_used;      /* READY edges consumed by WAIT_IRQ     */

static fpga_info_t info;
static uint8_t     info_rx[FPGA_INFO_BYTES];

/* ======================================================================
 * Queue
 * ====================================================================== */
static bool has_write(const fpga_cmd_t *c) { return c->kind != FPGA_KIND_RAW_READ; }

static bool has_answer(const fpga_cmd_t *c)
{
    return c->kind == FPGA_KIND_READ || c->kind == FPGA_KIND_ECHO
        || c->kind == FPGA_KIND_INFO;
}

/* Bytes a write puts into the FPGA's input FIFO: opcode, fields, payload. */
static uint32_t wire_bytes(const transport_hdr_t *h)
{
    return 1u + ((h->fields & TRANSPORT_F_XY) ? 4u : 0u)
              + transport_alt_bytes(h) + h->len;
}

static bool info_queued(void)
{
    for (uint8_t i = 0; i < q_count; i++)
        if (q[(q_head + i) % FPGA_QUEUE_LEN].kind == FPGA_KIND_INFO)
            return true;
    return false;
}

fpga_status_t fpga_submit(const fpga_cmd_t *c)
{
    if (c == NULL || c->kind > FPGA_KIND_RAW_READ) return FPGA_EINVAL;

    bool tx_ok = c->tx != NULL || (c->param_tx && c->hdr.len <= FPGA_PARAM_MAX);
    bool rx_ok = c->rx != NULL || (c->answer_rx && c->rx_len <= sizeof c->answer);
    if (has_write(c) && c->hdr.len > 0 && !tx_ok)           return FPGA_EINVAL;
    if (has_answer(c) && (!rx_ok || c->rx_len == 0))        return FPGA_EINVAL;
    if (c->kind == FPGA_KIND_ECHO && (c->hdr.len == 0 || c->rx_len != c->hdr.len))
        return FPGA_EINVAL;
    if (c->kind == FPGA_KIND_RAW_READ && (c->rx == NULL || c->hdr.len == 0))
        return FPGA_EINVAL;

    if (c->kind == FPGA_KIND_INFO && info_queued()) return FPGA_EBUSY;

    if (has_write(c) && info.valid && wire_bytes(&c->hdr) > info.max_cmd) {
        LOG_DBG("op 0x%02X refused: %lu bytes > N=%u", c->hdr.opcode,
                (unsigned long)wire_bytes(&c->hdr), info.max_cmd);
        return FPGA_ETOOBIG;
    }

    if (q_count >= FPGA_QUEUE_LEN) return FPGA_EFULL;

    fpga_cmd_t *slot = &q[(q_head + q_count) % FPGA_QUEUE_LEN];
    *slot = *c;
    if (slot->param_tx)  slot->tx = slot->param;    /* parameters ride along */
    if (slot->answer_rx) slot->rx = slot->answer;   /* the ID lands here     */
    q_count++;
    return FPGA_OK;     /* fpga_poll() starts it */
}

bool    fpga_has_space(void) { return q_count < FPGA_QUEUE_LEN; }
uint8_t fpga_queued(void)    { return q_count; }

/* Pop the head and tell its owner. The copy keeps the callback safe even
 * if it submits into the slot that was just freed. */
static void pop_and_report(fpga_err_t err)
{
    fpga_cmd_t c = q[q_head];
    q_head = (uint8_t)((q_head + 1u) % FPGA_QUEUE_LEN);
    q_count--;
    if (c.done) c.done(&c, err);
}

/* The running command has ended. On failure the commands queued behind it
 * are cancelled too -- but not ones its callbacks submit. */
static void finish(fpga_err_t err)
{
    uint8_t behind = (uint8_t)(q_count - 1u);

    run.active    = false;
    run.in_flight = false;
    if (err != FPGA_ERR_NONE)
        LOG_DBG("op 0x%02X failed at step %u/%u: %s", q[q_head].hdr.opcode,
                run.idx + 1, run.n, fpga_err_str(err));

    pop_and_report(err);
    if (err != FPGA_ERR_NONE)
        while (behind-- && q_count)
            pop_and_report(FPGA_ERR_CANCELLED);
}

static void do_abort(void)
{
    abort_pending = false;
    if (run.active) {
        if (run.in_flight) BACKEND->abort();
        finish(FPGA_ERR_ABORTED);
    } else {
        uint8_t n = q_count;
        while (n-- && q_count) pop_and_report(FPGA_ERR_CANCELLED);
    }
}

void fpga_abort_all(void)
{
    if (inside) { abort_pending = true; return; }   /* from a callback */
    inside = true;
    do_abort();
    inside = false;
}

/* ======================================================================
 * Running the head command
 * ====================================================================== */
static void start_head(void)
{
    const fpga_cmd_t *c = &q[q_head];

    memset(&run, 0, sizeof run);
    if (c->kind == FPGA_KIND_RAW_READ) {
        run.steps[run.n++] = STEP_READ;
    } else {
        run.steps[run.n++] = STEP_WRITE;
        run.steps[run.n++] = STEP_WAIT_IRQ;
        if (has_answer(c)) run.steps[run.n++] = STEP_READ;
    }
    run.t0       = plat_millis();
    run.irq_mark = transport_ready_count();
    run.active   = true;
}

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)((uint32_t) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); }

/* INFO answer: [W][H][N] 16 bits each, [SID] 32 bits, MSB first. */
static fpga_err_t parse_info(void)
{
    fpga_info_t in = {
        .valid   = true,
        .width   = be16(&info_rx[0]),
        .height  = be16(&info_rx[2]),
        .max_cmd = be16(&info_rx[4]),
        .session = be32(&info_rx[6]),
    };
    /* A box header alone is 9 bytes; anything smaller is not usable. */
    if (in.width == 0 || in.height == 0 || in.max_cmd < 9u)
        return FPGA_ERR_BAD_INFO;

    if (info.valid && info.session != in.session)
        LOG_INFO("new FPGA session 0x%08lX (was 0x%08lX)",
                 (unsigned long)in.session, (unsigned long)info.session);
    info = in;
    return FPGA_ERR_NONE;
}

/* All steps done: final checks, then finish. */
static void complete(void)
{
    const fpga_cmd_t *c = &q[q_head];
    fpga_err_t err = FPGA_ERR_NONE;

    if (run.nack) {                     /* the read was the error code */
        last_fail = fail_rx[0];
        LOG_DBG("op 0x%02X: NACK, %s (0x%02X)", c->hdr.opcode,
                fpga_fail_str(last_fail), last_fail);
        finish(FPGA_ERR_STATUS);
        return;
    }

    if (c->kind == FPGA_KIND_ECHO && memcmp(c->tx, c->rx, c->rx_len) != 0)
        err = FPGA_ERR_MISMATCH;
    else if (c->kind == FPGA_KIND_INFO)
        err = parse_info();

    finish(err);
}

static void next_step(void)
{
    run.in_flight = false;
    run.t0        = plat_millis();
    if (++run.idx >= run.n) complete();
}

/* Advance the head command as far as it can go right now. Returns true if
 * something moved (a step or the whole command ended), so the caller
 * should try again straight away. */
static bool step(void)
{
    const fpga_cmd_t *c = &q[q_head];
    step_kind_t kind = run.steps[run.idx];
    uint32_t elapsed = plat_millis() - run.t0;   /* wrap-safe */

    if (kind == STEP_WAIT_IRQ) {
        if (transport_ready_count() != run.irq_mark) {
            ready_used++;
            last_status = transport_ready_status();
            if (last_status != FPGA_STATUS_OK) {
                /* NACK: the FPGA queued an error code instead of any answer.
                 * Read that one byte, then fail the command. */
                run.nack = true;
                run.steps[run.idx + 1] = STEP_READ;
                run.n = (uint8_t)(run.idx + 2);
            }
            next_step();
            return true;
        }
        if (elapsed < FPGA_IRQ_TIMEOUT_MS) return false;
        finish(FPGA_ERR_NO_READY);
        return true;
    }

    if (!run.in_flight) {
        if (!BACKEND->done) {
            if (elapsed < BACKEND_READY_TIMEOUT_MS) return false;
            finish(FPGA_ERR_LINK_BUSY);
            return true;
        }

        /* Take the mark BEFORE starting: a READY that fires during the
         * transfer then still counts for the WAIT_IRQ step after it. */
        run.irq_mark = transport_ready_count();

        transport_status_t st;
        if (kind == STEP_WRITE) {
            st = BACKEND->write(&c->hdr, c->tx);
        } else if (run.nack) {
            transport_hdr_t rh = { .opcode = READ_OPCODE, .len = sizeof fail_rx };
            st = BACKEND->read(&rh, fail_rx);
        } else if (c->kind == FPGA_KIND_RAW_READ) {
            st = BACKEND->read(&c->hdr, c->rx);
        } else {
            transport_hdr_t rh = { .opcode = READ_OPCODE, .len = c->rx_len };
            st = BACKEND->read(&rh, c->rx);   /* backends don't keep hdr */
        }
        LOG_TRACE("op 0x%02X step %u/%u: %s", c->hdr.opcode, run.idx + 1,
                  run.n, step_names[kind]);

        if (st != TRANSPORT_OK) {
            finish(FPGA_ERR_START);
            return true;
        }
        run.in_flight = true;
        run.t0        = plat_millis();
        /* fall through: a very short transfer may already be done */
    }

    if (BACKEND->done) {
        if (BACKEND->error && run.nack) {       /* NACK, code unreadable */
            last_fail = 0;
            finish(FPGA_ERR_STATUS);
        } else if (BACKEND->error) {
            finish(FPGA_ERR_XFER);
        } else {
            next_step();
        }
        return true;
    }
    if (plat_millis() - run.t0 >= XFER_DONE_TIMEOUT_MS) {
        BACKEND->abort();
        finish(FPGA_ERR_XFER_TIMEOUT);
        return true;
    }
    return false;
}

void fpga_poll(void)
{
    if (inside) return;         /* called from a callback */
    inside = true;

    /* Bounded, so a callback that keeps resubmitting a command that fails
     * instantly can't lock up the main loop. */
    for (unsigned budget = 4u * FPGA_QUEUE_LEN; budget; budget--) {
        if (abort_pending) { do_abort(); continue; }
        if (!run.active) {
            if (q_count == 0) break;
            start_head();
        }
        if (!step()) break;
    }

    inside = false;
}

/* ======================================================================
 * Status
 * ====================================================================== */
const fpga_cmd_t *fpga_current(void)  { return run.active ? &q[q_head] : NULL; }

const char *fpga_current_step(void)
{
    return run.active ? step_names[run.steps[run.idx]] : "-";
}

uint32_t fpga_current_ms(void)
{
    return run.active ? plat_millis() - run.t0 : 0u;
}

uint8_t  fpga_ready_status(void) { return last_status; }
uint8_t  fpga_fail_code(void)    { return last_fail; }

const char *fpga_fail_str(uint8_t code)
{
    switch (code) {
    case FPGA_FAIL_UNKNOWN_COMMAND:     return "unknown command";
    case FPGA_FAIL_MISSING_PARAMETER:   return "missing parameter";
    case FPGA_FAIL_TOO_MANY_PARAMETERS: return "too many parameters";
    case FPGA_FAIL_ILLEGAL_VALUE:       return "illegal value";
    case FPGA_FAIL_RUNTIME:             return "runtime error";
    case FPGA_FAIL_MEMORY:              return "memory error";
    case FPGA_FAIL_UNKNOWN_IDENTIFIER:  return "unknown identifier";
    case FPGA_FAIL_UNKNOWN_VALUE:       return "unknown value";
    case FPGA_FAIL_TIMEOUT:             return "timeout";
    case FPGA_FAIL_RESOURCE_EXHAUSTED:  return "resource exhausted";
    case FPGA_FAIL_INVALID_STATE:       return "invalid state";
    case FPGA_FAIL_UNKNOWN:             return "unknown error";
    case 0:                             return "no code";
    default:                            return "?";
    }
}
uint32_t fpga_ready_used(void)   { return ready_used; }

const fpga_info_t *fpga_link_info(void) { return &info; }

void fpga_set_link_info(const fpga_info_t *in) { info = *in; }

uint16_t fpga_max_payload(uint8_t opcode)
{
    if (!info.valid) return UINT16_MAX;

    transport_hdr_t h = { .opcode = opcode };
    switch (opcode) {
    case WRITE_CONS_OPCODE: case READ_CONS_OPCODE:
        h.fields = TRANSPORT_F_XY | TRANSPORT_F_ALT16; break;
    case WRITE_BOX_OPCODE: case READ_BOX_OPCODE:
        h.fields = TRANSPORT_F_XY | TRANSPORT_F_ALT32; break;
    default:
        break;                              /* opcode only (WRITE_SPRITE...) */
    }
    uint32_t hdr = wire_bytes(&h);          /* h.len == 0: header only */
    return info.max_cmd > hdr ? (uint16_t)(info.max_cmd - hdr) : 0u;
}

const char *fpga_err_str(fpga_err_t err)
{
    static const char *const names[] = {
        "ok", "backend not ready (timeout)", "transfer did not start",
        "transfer error", "transfer never completed",
        "no FPGA ready irq (timeout)", "FPGA answered NACK",
        "echo data mismatch", "bad INFO answer", "aborted", "cancelled",
    };
    return (unsigned)err < sizeof names / sizeof names[0] ? names[err] : "?";
}

const char *fpga_status_str(fpga_status_t st)
{
    static const char *const names[] = {
        "ok", "queue full", "INFO already queued", "bad arguments",
        "too big for the FPGA's FIFO (N)",
    };
    return (unsigned)st < sizeof names / sizeof names[0] ? names[st] : "?";
}

/* ======================================================================
 * Builders
 * ====================================================================== */
fpga_cmd_t fpga_cmd_echo(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
    /* No N field: the FPGA takes the opcode, then drains its FIFO until
     * empty, so the payload length needs no header. */
    return (fpga_cmd_t){ .hdr  = { .opcode = ECHO_OPCODE, .len = len },
                         .tx = tx, .rx = rx, .rx_len = len,
                         .kind = FPGA_KIND_ECHO };
}

/* ---- grid memory ------------------------------------------------------ */
fpga_cmd_t fpga_cmd_write_cons(uint16_t x, uint16_t y, const uint8_t *data, uint16_t n)
{
    return (fpga_cmd_t){ .hdr = { .opcode = WRITE_CONS_OPCODE,
                                  .fields = TRANSPORT_F_XY | TRANSPORT_F_ALT16,
                                  .x = x, .y = y, .alt = n, .len = n },
                         .tx = data, .kind = FPGA_KIND_WRITE };
}

fpga_cmd_t fpga_cmd_read_cons(uint16_t x, uint16_t y, uint8_t *buf, uint16_t n)
{
    return (fpga_cmd_t){ .hdr = { .opcode = READ_CONS_OPCODE,
                                  .fields = TRANSPORT_F_XY | TRANSPORT_F_ALT16,
                                  .x = x, .y = y, .alt = n },
                         .rx = buf, .rx_len = n, .kind = FPGA_KIND_READ };
}

/* Box header: X,Y in the address phase, W,H in the 32-bit alt bytes. */
static transport_hdr_t box_hdr(uint8_t op, fpga_box_t b, uint16_t len)
{
    return (transport_hdr_t){ .opcode = op,
                              .fields = TRANSPORT_F_XY | TRANSPORT_F_ALT32,
                              .x = b.x1, .y = b.y1,
                              .alt = ((uint32_t)b.x2 << 16) | b.y2,
                              .len = len };
}

fpga_cmd_t fpga_cmd_write_box(fpga_box_t b, const uint8_t *pixels, uint16_t len)
{
    return (fpga_cmd_t){ .hdr = box_hdr(WRITE_BOX_OPCODE, b, len),
                         .tx = pixels, .kind = FPGA_KIND_WRITE };
}

fpga_cmd_t fpga_cmd_read_box(fpga_box_t b, uint8_t *buf, uint16_t len)
{
    return (fpga_cmd_t){ .hdr = box_hdr(READ_BOX_OPCODE, b, 0),
                         .rx = buf, .rx_len = len, .kind = FPGA_KIND_READ };
}

/* ---- sprites -------------------------------------------------------------
 * Parameters are the payload, in spec order, carried in c.param. */
static uint8_t *put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
    return p + 2;
}

fpga_cmd_t fpga_cmd_write_sprite(const uint8_t *pixels, uint16_t len)
{
    /* No size field: the FPGA takes the opcode, then the whole payload as
     * the image, like ECHO. Answers the memID. */
    return (fpga_cmd_t){ .hdr = { .opcode = WRITE_SPRITE_OPCODE, .len = len },
                         .tx = pixels, .rx_len = 2, .answer_rx = true,
                         .kind = FPGA_KIND_READ };
}

fpga_cmd_t fpga_cmd_sprite_write(uint16_t mem_id, bool active, uint16_t x, uint16_t y,
                                 uint16_t w, uint16_t h)
{
    fpga_cmd_t c = { .hdr = { .opcode = SPRITE_WRITE_OPCODE, .len = 11 },
                     .param_tx = true, .rx_len = 2, .answer_rx = true,
                     .kind = FPGA_KIND_READ };
    uint8_t *p = put16(c.param, mem_id);
    *p++ = active ? 1u : 0u;
    p = put16(p, x);
    p = put16(p, y);
    p = put16(p, w);
    put16(p, h);
    return c;
}

fpga_cmd_t fpga_cmd_sprite_update(uint16_t sprite_id, bool active, uint16_t x, uint16_t y)
{
    fpga_cmd_t c = { .hdr = { .opcode = SPRITE_UPDATE_OPCODE, .len = 7 },
                     .param_tx = true, .kind = FPGA_KIND_WRITE };
    uint8_t *p = put16(c.param, sprite_id);
    *p++ = active ? 1u : 0u;
    p = put16(p, x);
    put16(p, y);
    return c;
}

fpga_cmd_t fpga_cmd_sprite_delete(uint16_t sprite_id)
{
    fpga_cmd_t c = { .hdr = { .opcode = SPRITE_DELETE_OPCODE, .len = 2 },
                     .param_tx = true, .kind = FPGA_KIND_WRITE };
    put16(c.param, sprite_id);
    return c;
}

fpga_cmd_t fpga_cmd_sim_run(bool run_it)
{
    /* Opcode only: which one says start or stop. */
    return (fpga_cmd_t){ .hdr = { .opcode = run_it ? START_SIM_OPCODE
                                                   : STOP_SIM_OPCODE },
                         .kind = FPGA_KIND_WRITE };
}

fpga_cmd_t fpga_cmd_info(void)
{
    return (fpga_cmd_t){ .hdr = { .opcode = INFO_OPCODE },
                         .rx = info_rx, .rx_len = sizeof info_rx,
                         .kind = FPGA_KIND_INFO };
}

fpga_cmd_t fpga_cmd_raw_write(transport_hdr_t hdr, const uint8_t *data)
{
    return (fpga_cmd_t){ .hdr = hdr, .tx = data, .kind = FPGA_KIND_WRITE };
}

fpga_cmd_t fpga_cmd_raw_read(transport_hdr_t hdr, uint8_t *buf)
{
    return (fpga_cmd_t){ .hdr = hdr, .rx = buf, .rx_len = hdr.len,
                         .kind = FPGA_KIND_RAW_READ };
}