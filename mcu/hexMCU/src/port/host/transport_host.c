/* clock_gettime / termios are POSIX, not ISO C -- ask for them explicitly
 * so this file builds under -std=c11 as well as -std=gnu11. */
#define _POSIX_C_SOURCE 200809L

/* ======================================================================
 * transport_host.c -- fake FPGA behind the same spi_backend_t interface.
 *
 * Models the parts of the link that can actually go wrong:
 *   - transfers take time, proportional to byte count at the configured
 *     clock and lane count
 *   - the FPGA answers a command with a READY pulse some time later
 *   - transfers can fail, at a configurable rate
 *
 * Timing is wall-clock for now. When the virtual clock lands, replace
 * now_ns() with it and drive it from transport_poll() -- nothing above
 * this file changes.
 * ====================================================================== */
#define LOG_TAG "fake"

#include "transport.h"
#include "log.h"
#include "host_hooks.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- tunables (host_hooks.h exposes setters for the CLI / argv) ------- */
static double   cfg_clock_mhz   = 20.0;  /* OCTOSPI clock                  */
static unsigned cfg_lanes       = 8;     /* 8 = octal, 4 = quad, 1 = single*/
static unsigned cfg_ready_us    = 200;   /* command -> READY pulse delay   */
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

/* Header is the opcode, then optionally x,y (4) and the len field (2),
 * each switched on by its own bit in hdr->fields. Same for both
 * directions; a read adds the turnaround cycles below. */
static uint64_t transfer_ns(const transport_hdr_t *hdr, bool is_read)
{
    uint64_t bytes = 1u                                          /* opcode */
                   + ((hdr->fields & TRANSPORT_F_XY)  ? 4u : 0u) /* x, y   */
                   + ((hdr->fields & TRANSPORT_F_LEN) ? 2u : 0u) /* len f. */
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

/* ---- fake FPGA state ------------------------------------------------- */
typedef enum { XF_IDLE, XF_RUNNING } xfer_state_t;

static spi_backend_t host_backend;

static struct {
    xfer_state_t state;
    uint64_t     finish_ns;      /* when the current transfer completes    */
    bool         fail;           /* this transfer is going to fail         */
    bool         is_read;
    uint8_t     *rx_buf;         /* caller's buffer, filled on completion  */
    uint16_t     rx_len;

    uint64_t     ready_at_ns;    /* 0 = no READY pulse pending             */
    uint8_t      last_opcode;    /* what a following read echoes back      */
} fpga;

/* What the fake FPGA hands back on a read: the opcode that was last
 * written, then a counting pattern. Recognisable in the hex dump, and it
 * proves the write actually reached the "FPGA" before the read. */
static void fill_read_data(uint8_t *buf, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++)
        buf[i] = (uint8_t)(fpga.last_opcode + i);
}

static transport_status_t host_init(void)
{
    memset(&fpga, 0, sizeof fpga);
    host_backend.done  = 1;
    host_backend.error = 0;
    host_ready_pin = false;
    LOG_INFO("fake fpga up: %.1f MHz x%u lanes, ready delay %u us",
             cfg_clock_mhz, cfg_lanes, cfg_ready_us);
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

    fpga.state     = XF_RUNNING;
    fpga.finish_ns = now_ns() + dur;
    fpga.fail      = roll_error();
    fpga.is_read   = is_read;
    fpga.rx_buf    = rx;
    fpga.rx_len    = hdr->len;

    host_backend.error = 0;
    host_backend.done  = 0;

    if (!is_read) {
        fpga.last_opcode = hdr->opcode;
        /* The FPGA "processes" the command and pulses READY afterwards. */
        fpga.ready_at_ns = fpga.finish_ns + (uint64_t)cfg_ready_us * 1000ull;
    }

    LOG_DBG("%s op=0x%02X len=%u -> %lu ns%s",
            is_read ? "read" : "write", hdr->opcode, hdr->len,
            (unsigned long)dur, fpga.fail ? " (will fail)" : "");

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
    fpga.state       = XF_IDLE;
    fpga.ready_at_ns = 0;
    host_backend.error = 1;
    host_backend.done  = 1;
}

/* Everything that would be an interrupt on hardware happens here. */
static void host_poll(void)
{
    uint64_t t = now_ns();

    if (fpga.state == XF_RUNNING && t >= fpga.finish_ns) {
        fpga.state = XF_IDLE;
        if (fpga.fail) {
            fpga.ready_at_ns   = 0;      /* a failed command gets no READY */
            host_backend.error = 1;      /* error BEFORE done, same order  */
            host_backend.done  = 1;      /* as the HAL callbacks           */
        } else {
            if (fpga.is_read && fpga.rx_buf)
                fill_read_data(fpga.rx_buf, fpga.rx_len);
            host_backend.done = 1;
        }
    }

    if (fpga.ready_at_ns != 0 && t >= fpga.ready_at_ns) {
        fpga.ready_at_ns = 0;
        host_ready_pin   = true;         /* level, for the readrdy command */
        transport_ready_isr();           /* edge, same entry as the EXTI   */
    }
}

static spi_backend_t host_backend = {
    .init  = host_init,
    .write = host_write,
    .read  = host_read,
    .abort = host_abort,
    .poll  = host_poll,
    .done  = 1,
    .error = 0,
};

spi_backend_t *const BACKEND = &host_backend;
