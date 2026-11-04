/* ======================================================================
 * probe.c - the handshake with the FPGA (see probe.h).
 *
 *   WAIT --(READY steady, retry time reached)--> ECHO --ok--> INFO --ok--> DONE
 *    ^                                            |             |
 *    +------------------ failed ------------------+-------------+
 *
 * Why wait for READY first: before the first byte of the ECHO goes out,
 * READY must have been high, with no stray edges, for READY_QUIET_MS.
 * Otherwise an FPGA still booting (or a noisy line) could catch the
 * command halfway through and take some byte in the middle as the opcode.
 * Edges taken by commands (fpga_ready_used()) don't count, only stray
 * ones. Two stray edges less than READY_QUIET_MS apart is a READY storm:
 * logged once when it starts and once when it ends, and shown on the
 * status line. Normal commands don't wait for this (fpga.c only checks
 * that READY is high before each transfer).
 * ====================================================================== */
#define LOG_TAG "probe"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "probe.h"
#include "fpga.h"
#include "transport.h"
#include "platform.h"
#include "log.h"
#include "cli.h"

#define PROBE_LEN         100u  /* ECHO bytes                                 */
#define PROBE_RETRY_MS    200u  /* between tries                              */
#define READY_QUIET_MS    10u   /* READY high and no stray edges this long    */
#define STATUS_EVERY_MS   100u  /* status line redrawn at most this often     */
#define READY_LOW_SHOW_MS 100u  /* READY low this long: say so in the status  */

typedef enum { P_WAIT, P_ECHO, P_INFO, P_INFO_WAIT, P_DONE } phase_t;

static struct {
    phase_t    ph;              /* starts as P_WAIT                       */
    uint32_t   retry_at;        /* P_WAIT: don't send before this         */
    bool       retry_wait;      /* retry_at is in use                     */
    uint32_t   tries;           /* failed tries in a row                  */
    fpga_err_t err;             /* why the last one failed                */
    const char *what;           /* "probe" or "INFO": which one failed    */
    uint8_t    pattern;         /* changes every try                      */
} p;

static uint8_t probe_tx[PROBE_LEN], probe_rx[PROBE_LEN];

/* ---- READY: steady before we send ----------------------------------- */
static struct {
    bool     init;              /* the fields below are set up           */
    uint32_t spare;             /* stray edges counted so far            */
    uint32_t edge_at;           /* when a stray edge was last seen       */
    uint32_t low_at;            /* when READY was last seen low          */
    uint32_t high_at;           /* when READY was last seen high         */
    bool     storm;
    uint32_t storm_edges, storm_from;
} rdy;

static uint32_t stray_ready_count(void)
{
    return transport_ready_count() - fpga_ready_used();
}

/* true: READY has been high, with no stray edges, for READY_QUIET_MS. */
static bool ready_quiet(uint32_t now)
{
    uint32_t c = stray_ready_count();
    bool high = plat_fpga_ready_pin();
    if (!rdy.init) {                    /* watch it for READY_QUIET_MS first */
        rdy.init = true;
        rdy.spare = c;
        rdy.edge_at = rdy.low_at = rdy.high_at = now;
    }
    if (c != rdy.spare) {
        uint32_t n = c - rdy.spare;
        rdy.spare = c;
        if (n > 1 || now - rdy.edge_at < READY_QUIET_MS) {
            if (!rdy.storm) {
                rdy.storm = true;
                rdy.storm_edges = 0;
                rdy.storm_from = now;
                LOG_WARN("READY storm (edges < %u ms apart): waiting for it to stop",
                         READY_QUIET_MS);
            }
            rdy.storm_edges += n;
        }
        rdy.edge_at = now;
    }
    if (high) rdy.high_at = now;
    else      rdy.low_at  = now;

    bool quiet = high && now - rdy.edge_at >= READY_QUIET_MS && now - rdy.low_at >= READY_QUIET_MS;
    if (quiet && rdy.storm) {
        rdy.storm = false;
        LOG_INFO("READY storm over: %lu edges in %lu ms", (unsigned long)rdy.storm_edges,
                 (unsigned long)(rdy.edge_at - rdy.storm_from));
    }
    return quiet;
}

