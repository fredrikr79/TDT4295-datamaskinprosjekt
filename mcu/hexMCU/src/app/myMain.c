#include "log.h"
#include "cli.h"
#include "transport.h"

void myMain(void)
{
    transport_init();
    cli_init();

    for (;;) {
        transport_poll();   /* fake FPGA timing on host; no-op on hardware */
        cli_poll();
    }
}