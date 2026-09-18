#ifndef FPGA_H
#define FPGA_H

#include <stdint.h>
#include <stdbool.h>

/* State of a caller-owned request.
 * The caller creates an fpga_req_t, points buf at its own memory and
 * passes it to one of the public fpga_* calls. While state is PENDING the
 * driver owns buf and the caller must not touch it. */
typedef enum {
    FPGA_REQ_IDLE,      /* free to use / reuse           */
    FPGA_REQ_PENDING,   /* job owns buf, don't touch it  */
    FPGA_REQ_DONE,      /* success, buf holds valid data */
    FPGA_REQ_ERROR,     /* failed, see err               */
} fpga_req_state_t;

typedef enum {
    FPGA_OK = 0,
    FPGA_ERR_BACKEND_BUSY,    /* transport never became free            */
    FPGA_ERR_NOT_READY,       /* READY pin low before a transfer        */
    FPGA_ERR_READY_TIMEOUT,   /* no READY interrupt in time             */
    FPGA_ERR_READY_DROPPED,   /* READY went low while sampling ACK      */
    FPGA_ERR_XFER_START,      /* transfer could not be started          */
    FPGA_ERR_XFER,            /* transfer reported an error             */
    FPGA_ERR_XFER_TIMEOUT,    /* transfer never completed               */
    FPGA_ERR_NACK,            /* FPGA nacked, reason unknown            */
    FPGA_ERR_NACK_CODE,       /* FPGA nacked, reason in req->nack_code  */
} fpga_err_t;

typedef struct {
    uint8_t         *buf;       /* buffer to send from or read into        */
    uint16_t         cap;       /* size of buf (uint16 to match hdr.len)   */
    fpga_req_state_t state;     /* set by driver, reset to IDLE by caller  */
    fpga_err_t       err;       /* valid when state == FPGA_REQ_ERROR      */
    uint8_t          nack_code; /* valid when err == FPGA_ERR_NACK_CODE    */
} fpga_req_t;

/* Call often from the main loop to drive the current job. */
void fpga_poll(void);

/* Returns true if the job was started, false if the driver is busy or
 * the arguments are invalid (e.g. len > req->cap). */
bool fpga_read_line(uint16_t x, uint16_t y, uint16_t len, fpga_req_t *req);

/* Human-readable text for an error code. */
const char *fpga_err_str(fpga_err_t err);

/* Call from the FPGA_READY rising-edge EXTI interrupt handler. */
void on_fpga_ready_irq(void);

#endif /* FPGA_H */