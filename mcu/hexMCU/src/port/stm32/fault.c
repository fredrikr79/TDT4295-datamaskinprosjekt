/* Crash report and stack meter for STM32 Cortex-M33/M55 (ARMv8-M): U5 and N6.
 *
 * HardFault: prints what happened over the log UART (huart1) with polled
 * register writes -- no HAL, no printf, no interrupts, its own stack -- so
 * it works even after a stack overflow and without a debugger. Then it
 * stops at a breakpoint if a debugger is attached, else resets. Find the
 * line that crashed with
 *     arm-none-eabi-addr2line -f -e <project>.elf <pc>
 *
 * Stack meter: stack_paint() fills the free stack with a pattern once at
 * start; stack_used() then tells how deep the stack has been since.
 *
 * CubeMX must not generate a HardFault_Handler: NVIC -> Code generation ->
 * untick "Generate IRQ handler" for "Hard fault interrupt". Otherwise the
 * link fails with "multiple definition of HardFault_Handler". */

#include <stdbool.h>
#include <stdint.h>

#include "main.h"               /* CubeMX: HAL, CMSIS */
#include "fault.h"

extern UART_HandleTypeDef huart1;   /* the log UART, as io_uart.c */

/* From the CubeMX linker script. _Min_Stack_Size is an absolute symbol:
 * its address is the value. */
extern uint32_t _estack[];
extern uint8_t  _Min_Stack_Size[];

#define STACK_FILL 0xA5A5A5A5u

/* ---- stack meter ------------------------------------------------------------- */
static uint32_t *stack_bottom(void)
{
    return (uint32_t *)((uintptr_t)_estack - (uintptr_t)_Min_Stack_Size);
}

uint32_t stack_size(void)
{
    return (uint32_t)(uintptr_t)_Min_Stack_Size;
}

void stack_paint(void)
{
    /* Interrupts off: one arriving now would put its frame right where
     * the pattern is being written. */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    uint32_t *p   = stack_bottom();
    uint32_t *end = (uint32_t *)__get_MSP() - 16;   /* keep clear of the live part */
    while (p < end) *p++ = STACK_FILL;

    __set_PRIMASK(primask);
}

uint32_t stack_used(void)
{
    const uint32_t *p = stack_bottom();
    while (p < _estack && *p == STACK_FILL) p++;
    return (uint32_t)((uintptr_t)_estack - (uintptr_t)p);
}

/* ---- polled UART output ---------------------------------------------------------- */
static void fput(char c)
{
    USART_TypeDef *u = huart1.Instance;
    if (u == NULL) return;                          /* fault before MX_USART1_UART_Init */
    for (uint32_t n = 0; !(u->ISR & USART_ISR_TXE_TXFNF); n++)
        if (n > 1000000u) return;                   /* UART not running: give up */
    u->TDR = (uint8_t)c;
}

static void fstr(const char *s)
{
    while (*s) fput(*s++);
}

static void fhex(const char *name, uint32_t v)
{
    fstr(name);
    fstr("0x");
    for (int i = 28; i >= 0; i -= 4) fput("0123456789ABCDEF"[(v >> i) & 0xFu]);
    fstr("  ");
}

static void fdec(uint32_t v)
{
    char buf[11];
    int  i = 0;
    do { buf[i++] = (char)('0' + v % 10u); v /= 10u; } while (v && i < 10);
    while (i) fput(buf[--i]);
}

/* ---- HardFault --------------------------------------------------------------- */
/* frame: what the CPU stacked on fault entry (r0-r3, r12, lr, pc, xPSR).
 * Called from HardFault_Handler below, on fault_stack. */
void fault_report(const uint32_t *frame, uint32_t exc_return, uint32_t sp);

uint32_t fault_stack[256] __attribute__((aligned(8)));     /* 1 KB of its own */

void HardFault_Handler(void);

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst    lr, #4                               \n"  /* EXC_RETURN bit 2: frame on MSP or PSP */
        "ite    eq                                   \n"
        "mrseq  r0, msp                              \n"
        "mrsne  r0, psp                              \n"
        "mov    r1, lr                               \n"
        "mov    r2, r0                               \n"
        "movs   r3, #0                               \n"
        "msr    msplim, r3                           \n"  /* the stack may be what overflowed: */
        "movw   r3, #:lower16:fault_stack+1024       \n"  /* switch to a stack of our own      */
        "movt   r3, #:upper16:fault_stack+1024       \n"
        "mov    sp, r3                               \n"
        "b      fault_report                         \n");
}

__attribute__((used, noreturn))
void fault_report(const uint32_t *frame, uint32_t exc_return, uint32_t sp)
{
    uint32_t cfsr = SCB->CFSR;

    fstr("\r\n\r\n*** HARD FAULT ***\r\n");
    fhex("pc ", frame[6]); fhex("lr ", frame[5]); fhex("xpsr ", frame[7]); fstr("\r\n");
    fhex("r0 ", frame[0]); fhex("r1 ", frame[1]); fhex("r2 ", frame[2]); fhex("r3 ", frame[3]);
    fhex("r12 ", frame[4]); fstr("\r\n");
    fhex("cfsr ", cfsr); fhex("hfsr ", SCB->HFSR);
    fhex("mmfar ", SCB->MMFAR); fhex("bfar ", SCB->BFAR); fstr("\r\n");
#if defined(__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
    fhex("sfsr ", SAU->SFSR); fhex("sfar ", SAU->SFAR);
#endif
    fhex("exc_return ", exc_return); fhex("sp ", sp); fstr("\r\n");

    /* the likely reasons, in words */
    if (cfsr & SCB_CFSR_STKOF_Msk) {
        fstr("stack overflow: ");
        fdec(stack_size());
        fstr(" bytes isn't enough (CubeMX: Project Manager -> Linker Settings -> Minimum Stack Size)\r\n");
    }
    if (cfsr & SCB_CFSR_UNDEFINSTR_Msk) fstr("undefined instruction (jump to garbage? corrupted stack?)\r\n");
    if (cfsr & SCB_CFSR_INVSTATE_Msk)   fstr("invalid state (bad function pointer?)\r\n");
    if (cfsr & SCB_CFSR_UNALIGNED_Msk)  fstr("unaligned access\r\n");
    if (cfsr & SCB_CFSR_DIVBYZERO_Msk)  fstr("divide by zero\r\n");
    if (cfsr & SCB_CFSR_PRECISERR_Msk)  fstr("bus error reading/writing bfar\r\n");
    if (cfsr & SCB_CFSR_IMPRECISERR_Msk) fstr("bus error, imprecise (a write shortly before pc)\r\n");
    if (cfsr & SCB_CFSR_DACCVIOL_Msk)   fstr("memory access violation at mmfar\r\n");
    if (cfsr & SCB_CFSR_IACCVIOL_Msk)   fstr("instruction fetch violation (pc in non-executable memory)\r\n");
    fstr("find the line: arm-none-eabi-addr2line -f -e <project>.elf <pc>\r\n");

    /* wait for the last byte to leave the UART */
    if (huart1.Instance)
        for (uint32_t n = 0; !(huart1.Instance->ISR & USART_ISR_TC) && n < 1000000u; n++) {}

#if defined(DCB)
    if (DCB->DHCSR & DCB_DHCSR_C_DEBUGEN_Msk) __BKPT(0);
#else
    if (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk) __BKPT(0);
#endif
    /* No debugger: restart. On a flash-booting board (U5, U595) the program
     * comes back up. On the N6 in development boot the boot ROM doesn't
     * restart it, but the report above is already out. */
    NVIC_SystemReset();
}
