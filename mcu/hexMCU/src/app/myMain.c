#define LOG_TAG "main"
#include "log.h"
#include "cli.h"
#include "transport.h"
#include "fpga.h"
#include "game.h"
#include "usb_input.h"
#include "platform.h"
#include "badapple.h"

/* 1: the reset button (NRST) reprograms the FPGA too; 0: only logged */
#ifndef BUTTON_REPROGRAMS_FPGA
#define BUTTON_REPROGRAMS_FPGA 1
#endif

/* Why did the MCU start? If only the reset button restarted it, the FPGA
 * kept running in whatever state it was in, so it's reprogrammed too, as
 * 'reset -b fpga' would (PROGRAM_B, then the handshake waits for DONE).
 * Not after power-up (the FPGA loaded with us), 'reset mcu' or flashing
 * (software resets), or a watchdog. See plat_reset_cause() in platform.h. */
static void check_reset_cause(void)
{
    plat_reset_t why = plat_reset_cause();
    LOG_INFO("MCU started by: %s", plat_reset_name(why));
    if (why == PLAT_RESET_PIN && BUTTON_REPROGRAMS_FPGA) {
        LOG_WARN("reset button: reprogramming the FPGA too");
        game_reprogram_fpga();
    }
}

void myMain(void)
{
    transport_init();
    cli_init();
    usb_init();
    player_init();
    portals_init();
    check_reset_cause();

    while(1) {
        transport_poll();   /* fake FPGA timing on host; no-op on hardware */
        fpga_poll();        /* advance the running FPGA command, if any    */
        cli_poll();
        usb_poll();
        game_poll();
        badapple_poll();
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