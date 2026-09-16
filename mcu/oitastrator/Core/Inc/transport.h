#ifndef TRANSPORT_H
#define TRANSPORT_H

#include "stm32u5xx_hal.h"
#include <stdint.h>

/*
 * Layer 1: moves bytes between MCU and FPGA. Knows nothing about what the
 * bytes mean -- that's fpgacon.c's job.
 *
 * Each backend is wrapped in a compile-time guard so a backend with no
 * hardware behind it compiles out entirely:
 *
 * - HAL_OSPI_MODULE_ENABLED is defined by CubeMX (stm32u5xx_hal_conf.h)
 *   whenever OCTOSPI1 is enabled in the .ioc.
 * - TRANSPORT_ENABLE_BITBANG has no CubeMX equivalent, so it's a project
 *   toggle -- define it here or (better) as a compiler define.
 */
// #define TRANSPORT_ENABLE_BITBANG

/*
 * ---- Wire format (one transaction = one CS-low period) --------------------
 *
 * Every phase uses all 8 data lines, single data rate, 1 byte per clock:
 *
 *   write:  [opcode] [x_hi x_lo y_hi y_lo len_hi len_lo] [payload ...]
 *   read:   [opcode] [x_hi x_lo y_hi y_lo len_hi len_lo] [turnaround] [FPGA drives n bytes]
 *                     \_____ only if has_args != 0 _____/
 *
 * Inside the OCTOSPI backend this is mapped onto the peripheral's
 * instruction / address / alternate-bytes / data phases, but on the wire it
 * is just this byte sequence (big-endian) -- the FPGA only has to count
 * bytes after CS falls.
 *
 * TRANSPORT_TURNAROUND_CYCLES: clocks where nobody drives the bus, so the
 * FPGA can switch its IOs from input to output before it starts sending.
 * Must match the FPGA implementation.
 */
#define TRANSPORT_TURNAROUND_CYCLES  4u

typedef struct {
    uint8_t  opcode;
    uint8_t  has_args;   /* 0 = opcode only, 1 = opcode + x, y, len */
    uint16_t x;
    uint16_t y;
    uint16_t len;        /* the "len" field sent to the FPGA (not necessarily n) */
} transport_hdr_t;

typedef struct {
    HAL_StatusTypeDef (*init)(void);

    /* Send header, then n payload bytes from data (n may be 0, data may then
     * be NULL). Non-blocking: returns once the transfer has STARTED.
     * data must stay valid and unchanged until done == 1. */
    HAL_StatusTypeDef (*write)(const transport_hdr_t *hdr, const uint8_t *data, uint16_t n);

    /* Send header, wait TRANSPORT_TURNAROUND_CYCLES, then clock n bytes
     * (n >= 1) into buf. Non-blocking, same buffer rule as write(). */
    HAL_StatusTypeDef (*read)(const transport_hdr_t *hdr, uint8_t *buf, uint16_t n);

    /* Kill an in-flight transfer (used on timeouts). Afterwards done == 1
     * and error == 1. */
    void (*abort)(void);

    /* The link is half duplex -- at most ONE transfer is in flight, so one
     * pair of flags covers both directions.
     *   done  == 1 : nothing in flight (idle, finished, or failed)
     *   error == 1 : the last transfer failed. Only meaningful when done == 1.
     * Set from interrupt context, read from the main loop. */
    volatile uint8_t done;
    volatile uint8_t error;
} spi_backend_t;

#ifdef HAL_OSPI_MODULE_ENABLED
extern spi_backend_t ospi_backend;
#endif

#ifdef TRANSPORT_ENABLE_BITBANG
extern spi_backend_t bitbang_backend;
#endif

/* Calls the chosen backend's init() through the common interface. */
HAL_StatusTypeDef transport_init(spi_backend_t *backend);

#endif /* TRANSPORT_H */
