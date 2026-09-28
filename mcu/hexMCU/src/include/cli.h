#pragma once

void cli_init(void);
void cli_poll(void);

/* Registered as log hooks in cli_init() so that log output from anywhere
 * redraws a half-typed command line instead of trampling it. */
void cli_async_begin(void);
void cli_async_end(void);