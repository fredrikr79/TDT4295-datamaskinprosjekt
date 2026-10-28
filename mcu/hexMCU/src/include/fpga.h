/* ======================================================================
 * fpga.h - async, queued command layer for the FPGA link.
 *
 * The protocol is docs/mcu_fpga_spi_api.md (the FPGA team's spec). On the
 * wire every command is one byte stream: opcode, then the parameters in
 * the order the spec lists them, then any payload. OCTOSPI carries the
 * first parameters in its address / alternate-bytes phases where they fit
 * (x,y and N or W,H), the rest in the data phase -- the FPGA can't tell
 * the difference, it just sees the bytes in order.
 *
 * ------------------------------------------------------------------------------------------
 * | NAME          | OP | PARAMETERS (all multi-byte fields MSB first)  | PAYLOAD  | ANSWER
 * ------------------------------------------------------------------------------------------
 * | ECHO          | 10 | -                                             | B1..BN   | B1..BN
 * | INFO          | 20 | -                                             | -        | W H N SID
 * | WRITE_CONS    | 30 | X 2B, Y 2B, N 2B                              | N bytes  | -
 * | READ_CONS     | 40 | X 2B, Y 2B, N 2B                              | -        | N bytes
 * | WRITE_BOX     | 50 | X 2B, Y 2B, W 2B, H 2B                        | pixels   | -
 * | READ_BOX      | 60 | X 2B, Y 2B, W 2B, H 2B                        | -        | pixels
 * | WRITE_SPRITE  | 70 | -                                             | pixels   | memID 2B
 * | SPRITE_WRITE  | 80 | memID 2B, active, X 2B, Y 2B, W 2B, H 2B      | -        | spriteID 2B
 * | SPRITE_UPDATE | 90 | spriteID 2B, active, X 2B, Y 2B               | -        | -
 * | SPRITE_DELETE | A0 | spriteID 2B                                   | -        | -
 * | START_SIM     | B0 | -                                             | -        | -
 * | STOP_SIM      | C0 | -                                             | -        | -
 * ------------------------------------------------------------------------------------------
 * CONS and BOX work on the grid (the SIM layer). The HUD is sprites:
 * WRITE_SPRITE uploads an image once and gives its memID; SPRITE_WRITE
 * places an instance of it and gives its spriteID; SPRITE_UPDATE moves or
 * hides it (its image and W,H are fixed); SPRITE_DELETE frees it.
 *
 * Sprite memory is append-only: every WRITE_SPRITE goes right after the
 * last one and takes one memID, and nothing can be changed or freed
 * without resetting the FPGA. An image bigger than one command is several
 * WRITE_SPRITEs in a row; SPRITE_WRITE then names the first memID and the
 * whole W x H. memIDs are few (see the spec), spriteIDs many.
 *
 * Boxes: X,Y is the top-left pixel, W the pixels per row, H the rows; the
 * pixels are one stream, row by row, the last one at (X+W-1, Y+H-1). Build
 * boxes with fpga_box(). Pixels are RGB565, 2 bytes each, MSB first.
 * 0xF81F (magenta) is see-through in sprites.
 *
 * INFO answer, MSB first (FPGA_INFO_BYTES): W, H, N 16 bits, SID 32 bits.
 *   W, H  screen size in pixels
 *   N     the FPGA's input buffer: no command, opcode and parameters
 *         included, may be larger than this
 *   SID   session id; a new one means the FPGA restarted and its memory
 *         (grid, sprite images and instances) is gone
 *
 * Every command is WRITE -> wait for FPGA_READY -> and, if it has an
 * answer, READ (READ_OPCODE, turnaround, then the answer bytes). When
 * READY goes high the FPGA's ACK line says whether the command worked.
 * Low (NACK): the FPGA has flushed its FIFOs and put a one-byte error code
 * (fpga_fail_code_t) in the answer FIFO instead; fpga.c reads it, and the
 * command fails with FPGA_ERR_STATUS -- fpga_fail_code() tells why. What a
 * failed write left in memory is not known, so write it again.
 *
 * How to use it
 * -------------
 *   1. Build a command:      fpga_cmd_t c = fpga_cmd_read_box(box, buf, len);
 *      optionally add:       c.done = my_done;  c.ctx = whatever;
 *   2. Queue it:             fpga_submit(&c)
 *        FPGA_OK       queued; it will run and then call c.done
 *        FPGA_EFULL    the queue is full: wait until fpga_has_space(),
 *                      then submit the same c again
 *        FPGA_EINVAL   bad arguments, nothing queued
 *        FPGA_ETOOBIG  bigger than the FPGA's N, nothing queued. Split
 *                      writes with fpga_max_payload(); this is only the
 *                      safety net.
 *        FPGA_EBUSY    an INFO is already queued (they share one buffer)
 *   3. Call fpga_poll() from the main loop. Commands run in the order
 *      they were submitted, one at a time.
 *   4. When a command ends, c.done(cmd, err) is called from fpga_poll()
 *      (main loop, never an interrupt). err == FPGA_ERR_NONE: the answer
 *      is there -- in your rx buffer, or for the commands that answer with
 *      an ID, in fpga_answer_id(cmd).
 *
 * Buffers belong to the caller and must stay valid (tx: unchanged) from
 * fpga_submit() until done is called -- DMA uses them directly. The
 * command struct itself is copied, so it can live on the stack; the sprite
 * commands' parameters travel inside it.
 *
 * When a command FAILS, every command queued before that failure is
 * cancelled (done with FPGA_ERR_CANCELLED): after a timeout or a NACK the
 * MCU no longer knows where the FPGA is, and later commands may depend on
 * the failed one. Resync, then submit again. Commands that callbacks
 * submit while this happens are kept.
 *
 * Callbacks may call fpga_submit() and fpga_abort_all(), but not
 * fpga_poll().
 * ====================================================================== */
