/* clock_gettime / termios are POSIX, not ISO C -- ask for them explicitly
 * so this file builds under -std=c11 as well as -std=gnu11. */
#define _POSIX_C_SOURCE 200809L

/* ======================================================================
 * transport_host.c -- the OCTOSPI link of the host build, behind the same
 * spi_backend_t interface as transport_ospi.c.
 *
 * Only the wires live here:
 *   - transfers take time, proportional to byte count at the configured
 *     clock and lane count
 *   - transfers can fail, at a configurable rate
 *   - the READY line, pulsed when the FPGA says so
 * What the FPGA does with the bytes is fake_fpga.c. A write is handed
 * over when it completes intact; a failed transfer never reaches it.
 *
 * Timing is wall-clock for now. When the virtual clock lands, replace
 * now_ns() with it and drive it from transport_poll() -- nothing above
 * this file changes.
 * ====================================================================== */
#define LOG_TAG "link"

#include "transport.h"
#include "log.h"
#include "host_hooks.h"
#include "cli.h"
#include "fake_fpga.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- tunables (host_hooks.h exposes setters for the CLI / argv) ------- */
static double   cfg_clock_mhz   = 20.0;  /* OCTOSPI clock                  */
static unsigned cfg_lanes       = 8;     /* 8 = octal, 4 = quad, 1 = single*/
static unsigned cfg_error_pct   = 0;     /* chance a transfer fails        */

void host_fpga_set_clock(double mhz, unsigned lanes)
{
    if (mhz > 0.0)  cfg_clock_mhz = mhz;
    if (lanes == 1 || lanes == 4 || lanes == 8) cfg_lanes = lanes;
    LOG_INFO("link: %.1f MHz x%u lanes = %.2f MB/s", cfg_clock_mhz, cfg_lanes,
             cfg_clock_mhz * cfg_lanes / 8.0);
}

void host_fpga_set_error_rate(unsigned percent)
{
    cfg_error_pct = percent > 100 ? 100 : percent;
    LOG_INFO("error injection: %u%%", cfg_error_pct);
}

/* ---- clock ----------------------------------------------------------- */
static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

/* Header is the opcode, then optionally x,y (4) and the alt field (2 or
 * 4), each switched on by its own bit in hdr->fields. Same for both
 * directions; a read adds the turnaround cycles below. */
static uint64_t transfer_ns(const transport_hdr_t *hdr, bool is_read)
{
    uint64_t bytes = 1u                                          /* opcode */
                   + ((hdr->fields & TRANSPORT_F_XY)  ? 4u : 0u) /* x, y   */
                   + transport_alt_bytes(hdr)                    /* alt    */
                   + (uint64_t)hdr->len;                         /* data   */

    /* bits / (MHz * lanes) -> microseconds, then to ns */
    double us = (double)(bytes * 8u) / (cfg_clock_mhz * (double)cfg_lanes);
    uint64_t ns = (uint64_t)(us * 1000.0);

    if (is_read) {
        /* turnaround cycles at the same clock */
        ns += (uint64_t)((double)TRANSPORT_TURNAROUND_CYCLES
                         / cfg_clock_mhz * 1000.0);
    }
    return ns;
}

static bool roll_error(void)
{
    if (cfg_error_pct == 0) return false;
    return (unsigned)(rand() % 100) < cfg_error_pct;
}

/* ---- link state ------------------------------------------------------ */
static spi_backend_t host_backend;

static struct {
    bool            running;     /* a transfer is on the wire              */
    uint64_t        finish_ns;   /* when it completes                      */
    bool            fail;        /* this transfer is going to fail         */
    bool            is_read;
    transport_hdr_t hdr;         /* copy: the caller's may be on its stack */
    const uint8_t  *tx;          /* caller's buffers, valid until done     */
    uint8_t        *rx;

