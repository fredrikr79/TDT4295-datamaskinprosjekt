#include "log.h"
#include "cli.h"
#include "transport.h"
#include "fpga.h"

void myMain(void)
{
    transport_init();
    cli_init();

    while(1) {
        transport_poll();   /* fake FPGA timing on host; no-op on hardware */
        fpga_poll();        /* advance the running FPGA command, if any    */
        cli_poll();
    }
}



// example
// static void myfunc (const fpga_cmd_t *c, fpga_err_t e)

// fpga_cmd_t c = fpga_cmd_read_sim_box(area_around(projectile), around_proj,   AREA_BYTES);
// c.done = myfunc;
// fpga_status_t st = fpga_submit(&c); 