#ifndef FPGA_H
#define FPGA_H

#include <stdint.h>
#include <stdbool.h>
#include "transport.h"      /* transport_hdr_t */

/* Commands the queue holds, the running one included. */
#ifndef FPGA_QUEUE_LEN
#define FPGA_QUEUE_LEN          10u
#endif

/* ---- opcodes (spec opcode in the high nibble) ------------------------ */
#define ECHO_OPCODE             0x10u
#define INFO_OPCODE             0x20u
#define WRITE_CONS_OPCODE       0x30u
#define READ_CONS_OPCODE        0x40u
#define WRITE_BOX_OPCODE        0x50u
#define READ_BOX_OPCODE         0x60u
#define WRITE_SPRITE_OPCODE     0x70u
#define SPRITE_WRITE_OPCODE     0x80u
#define SPRITE_UPDATE_OPCODE    0x90u
#define SPRITE_DELETE_OPCODE    0xA0u
#define START_SIM_OPCODE        0xB0u
#define STOP_SIM_OPCODE         0xC0u

/* The READ step of every command that returns data. The FPGA ignores it
 * (a dummy); it is only there because the HAL and OCTOSPI need one. */
#define READ_OPCODE             0x00u

#define FPGA_INFO_BYTES         10u     /* W, H, N: 2 bytes each, SID 4 bytes */

/* Sprite parameters travel inside the command (see struct fpga_cmd). */
#define FPGA_PARAM_MAX          11u     /* SPRITE_WRITE's, the longest */

/* ACK line at READY, as transport_ready_status() reports it. */
#define FPGA_STATUS_OK          0u      /* ACK high: the command worked */
#define FPGA_STATUS_NACK        1u      /* ACK low: it failed           */

/* The error code the FPGA sends after a NACK (fpga_fail_code()). */
typedef enum {
    FPGA_FAIL_UNKNOWN_COMMAND     = 0xE0,
    FPGA_FAIL_MISSING_PARAMETER   = 0xE1,
    FPGA_FAIL_TOO_MANY_PARAMETERS = 0xE2,
    FPGA_FAIL_ILLEGAL_VALUE       = 0xE3,
    FPGA_FAIL_RUNTIME             = 0xE4,
    FPGA_FAIL_MEMORY              = 0xE5,
    FPGA_FAIL_UNKNOWN_IDENTIFIER  = 0xE6,
    FPGA_FAIL_UNKNOWN_VALUE       = 0xE7,
    FPGA_FAIL_TIMEOUT             = 0xE8,
    FPGA_FAIL_RESOURCE_EXHAUSTED  = 0xE9,
    FPGA_FAIL_INVALID_STATE       = 0xEA,
    FPGA_FAIL_UNKNOWN             = 0xEF,
} fpga_fail_code_t;

