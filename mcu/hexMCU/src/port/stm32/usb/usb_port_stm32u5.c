/* STM32U5 board side of the USB host -- what TinyUSB doesn't do itself:
 *
 *   - USB clock, power domain and D-/D+ pins (replaces Cube's MX_USB_*_Init,
 *     which must not be called)
 *   - the USB interrupt handler (Cube must not generate one)
 *   - VBUS: the TPS2051C power switch, and USB-C attach detection on CC1/CC2
 *
 * Both USB blocks of the U5 family, picked by the chip header:
 *   USB_DRD_FS  U535/U545  TinyUSB driver: stm32_fsdev   (no UCPD on these)
 *   USB_OTG_HS  U595/U5A5  TinyUSB driver: dwc2          (UCPD1 on PA15/PB15)
 *
 * Board settings:
 *   VBUS switch  Give the pins these labels in CubeMX and main.h gets the
 *                macros this file looks for:
 *                  USB_VBUS_EN   GPIO output, initial level low  (TPS2051C EN,
 *                                high = VBUS on; it must never float)
 *                  USB_VBUS_FLT  GPIO input, pull-up unless the board has one
 *                                (TPS2051C /FLT: open drain, low = overcurrent)
 *                No labels (e.g. the Nucleo): no switch, VBUS is not ours.
 *   USB_USE_UCPD=1        CC1/CC2 are wired to PA15/PB15 (UCPD1): VBUS only
 *                         turns on once a device is plugged in. Otherwise
 *                         VBUS is turned on at start and stays on.
 *   USB_HS_PHY_REFCLK     U595/U5A5: SYSCFG_OTG_HS_PHY_CLK_SELECT_x that
 *                         matches the HSE crystal (default _1 = 16 MHz).
 * Set the last two with target_compile_definitions() in CMake. */

#define LOG_TAG "usb-port"

#include <stdbool.h>
#include <stdint.h>

#include "main.h"               /* Cube: HAL + the pin labels */
#include "tusb.h"
#include "log.h"
#include "platform.h"
#include "usb_port.h"

#if defined(USB_VBUS_EN_Pin) && defined(USB_VBUS_FLT_Pin)
#define HAVE_VBUS_SWITCH 1
#else
#define HAVE_VBUS_SWITCH 0
#endif

#ifndef USB_USE_UCPD
#define USB_USE_UCPD 0
#endif
#if USB_USE_UCPD && !defined(UCPD1)
#error "USB_USE_UCPD: this chip has no UCPD (the U535/U545 don't)"
#endif
#if USB_USE_UCPD && !HAVE_VBUS_SWITCH
#error "USB_USE_UCPD: attach detection needs the VBUS switch pins (USB_VBUS_EN / USB_VBUS_FLT)"
#endif

#ifndef USB_HS_PHY_REFCLK
#define USB_HS_PHY_REFCLK SYSCFG_OTG_HS_PHY_CLK_SELECT_1   /* 16 MHz HSE */
#endif

#define ATTACH_DEBOUNCE_MS  150u    /* USB-C tCCDebounce: 100-200 ms */
#define DETACH_DEBOUNCE_MS  15u     /* USB-C tPDDebounce: 10-20 ms   */
#define FAULT_RETRY_MS      1000u   /* VBUS off this long after overcurrent */

/* ---- interrupt -------------------------------------------------------------
 * The startup file's handlers are weak aliases of Default_Handler, so this
 * one replaces it. TinyUSB enables the IRQ in the NVIC itself (in tusb_init). */
#if defined(USB_DRD_FS)
void USB_IRQHandler(void)
{
    tusb_int_handler(0, true);
}
#elif defined(USB_OTG_HS)
void OTG_HS_IRQHandler(void)
{
    tusb_int_handler(0, true);
}
#else
#error "no USB block known on this chip"
#endif

/* ---- USB-C CC lines (UCPD1 as a source) ------------------------------------
 * We show Rp ("default USB power") on both CC lines with VBUS off. A device
 * or an A-to-C adapter shows Rd on one of them: that's an attach. */
#if USB_USE_UCPD
static void cc_init(void)
{
    GPIO_InitTypeDef g = { 0 };

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_UCPD_CLK_ENABLE();

    g.Pin  = GPIO_PIN_15;               /* CC1 = PA15, CC2 = PB15 */
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);
    HAL_GPIO_Init(GPIOB, &g);

    /* CFG1 is only writable while UCPD is off; values as TinyUSB's UCPD driver */
    UCPD1->CFG1 = (0x0Du << UCPD_CFG1_HBITCLKDIV_Pos) | (0x10u << UCPD_CFG1_IFRGAP_Pos) |
                  (0x07u << UCPD_CFG1_TRANSWIN_Pos) | (0x01u << UCPD_CFG1_PSC_UCPDCLK_Pos);
    UCPD1->CFG1 |= UCPD_CFG1_UCPDEN;

    /* source role (ANAMODE = 0), Rp = default USB power, both CC lines */
    UCPD1->CR = (UCPD1->CR & ~(UCPD_CR_ANAMODE | UCPD_CR_ANASUBMODE | UCPD_CR_CCENABLE)) |
                UCPD_CR_ANASUBMODE_0 | UCPD_CR_CCENABLE_0 | UCPD_CR_CCENABLE_1;
}

