#include "log.h"
#include "cli.h"
#include "transport.h"
#include "fpga.h"
#include "game.h"
#include "usb_input.h"
#include "platform.h"

void myMain(void)
{
    transport_init();
    cli_init();
    usb_init();
    player_init();
    portals_init();

    while(1) {
        transport_poll();   /* fake FPGA timing on host; no-op on hardware */
        fpga_poll();        /* advance the running FPGA command, if any    */
        cli_poll();
        usb_poll();
        game_poll();
    }
}



/* Example: read the cells around something, with a callback (fpga.h).
 *
 *   static void my_done(const fpga_cmd_t *c, fpga_err_t err)
 *   {
 *       if (err != FPGA_ERR_NONE) return;       // failed: the game resyncs
 *       ...c->rx holds w * h cell bytes...
 *   }
 *
 *   fpga_cmd_t c = fpga_cmd_read_box(fpga_box(x, y, w, h), buf,
 *                                    (uint16_t)fpga_sim_bytes(w * h));
 *   c.done = my_done;
 *   fpga_status_t st = fpga_submit(&c);         // FPGA_EFULL: try again next poll
 *
 * x, y, w, h count grid cells (160x90), not screen pixels. */