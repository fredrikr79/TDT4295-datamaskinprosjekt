#pragma once
#include <stdint.h>

void io_init(void);
void io_write(const char *buf, uint16_t n);  /* blocking, writes all n */
int  io_getc(void);                          /* next byte, or -1 if none */