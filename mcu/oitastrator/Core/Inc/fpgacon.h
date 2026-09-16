#ifndef FPGACON_H
#define FPGACON_H

#include "transport.h"
#include <stdint.h>


/* ---- Protocol constants: must match the FPGA side ------------------------ */
#define FPGA_OP_WRITE_LINE  0x01u  /* args x,y,len + len pixel bytes; FPGA answers ACK/NACK */
#define FPGA_OP_READ_REQ    0x02u  /* args x,y,len, no payload; FPGA prepares len bytes     */
#define FPGA_OP_READBACK    0x03u  /* no args; FPGA drives its pending response             */

#define FPGA_ACK   0xFFu
#define FPGA_NACK  0x00u

/* ---- Tuning --------------------------------------------------------------- */
#define FPGA_MAX_RETRIES       3u   /* resends of a NACKed line before giving up   */
#define FPGA_XFER_TIMEOUT_MS   10u  /* max time one bus transfer may take          */
#define FPGA_READY_TIMEOUT_MS  10u  /* max time between command and the ready IRQ  */

// Booleg padding
#define OSPI_PAD  0x00
#define OSPI_PADING 1

typedef enum {
    FPGA_PENDING = 0,  /* job still running -- poll again next tick           */
    FPGA_OK,           /* finished OK; for reads the buffer is now valid      */
    FPGA_NACK_FAIL,    /* line NACKed FPGA_MAX_RETRIES+1 times in a row       */
    FPGA_TIMEOUT,      /* ready IRQ never came, or a transfer never finished  */
    FPGA_BUS_ERROR,    /* OCTOSPI/DMA error, or the bus refused a transfer    */
    FPGA_NO_JOB        /* check_done() called with nothing started            */
} fpga_result_t;

/* Pick the transport (&ospi_backend or &bitbang_backend) and init it. */
HAL_StatusTypeDef fpga_init(spi_backend_t *chosen_backend);

/* Write len pixel bytes starting at (x, y). Waits for ACK, resends on NACK. */
HAL_StatusTypeDef fpga_write_line(uint16_t x, uint16_t y, uint16_t len, const uint8_t *pixels);

/* Read len bytes starting at (x, y) into dst. */
HAL_StatusTypeDef fpga_read(uint16_t x, uint16_t y, uint16_t len, uint8_t *dst);

/* Read len bytes of line y, starting at x = 0. */
HAL_StatusTypeDef fpga_read_line(uint16_t y, uint16_t len, uint8_t *dst);

/* Advance the state machine. Call once per main-loop tick. */
void fpga_poll(void);

/* Collect the result of the current job (see top of file). */
fpga_result_t fpga_check_done(void);

/* Call from HAL_GPIO_EXTI_Rising_Callback() for the FPGA "ready" pin.
 * (STM32U5 has no HAL_GPIO_EXTI_Callback -- it's split into
 * HAL_GPIO_EXTI_Rising_Callback / HAL_GPIO_EXTI_Falling_Callback.) */
void on_fpga_ready_irq(void);

#endif /* FPGACON_H */