static bool sink_attached(void)
{
    /* source mode, per CC line: 00 = vRa, 01 = vRd (sink there), 10 = open */
    uint32_t sr = UCPD1->SR;
    return (sr & UCPD_SR_TYPEC_VSTATE_CC1) == UCPD_SR_TYPEC_VSTATE_CC1_0 ||
           (sr & UCPD_SR_TYPEC_VSTATE_CC2) == UCPD_SR_TYPEC_VSTATE_CC2_0;
}
#elif HAVE_VBUS_SWITCH
static bool sink_attached(void)
{
    return true;                /* no CC detection: VBUS always wanted */
}
#endif

/* ---- VBUS switch (TPS2051C) ----------------------------------------------- */
#if HAVE_VBUS_SWITCH
static void vbus_set(bool on)
{
    HAL_GPIO_WritePin(USB_VBUS_EN_GPIO_Port, USB_VBUS_EN_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static bool vbus_fault(void)
{
    /* the TPS2051C already deglitches this for 9 ms */
    return HAL_GPIO_ReadPin(USB_VBUS_FLT_GPIO_Port, USB_VBUS_FLT_Pin) == GPIO_PIN_RESET;
}

static enum { VB_OFF, VB_ON, VB_FAULT } vb;
static bool     cc_seen;        /* last sink_attached() reading */
static uint32_t cc_since;       /* ...and since when            */
static uint32_t fault_at;
#endif

/* ---- init ----------------------------------------------------------------- */
void usb_port_init(void)
{
    GPIO_InitTypeDef g = { 0 };

    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* D- = PA11, D+ = PA12 */
    g.Pin       = GPIO_PIN_11 | GPIO_PIN_12;
    g.Mode      = GPIO_MODE_AF_PP;
    g.Pull      = GPIO_NOPULL;
    g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Alternate = GPIO_AF10_USB;
    HAL_GPIO_Init(GPIOA, &g);

    HAL_PWREx_EnableVddUSB();   /* the USB pins have their own supply domain */

#if defined(USB_DRD_FS)
    __HAL_RCC_USB_FS_CLK_ENABLE();
#elif defined(USB_OTG_HS)
    __HAL_RCC_SYSCFG_CLK_ENABLE();
    __HAL_RCC_USB_OTG_HS_CLK_ENABLE();
    __HAL_RCC_USBPHYC_CLK_ENABLE();
    HAL_PWREx_EnableUSBHSTranceiverSupply();
    HAL_SYSCFG_SetOTGPHYReferenceClockSelection(USB_HS_PHY_REFCLK);
    HAL_SYSCFG_EnableOTGPHY(SYSCFG_OTG_HS_PHY_ENABLE);
#endif

#if USB_USE_UCPD
    cc_init();
#endif
#ifdef PWR_UCPDR_UCPD_DBDIS
    /* Chips with UCPD start with "dead battery" Rd on PA15/PB15, which says
     * "I'm a sink". Turn it off (after UCPD is set up, if we use it). */
    HAL_PWREx_DisableUCPDDeadBattery();
#endif

#if HAVE_VBUS_SWITCH
    vbus_set(false);
    vb       = VB_OFF;
    cc_seen  = sink_attached();
    cc_since = plat_millis();
    LOG_INFO("VBUS switch, %s", USB_USE_UCPD ? "on when a device attaches" : "always on");
#endif
}

/* ---- poll: VBUS state machine ----------------------------------------------
 *   OFF   -> ON     sink seen on CC for ATTACH_DEBOUNCE_MS (or no CC detection)
 *   ON    -> OFF    CC open for DETACH_DEBOUNCE_MS
 *   ON    -> FAULT  /FLT low: overcurrent or overheating
 *   FAULT -> OFF    after FAULT_RETRY_MS */
void usb_port_poll(void)
{
#if HAVE_VBUS_SWITCH
    uint32_t now = plat_millis();

    if (vb == VB_ON && vbus_fault()) {
        vbus_set(false);        /* also clears /FLT */
        vb       = VB_FAULT;
        fault_at = now;
        LOG_WARN("VBUS overcurrent: off for %u ms", (unsigned)FAULT_RETRY_MS);
        return;
    }
    if (vb == VB_FAULT) {
        if (now - fault_at < FAULT_RETRY_MS) return;
        vb = VB_OFF;
    }

    bool sink = sink_attached();
    if (sink != cc_seen) {
        cc_seen  = sink;
        cc_since = now;
    }
    uint32_t stable = now - cc_since;

    if (vb == VB_OFF && sink && stable >= ATTACH_DEBOUNCE_MS) {
        vbus_set(true);
        vb = VB_ON;
        LOG_INFO("VBUS on");
    } else if (vb == VB_ON && !sink && stable >= DETACH_DEBOUNCE_MS) {
        vbus_set(false);
        vb = VB_OFF;
        LOG_INFO("device removed, VBUS off");
    }
#endif
}