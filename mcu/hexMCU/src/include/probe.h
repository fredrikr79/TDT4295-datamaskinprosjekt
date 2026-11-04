#ifndef PROBE_H
#define PROBE_H

/* probe.c - the handshake with the FPGA, at startup and after a resync:
 *
 *   1. wait until READY has been high, with no stray edges, for 10 ms
 *      (a READY storm is waited out);
 *   2. ECHO 100 bytes, a new pattern each try, every 200 ms until one
 *      comes back right (the link works both ways);
 *   3. INFO: screen size, N, session (fpga_link_info()). If it fails,
 *      back to 1.
 *
 * While it waits, one status line in front of the console prompt says why
 * ("probing FPGA: x7 (...)", "READY storm: ..."), updated in place.
 *
 *   probe_start()   begin again from 1 (after fpga_abort_all() in a resync)
 *   probe_poll()    main loop: PROBE_BUSY until INFO has come back, then
 *                   PROBE_DONE (and stays so until the next probe_start()).
 * It starts by itself on the first probe_poll(). */

typedef enum { PROBE_BUSY, PROBE_DONE } probe_status_t;

void           probe_start(void);
probe_status_t probe_poll(void);

#endif