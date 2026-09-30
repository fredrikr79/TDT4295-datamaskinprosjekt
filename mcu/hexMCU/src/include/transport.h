#ifndef TRANSPORT_H
#define TRANSPORT_H

/* ======================================================================
 * transport.h - link to the FPGA, platform independent.
 * ====================================================================== */

#include <stdint.h>
#include <stdbool.h>

/* Clock cycles between the header and the data phase of a read, so the
 * bus can be handed over to the FPGA. */
#define TRANSPORT_TURNAROUND_CYCLES  4u

typedef enum {
    TRANSPORT_OK = 0,
    TRANSPORT_BUSY,     /* a transfer is already in flight */
    TRANSPORT_ERR,      /* bad arguments, or the transfer refused to start */
} transport_status_t;

/* Optional header fields, OR'ed into transport_hdr_t.fields.
 * Wire order is fixed by OCTOSPI: opcode, [x y], [alt], [payload].
 * Set at most one of the ALT flags (ALT32 wins if both are set). */
#define TRANSPORT_F_XY     0x01u   /* x, y in the address phase (4 bytes)    */
#define TRANSPORT_F_ALT16  0x02u   /* low 16 bits of alt, alt-bytes phase    */
#define TRANSPORT_F_ALT32  0x04u   /* all 32 bits of alt, alt-bytes phase    */

typedef struct {
    uint8_t  opcode;
    uint8_t  fields;     /* TRANSPORT_F_* bitmask, 0 = opcode only */
    uint16_t x;
    uint16_t y;
    uint32_t alt;        /* alternate-bytes value, MSB first on the wire.
                            ALT16: a single field, e.g. N for a line.
                            ALT32: two fields packed hi:lo, e.g. x2:y2 for
                            a box. Independent of len: a header can carry
                            an N field with no payload behind it. */
    uint16_t len;        /* write: payload bytes after the header
                            read : bytes clocked in after the turnaround */
} transport_hdr_t;

/* Bytes the alt-bytes phase takes on the wire: 0, 2 or 4. */
static inline unsigned transport_alt_bytes(const transport_hdr_t *hdr)
{
    if (hdr->fields & TRANSPORT_F_ALT32) return 4u;
    if (hdr->fields & TRANSPORT_F_ALT16) return 2u;
    return 0u;
}

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

    /* Read the FPGA's 3 status pins (stable while READY is high).
     * Called from transport_ready_isr(). NULL = always 0 (OK). */
    uint8_t (*read_status)(void);

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
 * FPGA_READY edge counter + status latch.
 *
 * Bumped from the EXTI ISR on hardware and from the fake FPGA on the host,
 * so everything above this layer is identical on both. The main loop only
 * ever compares counts -- it never has to catch an edge live.
 *
 * On each edge the ISR also latches the 3 status pins, so the status of
 * the command that READY answers is still there when the main loop looks.
 * -------------------------------------------------------------------- */
#define TRANSPORT_STATUS_MASK  0x07u

void     transport_ready_isr(void);     /* ISR / fake FPGA -> counter */
uint32_t transport_ready_count(void);   /* main loop only */
uint8_t  transport_ready_status(void);  /* status at the latest READY */

/* ----------------------------------------------------------------------
 * Per-transfer trace: one debug line for every transfer and every step.
 * Far too chatty to leave on (the game makes hundreds of transfers a
 * second), so it is off by default and switched with the console's
 * `trace` command. Needs log level 4 (debug) as well.
 * -------------------------------------------------------------------- */
void transport_set_trace(bool on);
bool transport_trace_on(void);

#define LOG_TRACE(...) \
    do { if (transport_trace_on()) LOG_DBG(__VA_ARGS__); } while (0)

#endif /* TRANSPORT_H */