/* ---- the status line in front of the console prompt ------------------- */
static void show_status(uint32_t now, bool at_once)
{
    static uint32_t shown_at;
    if (!at_once && now - shown_at < STATUS_EVERY_MS) return;
    shown_at = now;

    char msg[64];
    if (rdy.storm)
        snprintf(msg, sizeof msg, "READY storm: %lu edges, waiting", (unsigned long)rdy.storm_edges);
    else if (rdy.init && now - rdy.high_at >= READY_LOW_SHOW_MS)
        snprintf(msg, sizeof msg, "waiting for FPGA READY high (%lu ms)",
                 (unsigned long)(now - rdy.high_at));
    else if (p.tries)
        snprintf(msg, sizeof msg, "probing FPGA: x%lu (%s: %s)", (unsigned long)p.tries,
                 p.what, fpga_err_str(p.err));
    else
        msg[0] = '\0';
    cli_status(msg);
}

/* ---- the commands ---------------------------------------------------- */
static void try_failed(const char *what, fpga_err_t err)
{
    p.tries++;
    p.what = what;
    p.err  = err;
    p.retry_at   = plat_millis() + PROBE_RETRY_MS;
    p.retry_wait = true;
    p.ph = P_WAIT;
    show_status(plat_millis(), true);   /* one line, updated in place */
}

static void echo_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    (void)cmd;
    if (p.ph != P_ECHO) return;         /* probe_start() came in between */
    if (err != FPGA_ERR_NONE) { try_failed("echo", err); return; }
    p.ph = P_INFO;
}

static void info_done(const fpga_cmd_t *cmd, fpga_err_t err)
{
    (void)cmd;
    if (p.ph != P_INFO_WAIT) return;
    if (err != FPGA_ERR_NONE) { try_failed("INFO", err); return; }

    cli_status(NULL);
    if (p.tries)
        LOG_INFO("FPGA answered after %lu failed tr%s", (unsigned long)p.tries,
                 p.tries == 1 ? "y" : "ies");
    const fpga_info_t *in = fpga_link_info();
    LOG_INFO("fpga: %ux%u, N=%u, session 0x%08lX",
             in->width, in->height, in->max_cmd, (unsigned long)in->session);
    p.tries = 0;
    p.ph = P_DONE;
}

/* ---- public ---------------------------------------------------------- */
void probe_start(void)
{
    p.ph = P_WAIT;
    p.retry_wait = false;
    rdy.init = false;                   /* READY must be steady again first */
}

probe_status_t probe_poll(void)
{
    uint32_t now = plat_millis();

    switch (p.ph) {
    case P_WAIT: {
        bool quiet = ready_quiet(now);  /* tracked while waiting for retry_at too */
        show_status(now, false);
        if (!quiet || (p.retry_wait && (int32_t)(now - p.retry_at) < 0)) break;
        p.pattern++;
        for (unsigned i = 0; i < PROBE_LEN; i++)
            probe_tx[i] = (uint8_t)(i * 37u + p.pattern * 101u);   /* new pattern each try */
        fpga_cmd_t c = fpga_cmd_echo(probe_tx, probe_rx, PROBE_LEN);
        c.done = echo_done;
        if (fpga_submit(&c) == FPGA_OK) p.ph = P_ECHO;
        break;
    }
    case P_INFO: {
        fpga_cmd_t c = fpga_cmd_info();
        c.done = info_done;
        if (fpga_submit(&c) == FPGA_OK) p.ph = P_INFO_WAIT;
        break;                          /* full / INFO already queued: next poll */
    }
    case P_ECHO:
    case P_INFO_WAIT:
        break;                          /* the done callback moves on */
    case P_DONE:
        return PROBE_DONE;
    }
    return PROBE_BUSY;
}