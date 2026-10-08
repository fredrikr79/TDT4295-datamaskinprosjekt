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