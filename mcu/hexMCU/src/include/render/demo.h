#ifndef DEMO_H
#define DEMO_H

/* demo.c - tests and demos on the FPGA, started from the console:
 *
 *   demo            list them
 *   demo <test>     run one (echo, info, cons, box, sprite, scene, clear, all)
 *   demo stop       stop the one running
 *
 * Needs no SD card: everything is made on the fly. One command is in flight
 * at a time and every answer is checked against what was sent; the results
 * go to the log (tag "demo"). While a demo runs it has the FPGA to itself:
 * game_poll() calls demo_poll() instead of running the game. */

#include <stdbool.h>

bool demo_start(const char *name);  /* false: no such test (nothing started) */
void demo_stop(void);
bool demo_busy(void);               /* a demo is running                     */
void demo_poll(void);               /* main loop, while demo_busy()          */
void demo_list(void);               /* the tests, with log_raw (console)     */

#endif
