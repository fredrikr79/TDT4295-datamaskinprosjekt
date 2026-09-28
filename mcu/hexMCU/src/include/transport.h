#ifndef TRANSPORT_H
#define TRANSPORT_H

/* ======================================================================
 * transport.h -- link to the FPGA, platform independent.
 *
 * No stm32 headers here: this is included by app/ code that both builds
 * compile. Exactly one backend is linked per build and it defines BACKEND.
 * ====================================================================== */

#include <stdint.h>
#include <stdbool.h>

/* Clock cycles between the header and the data phase of a read, so the
 * bus can be handed over to the FPGA. */
#define TRANSPORT_TURNAROUND_CYCLES  4u

/* Wire format, same header layout in both directions:
 *   write: opcode, [x y], [len_field], payload...
 *   read : opcode, [x y], [len_field], [turnaround], data... */

typedef enum {
    TRANSPORT_OK = 0,
    TRANSPORT_BUSY,     /* a transfer is already in flight */
    TRANSPORT_ERR,      /* bad arguments, or the transfer refused to start */
} transport_status_t;

/* Optional header fields, OR'ed into transport_hdr_t.fields.
 * Wire order is fixed by OCTOSPI: opcode, [x y], [len_field], [payload]. */
#define TRANSPORT_F_XY   0x01u   /* x, y in the address phase (4 bytes)      */
#define TRANSPORT_F_LEN  0x02u   /* len_field in the alt-bytes phase (2 bytes) */

typedef struct {
    uint8_t  opcode;
    uint8_t  fields;     /* TRANSPORT_F_* bitmask, 0 = opcode only */
    uint16_t x;
    uint16_t y;
    uint16_t len_field;  /* value sent as the len field (TRANSPORT_F_LEN).
                            Independent of len: "send op x y 0" sends a
                            len field with no payload behind it. */
    uint16_t len;        /* write: payload bytes after the header
                            read : bytes clocked in after the turnaround */
} transport_hdr_t;

typedef struct {
    transport_status_t (*init)(void);

    /* Send opcode + the fields selected in hdr->fields, then hdr->len
     * payload bytes from data (len may be 0,
     * data may then be NULL). Non-blocking: returns once the transfer has
     * STARTED. data must stay valid and unchanged until done == 1. */
    transport_status_t (*write)(const transport_hdr_t *hdr, const uint8_t *data);

    /* Send opcode + the fields selected in hdr->fields, wait
     * TRANSPORT_TURNAROUND_CYCLES, then clock hdr->len bytes (>= 1) into buf. Non-blocking, same buffer rule as write(). */
    transport_status_t (*read)(const transport_hdr_t *hdr, uint8_t *buf);

    /* Kill an in-flight transfer (used on timeouts). Afterwards done == 1
     * and error == 1. */
    void (*abort)(void);

    /* Called from the main loop. NULL on hardware, where transfers are
     * driven by DMA and interrupts. The host backend uses it to advance
     * its simulated timing. */
    void (*poll)(void);

    /* The link is half duplex -- at most ONE transfer is in flight, so one
     * pair of flags covers both directions.
     *   done  == 1 : nothing in flight (idle, finished, or failed)
     *   error == 1 : the last transfer failed. Only meaningful when done == 1.
     * Set from interrupt context, read from the main loop. */
    volatile uint8_t done;
    volatile uint8_t error;
} spi_backend_t;

/* The active backend. Defined once per build, in the linked backend file. */
extern spi_backend_t *const BACKEND;

/* Bring the backend up. Call once, before cli_init(). */
transport_status_t transport_init(void);

/* Give the backend main-loop time. Cheap no-op on hardware. */
void transport_poll(void);

/* ----------------------------------------------------------------------
 * FPGA_READY edge counter.
 *
 * Bumped from the EXTI ISR on hardware and from the fake FPGA on the host,
 * so everything above this layer is identical on both. The main loop only
 * ever compares counts -- it never has to catch an edge live.
 * -------------------------------------------------------------------- */
void     transport_ready_isr(void);     /* ISR / fake FPGA -> counter */
uint32_t transport_ready_count(void);   /* main loop only */

#endif /* TRANSPORT_H */