    uint64_t        ready_at_ns; /* 0 = no READY pulse pending             */
    uint8_t         status_next; /* status pins for that pulse             */
    uint8_t         status_pins; /* what the status pins show right now    */
} link;

static transport_status_t host_init(void)
{
    memset(&link, 0, sizeof link);
    host_backend.done  = 1;
    host_backend.error = 0;
    host_ready_pin = false;
    fake_fpga_reset();
    LOG_INFO("link up: %.1f MHz x%u lanes", cfg_clock_mhz, cfg_lanes);
    return TRANSPORT_OK;
}

static transport_status_t start(const transport_hdr_t *hdr, bool is_read,
                                uint8_t *rx, const uint8_t *tx)
{
    if (hdr == NULL)                       return TRANSPORT_ERR;
    if (is_read  && (rx == NULL || hdr->len == 0)) return TRANSPORT_ERR;
    if (!is_read && hdr->len > 0 && tx == NULL)    return TRANSPORT_ERR;
    if (!host_backend.done)                return TRANSPORT_BUSY;

    uint64_t dur = transfer_ns(hdr, is_read);

    link.running   = true;
    link.finish_ns = now_ns() + dur;
    link.fail      = roll_error();
    link.is_read   = is_read;
    link.hdr       = *hdr;
    link.tx        = tx;
    link.rx        = rx;

    host_backend.error = 0;
    host_backend.done  = 0;

    /* Injected failures always show at debug level; the rest is trace. */
    if (link.fail)
        LOG_DBG("%s op=0x%02X len=%u -> %lu ns (will fail)",
                is_read ? "read" : "write", hdr->opcode, hdr->len,
                (unsigned long)dur);
    else
        LOG_TRACE("%s op=0x%02X len=%u -> %lu ns",
                  is_read ? "read" : "write", hdr->opcode, hdr->len,
                  (unsigned long)dur);

    return TRANSPORT_OK;
}

static transport_status_t host_write(const transport_hdr_t *hdr, const uint8_t *data)
{
    return start(hdr, false, NULL, data);
}

static transport_status_t host_read(const transport_hdr_t *hdr, uint8_t *buf)
{
    return start(hdr, true, buf, NULL);
}

static void host_abort(void)
{
    link.running     = false;
    link.ready_at_ns = 0;
    host_backend.error = 1;
    host_backend.done  = 1;
}

static uint8_t host_read_status(void)
{
    return link.status_pins;
}

/* Everything that would be an interrupt on hardware happens here. */
static void host_poll(void)
{
    uint64_t t = now_ns();

    if (link.running && t >= link.finish_ns) {
        link.running = false;
        if (link.fail) {
            link.ready_at_ns   = 0;      /* the FPGA never saw it: no READY */
            host_backend.error = 1;      /* error BEFORE done, same order   */
            host_backend.done  = 1;      /* as the HAL callbacks            */
        } else {
            if (link.is_read) {
                fake_fpga_read(&link.hdr, link.rx);
            } else {
                int32_t us = fake_fpga_write(&link.hdr, link.tx,
                                             &link.status_next);
                link.ready_at_ns = us < 0 ? 0 : t + (uint64_t)us * 1000ull;
            }
            host_backend.done = 1;
        }
    }

    if (link.ready_at_ns != 0 && t >= link.ready_at_ns) {
        link.ready_at_ns = 0;
        link.status_pins = link.status_next; /* stable before READY rises */
        host_ready_pin   = true;         /* level, for the readrdy command */
        transport_ready_isr();           /* edge, same entry as the EXTI   */
    }

    if (!fake_fpga_poll(t))
        cli_quit("window closed");
}

static spi_backend_t host_backend = {
    .init  = host_init,
    .write = host_write,
    .read  = host_read,
    .abort = host_abort,
    .poll  = host_poll,
    .read_status = host_read_status,
    .done  = 1,
    .error = 0,
};

spi_backend_t *const BACKEND = &host_backend;