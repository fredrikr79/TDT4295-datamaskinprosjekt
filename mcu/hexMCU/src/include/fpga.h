/* ======================================================================
 * fpga.h - async, queued command layer for the FPGA link.
 *
 * Commands (opcode in the instruction phase, then):
 * ------------------------------------------------------------------------------------
 * |  NAME         | OP | ADDRESS (32b)  | ALT BYTES       | PAYLOAD        | ANSWER (read)
 * ------------------------------------------------------------------------------------
 * | ECHO          | 01 | -              | -               | [B1]..[BN]     | [B1]..[BN]
 * | Send_line     | 02 | [X][Y]         | [N]      (16b)  | pixels         | -
 * | Read_line     | 03 | [X][Y]         | [N]      (16b)  | -              | pixels
 * | Send_HUD_box  | 04 | [X1][Y1]       | [X2][Y2] (32b)  | pixels         | -
 * | Read_HUD_box  | 05 | [X1][Y1]       | [X2][Y2] (32b)  | -              | pixels
 * | Send_SIM_box  | 06 | [X1][Y1]       | [X2][Y2] (32b)  | pixels         | -
 * | Read_SIM_box  | 07 | [X1][Y1]       | [X2][Y2] (32b)  | -              | pixels
 * | Stop_SIM      | 08 | -              | -               | [XXXXXXX0]     | -
 * | Start_SIM     | 09 | -              | -               | [XXXXXXX1]     | -
 * | INFO          | 0A | -              | -               | -              | [W][H][N][SID]
 * ------------------------------------------------------------------------------------
 * Pixels are RGB565, 2 bytes each, MSB first. On the HUD layer 0xF81F
 * (magenta) means "empty": the SIM pixel below shows through.
 *
 * INFO answer, each field 16 bits MSB first (FPGA_INFO_BYTES in total):
 *   W, H  size of the FPGA's screen in pixels
 *   N     size of the FPGA's input FIFO: no command, counting opcode,
 *         address and alt bytes, may be larger than this
 *   SID   session id; a new one means the FPGA restarted
 *
 * Every command is WRITE -> wait for FPGA_READY -> and, if it has an
 * answer, READ (READ_OPCODE, turnaround, then the answer bytes). When
 * READY goes high the FPGA also puts a status for that command on three
 * GPIOs (FPGA_STATUS_*); anything but OK fails the command.
 *
 * How to use it
 * -------------
 *   1. Build a command:      fpga_cmd_t c = fpga_cmd_read_hud_box(box, buf, len);
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
 *      (main loop, never an interrupt). err == FPGA_ERR_NONE: the rx
 *      buffer holds the answer. Either way both buffers are yours again.
 *
 * Buffers belong to the caller and must stay valid (tx: unchanged) from
 * fpga_submit() until done is called -- DMA uses them directly. The
 * command struct itself is copied, so it can live on the stack.
 *
 * When a command FAILS, every command queued before that failure is
 * cancelled (done with FPGA_ERR_CANCELLED): after a timeout or a bad
 * status the MCU no longer knows where the FPGA is, and later commands
 * may depend on the failed one. Resync, then submit again. Commands that
 * callbacks submit while this happens are kept.
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

/* ---- opcodes -------------------------------------------------------- */
#define ECHO_OPCODE             0x01u
#define SEND_LINE_OPCODE        0x02u
#define READ_LINE_OPCODE        0x03u
#define SEND_HUD_BOX_OPCODE     0x04u
#define READ_HUD_BOX_OPCODE     0x05u
#define SEND_SIM_BOX_OPCODE     0x06u
#define READ_SIM_BOX_OPCODE     0x07u
#define STOP_SIM_OPCODE         0x08u
#define START_SIM_OPCODE        0x09u
#define INFO_OPCODE             0x0Au   /* not agreed on yet */

/* The READ step of every command that returns data. The FPGA ignores it
 * (a dummy); it is only there because the HAL and OCTOSPI need one. */
#define READ_OPCODE             0x00u

#define FPGA_INFO_BYTES         8u      /* W, H, N, SID: 2 bytes each */

/* Status GPIOs, valid while READY is high. Only OK is agreed so far. */
#define FPGA_STATUS_OK          0u

/* ---- results -------------------------------------------------------- */
typedef enum {
    FPGA_OK = 0,        /* queued                                      */
    FPGA_EFULL,         /* queue full, try again when there is space   */
    FPGA_EBUSY,         /* an INFO is already queued                   */
    FPGA_EINVAL,        /* bad arguments (NULL buffer, zero length...) */
    FPGA_ETOOBIG,       /* command would not fit in the FPGA's FIFO    */
} fpga_status_t;

typedef enum {
    FPGA_ERR_NONE = 0,
    FPGA_ERR_LINK_BUSY,     /* backend never became free               */
    FPGA_ERR_START,         /* backend refused to start a transfer     */
    FPGA_ERR_XFER,          /* transfer / DMA error                    */
    FPGA_ERR_XFER_TIMEOUT,  /* transfer never completed                */
    FPGA_ERR_NO_READY,      /* no FPGA_READY in time (FPGA not there?) */
    FPGA_ERR_STATUS,        /* FPGA reported an error: fpga_ready_status() */
    FPGA_ERR_MISMATCH,      /* echo came back different; rx holds it   */
    FPGA_ERR_BAD_INFO,      /* INFO answer makes no sense              */
    FPGA_ERR_ABORTED,       /* was running when fpga_abort_all() came  */
    FPGA_ERR_CANCELLED,     /* never ran: an earlier command failed or
                               fpga_abort_all()                        */
} fpga_err_t;

typedef struct { uint16_t x1, y1, x2, y2; } fpga_box_t;

/* What INFO told us (or what the console set by hand). */
typedef struct {
    bool     valid;         /* false until INFO has succeeded          */
    uint16_t width;
    uint16_t height;
    uint16_t max_cmd;       /* N: largest command in bytes, header included */
    uint16_t session;
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
};

/* len is always the buffer size in bytes. n / box are the header fields
 * exactly as the protocol table above has them. */
fpga_cmd_t fpga_cmd_echo(const uint8_t *tx, uint8_t *rx, uint16_t len);

fpga_cmd_t fpga_cmd_send_line(uint16_t x, uint16_t y, uint16_t n,
                              const uint8_t *tiles, uint16_t len);
fpga_cmd_t fpga_cmd_read_line(uint16_t x, uint16_t y, uint16_t n,
                              uint8_t *buf, uint16_t len);

fpga_cmd_t fpga_cmd_send_hud_box(fpga_box_t box, const uint8_t *tiles, uint16_t len);
fpga_cmd_t fpga_cmd_read_hud_box(fpga_box_t box, uint8_t *buf, uint16_t len);
fpga_cmd_t fpga_cmd_send_sim_box(fpga_box_t box, const uint8_t *tiles, uint16_t len);
fpga_cmd_t fpga_cmd_read_sim_box(fpga_box_t box, uint8_t *buf, uint16_t len);

fpga_cmd_t fpga_cmd_sim_run(bool run);      /* Start_SIM / Stop_SIM      */

/* On success fpga_link_info() holds the answer, and every later submit is
 * checked against its N. Uses a buffer inside fpga.c. */
fpga_cmd_t fpga_cmd_info(void);

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

uint8_t       fpga_ready_status(void);  /* status pins at the last READY used */
uint32_t      fpga_ready_used(void);    /* READY edges consumed by commands */

const char   *fpga_err_str(fpga_err_t err);
const char   *fpga_status_str(fpga_status_t st);

#endif /* FPGA_H */