/* ---- results -------------------------------------------------------- */
typedef enum {
    FPGA_OK = 0,        /* queued                                      */
    FPGA_EFULL,         /* queue full, try again when there is space   */
    FPGA_EBUSY,         /* an INFO is already queued                   */
    FPGA_EINVAL,        /* bad arguments (NULL buffer, zero length...) */
    FPGA_ETOOBIG,       /* command would not fit in the FPGA's buffer  */
} fpga_status_t;

typedef enum {
    FPGA_ERR_NONE = 0,
    FPGA_ERR_LINK_BUSY,     /* backend never became free               */
    FPGA_ERR_START,         /* backend refused to start a transfer     */
    FPGA_ERR_XFER,          /* transfer / DMA error                    */
    FPGA_ERR_XFER_TIMEOUT,  /* transfer never completed                */
    FPGA_ERR_NO_READY,      /* no FPGA_READY in time (FPGA not there?) */
    FPGA_ERR_STATUS,        /* FPGA answered NACK on the ACK line      */
    FPGA_ERR_MISMATCH,      /* echo came back different; rx holds it   */
    FPGA_ERR_BAD_INFO,      /* INFO answer makes no sense              */
    FPGA_ERR_ABORTED,       /* was running when fpga_abort_all() came  */
    FPGA_ERR_CANCELLED,     /* never ran: an earlier command failed or
                               fpga_abort_all()                        */
} fpga_err_t;

/* A box on the wire: x1,y1 = top-left pixel, x2 = W (pixels per row),
 * y2 = H (rows) -- see the table above. Make one with fpga_box(). */
typedef struct { uint16_t x1, y1, x2, y2; } fpga_box_t;

/* The box w x h pixels with its top-left corner at x, y. */
static inline fpga_box_t fpga_box(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    return (fpga_box_t){ .x1 = x, .y1 = y, .x2 = w, .y2 = h };
}

/* What INFO told us (or what the console set by hand). */
typedef struct {
    bool     valid;         /* false until INFO has succeeded          */
    uint16_t width;
    uint16_t height;
    uint16_t max_cmd;       /* N: largest command in bytes, header included */
    uint32_t session;
} fpga_info_t;

/* ---- commands --------------------------------------------------------
 * Build with an fpga_cmd_*() function, then set done/ctx if you want a
 * callback. Leave the other fields as the builder set them. */
typedef struct fpga_cmd fpga_cmd_t;

/* Called from fpga_poll() when the command has ended. cmd is a copy of
 * what was submitted (ctx included); it is gone after the call returns. */
typedef void (*fpga_done_fn)(const fpga_cmd_t *cmd, fpga_err_t err);

enum {                      /* fpga_cmd_t.kind, set by the builders      */
    FPGA_KIND_WRITE,        /* WRITE -> READY                            */
    FPGA_KIND_READ,         /* WRITE -> READY -> READ answer into rx     */
    FPGA_KIND_ECHO,         /* like READ, then compare rx with tx        */
    FPGA_KIND_INFO,         /* like READ, then parse into fpga_link_info */
    FPGA_KIND_RAW_READ,     /* one bare READ with hdr as given (console) */
};

struct fpga_cmd {
    transport_hdr_t hdr;    /* header of the WRITE (RAW_READ: of the READ) */
    const uint8_t  *tx;     /* payload, hdr.len bytes, or NULL           */
    uint8_t        *rx;     /* answer buffer, or NULL                    */
    uint16_t        rx_len; /* answer bytes                              */
    uint8_t         kind;
    fpga_done_fn    done;   /* optional                                  */
    void           *ctx;    /* optional, for done                        */

    /* Carried inside the command, for the sprite commands: their
     * parameters are the payload (tx points here once queued), and their
     * two-byte answer lands here (fpga_answer_id()). */
    bool            param_tx;               /* tx = param            */
    bool            answer_rx;              /* rx = answer           */
    uint8_t         param[FPGA_PARAM_MAX];
    uint8_t         answer[2];
};

