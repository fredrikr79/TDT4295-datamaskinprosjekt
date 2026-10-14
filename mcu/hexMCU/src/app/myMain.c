#include "log.h"
#include "cli.h"
#include "transport.h"
#include "fpga.h"
#include "usb_input.h"
#include "platform.h"

void myMain(void)
{
    transport_init();
    cli_init();
    usb_init();

    while(1) {
        transport_poll();   /* fake FPGA timing on host; no-op on hardware */
        fpga_poll();        /* advance the running FPGA command, if any    */
        cli_poll();
        usb_poll();
        usb_poll();
        static uint32_t t;
        if (plat_millis() - t >= 20) {
            t = plat_millis();
            input_t in;
            input_take(&in);
            input_log(&in);
        }
    }
}



// example
// static void myfunc (const fpga_cmd_t *c, fpga_err_t e)

// fpga_cmd_t c = fpga_cmd_read_sim_box(area_around(projectile), around_proj,   AREA_BYTES);
// c.done = myfunc;
// fpga_status_t st = fpga_submit(&c); 