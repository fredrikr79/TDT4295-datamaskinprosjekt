#pragma once
/* Crash reports and stack use (STM32, Cortex-M33/M55). See fault.c. */

#include <stdint.h>

void     stack_paint(void);     /* once at start: fill the unused stack with a pattern  */
uint32_t stack_used(void);      /* deepest stack use since stack_paint(), in bytes       */
uint32_t stack_size(void);      /* the stack's size, as the linker script sets it        */