/* The memID (WRITE_SPRITE) or spriteID (SPRITE_WRITE) a command got back.
 * Valid in its done callback when err == FPGA_ERR_NONE. */
static inline uint16_t fpga_answer_id(const fpga_cmd_t *cmd)
{
    return (uint16_t)((cmd->answer[0] << 8) | cmd->answer[1]);     /* MSB first */
}

/* len is always the buffer size in bytes. */
fpga_cmd_t fpga_cmd_echo(const uint8_t *tx, uint8_t *rx, uint16_t len);

/* On success fpga_link_info() holds the answer, and every later submit is
 * checked against its N. Uses a buffer inside fpga.c. */
fpga_cmd_t fpga_cmd_info(void);

/* Grid memory: n bytes from grid position (x, y) on, or a box. */
fpga_cmd_t fpga_cmd_write_cons(uint16_t x, uint16_t y, const uint8_t *data, uint16_t n);
fpga_cmd_t fpga_cmd_read_cons(uint16_t x, uint16_t y, uint8_t *buf, uint16_t n);
fpga_cmd_t fpga_cmd_write_box(fpga_box_t box, const uint8_t *pixels, uint16_t len);
fpga_cmd_t fpga_cmd_read_box(fpga_box_t box, uint8_t *buf, uint16_t len);

/* Sprites. write_sprite uploads an image (w*h*2 bytes of pixels, at most
 * fpga_max_payload(WRITE_SPRITE_OPCODE)) and answers its memID;
 * sprite_write places an instance and answers its spriteID. Read either
 * with fpga_answer_id(cmd) in the done callback. */
fpga_cmd_t fpga_cmd_write_sprite(const uint8_t *pixels, uint16_t len);
fpga_cmd_t fpga_cmd_sprite_write(uint16_t mem_id, bool active, uint16_t x, uint16_t y,
                                 uint16_t w, uint16_t h);
fpga_cmd_t fpga_cmd_sprite_update(uint16_t sprite_id, bool active, uint16_t x, uint16_t y);
fpga_cmd_t fpga_cmd_sprite_delete(uint16_t sprite_id);

fpga_cmd_t fpga_cmd_sim_run(bool run);      /* START_SIM / STOP_SIM      */

/* Debug console only, hdr exactly as given. raw_write still waits for
 * READY -- every command gets one, and a READY nobody waits for would be
 * taken by the next command as its own. raw_read is one bare READ. */
fpga_cmd_t fpga_cmd_raw_write(transport_hdr_t hdr, const uint8_t *data);
fpga_cmd_t fpga_cmd_raw_read(transport_hdr_t hdr, uint8_t *buf);

/* ---- queue ---------------------------------------------------------- */
fpga_status_t fpga_submit(const fpga_cmd_t *cmd);
void          fpga_poll(void);          /* main loop, as often as possible */
bool          fpga_has_space(void);     /* can fpga_submit() take one more? */
uint8_t       fpga_queued(void);        /* commands waiting + running      */
void          fpga_abort_all(void);     /* abort the running one, cancel the rest */

/* The running command, or NULL. For debug output. */
const fpga_cmd_t *fpga_current(void);
const char   *fpga_current_step(void);  /* "write", "wait-irq", "read"     */
uint32_t      fpga_current_ms(void);    /* time spent in that step         */

/* ---- link facts ----------------------------------------------------- */
const fpga_info_t *fpga_link_info(void);
void          fpga_set_link_info(const fpga_info_t *info);  /* debug / tests */

/* Most payload (tx bytes) one command with this opcode may carry, header
 * already subtracted. UINT16_MAX while N is not known yet. */
uint16_t      fpga_max_payload(uint8_t opcode);

uint8_t       fpga_ready_status(void);  /* ACK status at the last READY used */

/* The error code that came with the last NACK; 0 if it couldn't be read. */
uint8_t       fpga_fail_code(void);
const char   *fpga_fail_str(uint8_t code);
uint32_t      fpga_ready_used(void);    /* READY edges consumed by commands */

const char   *fpga_err_str(fpga_err_t err);
const char   *fpga_status_str(fpga_status_t st);

#endif /* FPGA_H */