#ifndef FAKE_FPGA_H
#define FAKE_FPGA_H

/* ======================================================================
 * fake_fpga.h - the FPGA side of the host build.
 *
 * transport_host.c models the wires (timing, errors, the READY line) and
 * hands each completed transaction to this file, which models what the
 * FPGA does with it: command FIFO, SIM + HUD pixel layers, the outgoing
 * answer FIFO, and the picture it would put out on VGA.
 * ====================================================================== */

#include <stdbool.h>
#include <stdint.h>
#include "transport.h"

void fake_fpga_reset(void);

/* A write transaction reached the FPGA intact. data holds hdr->len bytes.
 * Returns microseconds until the FPGA pulses READY, or -1 for no READY;
 * *status is what the 3 status pins show while READY is high. */
int32_t fake_fpga_write(const transport_hdr_t *hdr, const uint8_t *data,
                        uint8_t *status);

/* A read transaction: drain hdr->len bytes of the answer FIFO into buf. */
void fake_fpga_read(const transport_hdr_t *hdr, uint8_t *buf);

/* Redraw the window from the layers when they changed (at most 30 Hz).
 * Returns false once the window has been closed. */
bool fake_fpga_poll(uint64_t now_ns);

#endif /* FAKE_FPGA_H */