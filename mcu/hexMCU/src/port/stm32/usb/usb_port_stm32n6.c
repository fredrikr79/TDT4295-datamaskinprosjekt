/* STM32N6 (Nucleo-N657X0-Q) board side of the USB host -- what TinyUSB
 * doesn't do itself. Same job as usb_port_stm32u5.c, for the test board.
 *
 *   USB1_OTG_HS: TinyUSB driver dwc2, root hub port 0, embedded HS PHY,
 *   USB-C connector CN8. VBUS comes from the board's 5 V through a
 *   TCPP03-M20 (USB-C port protection), switched over I2C2. VBUS is turned
 *   on at start and stays on: no CC (USB-C attach) detection, which a
 *   keyboard, a hub or an A-to-C adapter doesn't need.
 *
 * Neither CubeMX's MX_USB1_OTG_HS_*_Init nor a CubeMX USB IRQ handler may
 * be used: this file sets up the clocks, the PHY and the IRQ handler. From
 * CubeMX it needs, all in the FSBL context:
 *   - I2C2 on PB10/PB11 (hi2c2)
 *   - the RIF HAL module
 * HSE is started here if CubeMX didn't (it only does when its clock tree
 * uses HSE, and with USB off in CubeMX nothing does).
 *
 * Settings (target_compile_definitions):
 *   USB_TCPP_I2C   the CubeMX I2C handle wired to the TCPP03 (default hi2c2)
 *
 * Nucleo-N657X0-Q pins (UM3417): TCPP03 PWR_EN = PA7, INT (open drain) =
 * PD2. CubeMX's board labels for them (UCPD_PWR_EN, UCPD1_INT) are used
 * when main.h has them.
 *
 * Clock and PHY sequence as TinyUSB's own Nucleo-N657X0-Q board code
 * (hw/bsp/stm32n6/family.c) and ST's N6 USB examples. */

#define LOG_TAG "usb-port"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>               /* snprintf */

#include "main.h"               /* CubeMX: HAL + pin labels */
#include "stm32n6xx_ll_bus.h"   /* LL_AHB5_GRP1_*Reset */
#include "stm32n6xx_ll_rcc.h"   /* LL_RCC_HSE_SelectHSEDiv2AsDiv2Clock */
#include "tusb.h"
#include "log.h"
#include "platform.h"
#include "usb_port.h"

#if !defined(CPU_IN_SECURE_STATE)
#error "build this as secure code (the FSBL, compiled with -mcmse as CubeMX's FSBL project does)"
#endif
#if !defined(HAL_RIF_MODULE_ENABLED)
#error "enable the RIF HAL module in CubeMX (FSBL context)"
#endif

#ifndef USB_TCPP_I2C
#define USB_TCPP_I2C hi2c2
#endif
extern I2C_HandleTypeDef USB_TCPP_I2C;

#if defined(UCPD_PWR_EN_Pin) && defined(UCPD1_INT_Pin)
#define TCPP_EN_PORT    UCPD_PWR_EN_GPIO_Port
#define TCPP_EN_PIN     UCPD_PWR_EN_Pin
#define TCPP_INT_PORT   UCPD1_INT_GPIO_Port
#define TCPP_INT_PIN    UCPD1_INT_Pin
#else
#define TCPP_EN_PORT    GPIOA
#define TCPP_EN_PIN     GPIO_PIN_7
#define TCPP_INT_PORT   GPIOD
#define TCPP_INT_PIN    GPIO_PIN_2
#endif

/* TCPP03 over I2C (registers as ST's stm32-tcpp0203 driver). Addresses in
 * HAL's 8-bit form: 0x68 (7-bit 0x34) on the Nucleo, 0x6A if ADDR is high. */
#define TCPP_ADDR_A     0x68u
#define TCPP_ADDR_B     0x6Au
#define TCPP_REG_CTRL   0x00u       /* write: what we want               */
#define TCPP_REG_FLAGS  0x02u       /* read: what went wrong             */
#define TCPP_FLAGS_MASK 0x7Fu       /* bit 7 = device type, not a fault  */
#define TCPP_NORMAL     0x10u       /* power mode normal (dead-battery Rd off) */
#define TCPP_GDC_OPEN   0x08u       /* consumer (sink) path open         */
#define TCPP_GDP_CLOSED 0x04u       /* provider (source) path closed: VBUS on */

#define VBUS_SETTLE_MS  50u         /* ignore INT this long after VBUS on */
#define FAULT_RETRY_MS  1000u       /* VBUS off this long after a fault  */

/* USBPHYC_CR as ST's sequence writes it (0x00010025). Whole-register
 * write: FSEL resets to 001, so OR-ing 010 in would give an undefined 011. */
#define PHYC_CR_24MHZ   ((1u << USB_USBPHYC_CR_OTGDISABLE0_Pos) | /* VBUS comparator off: we're host */ \
                         (2u << USB_USBPHYC_CR_FSEL_Pos)        | /* 010 = 24 MHz reference          */ \
                         (1u << USB_USBPHYC_CR_CMN_Pos)         | /* PHY PLL off in suspend          */ \
                         (1u << USB_USBPHYC_CR_RETENABLEN1_Pos))  /* retention mode off              */

/* ---- interrupt -------------------------------------------------------------
 * Replaces the startup file's weak alias. TinyUSB numbers USB1 as port 0
 * and enables the IRQ in the NVIC itself (in tusb_init). */
#define OTG(off)        (*(volatile uint32_t *)(USB1_OTG_HS_BASE + (off)))
#define OTG_CH(n, off)  OTG(0x500u + (n) * 0x20u + (off))

/* Channel event counters per device address, for usb_port_dump(). Read from
 * the registers just before TinyUSB handles them, so nothing in TinyUSB
 * changes. Counts are close, not exact: an event that lands while TinyUSB is
 * already in its handler can be handled without us seeing it. */
#define CNT_DEVS 8u
#ifndef SPLIT_NYET_MAX
#define SPLIT_NYET_MAX 7u       /* as patched in cmake/tinyusb_patches.cmake (TinyUSB: 3) */
#endif
enum { C_ACK, C_NAK, C_NYET, C_GIVEUP, C_DTERR, C_XERR, C_FRMOR, C_BBERR, C_STALL, C_N };
static volatile uint32_t ch_cnt[CNT_DEVS][C_N];
static uint8_t nyet_run[CNT_DEVS];
static volatile uint16_t ss_uf[CNT_DEVS][8];   /* start splits sent, by microframe 0..7      */
static volatile uint16_t gu_uf[CNT_DEVS][8];   /* ...and of those, polls given up            */
static uint8_t last_ss_uf[CNT_DEVS];

static void count_channel_events(void)
{
    if (!(OTG(0x014) & OTG(0x018) & (1u << 25))) return;    /* GINTSTS.HCINT */
    uint32_t haint = OTG(0x414);
    for (unsigned n = 0; haint; n++, haint >>= 1) {
        if (!(haint & 1u)) continue;
        uint32_t hcint = OTG_CH(n, 0x08) & OTG_CH(n, 0x0C);
        uint32_t dev   = (OTG_CH(n, 0x00) >> 22) & 0x7Fu;
        if (dev >= CNT_DEVS || !(hcint & ~(1u << 1))) continue;   /* only a halt: nothing new */
        volatile uint32_t *c = ch_cnt[dev];
        if (hcint & (1u << 5)) {
            c[C_ACK]++;
            uint32_t hcsplt = OTG_CH(n, 0x04);
            if ((hcsplt & (1u << 31)) && !(hcsplt & (1u << 16))) {  /* split, start phase */
                uint32_t uf = OTG(0x408) & 7u;                      /* HFNUM: microframe now */
                ss_uf[dev][uf]++;
                last_ss_uf[dev] = (uint8_t)uf;
            }
        }
        if (hcint & (1u << 4))  c[C_NAK]++;
        if (hcint & (1u << 10)) c[C_DTERR]++;
        if (hcint & (1u << 7))  c[C_XERR]++;
        if (hcint & (1u << 9))  c[C_FRMOR]++;
        if (hcint & (1u << 8))  c[C_BBERR]++;
        if (hcint & (1u << 3))  c[C_STALL]++;
        if (hcint & (1u << 6)) {
            c[C_NYET]++;
            /* TinyUSB gives a poll up after this many NYETs in a row */
            if (++nyet_run[dev] >= SPLIT_NYET_MAX) { c[C_GIVEUP]++; gu_uf[dev][last_ss_uf[dev]]++; nyet_run[dev] = 0; }
        } else if (hcint & ((1u << 0) | (1u << 4) | (1u << 10) | (1u << 7))) {
            nyet_run[dev] = 0;                              /* data, NAK or error ends the run */
        }
    }
}

void USB1_OTG_HS_IRQHandler(void);
void USB1_OTG_HS_IRQHandler(void)
{
    count_channel_events();
    tusb_int_handler(0, true);
}

/* ---- TCPP03 -------------------------------------------------------------- */
static uint16_t tcpp_addr = TCPP_ADDR_A;

static bool tcpp_write(uint8_t v)
{
    return HAL_I2C_Mem_Write(&USB_TCPP_I2C, tcpp_addr, TCPP_REG_CTRL,
                             I2C_MEMADD_SIZE_8BIT, &v, 1, 10) == HAL_OK;
}

static uint8_t tcpp_flags(void)
{
    uint8_t v = 0;
    HAL_I2C_Mem_Read(&USB_TCPP_I2C, tcpp_addr, TCPP_REG_FLAGS, I2C_MEMADD_SIZE_8BIT, &v, 1, 10);
    return (uint8_t)(v & TCPP_FLAGS_MASK);
}

static bool vbus_set(bool on)
{
    /* consumer and provider paths are exclusive: consumer stays open */
    return tcpp_write(TCPP_NORMAL | TCPP_GDC_OPEN | (on ? TCPP_GDP_CLOSED : 0u));
}

static bool vbus_fault(void)
{
    /* in normal mode, INT low = overcurrent, overvoltage or overheating */
    return HAL_GPIO_ReadPin(TCPP_INT_PORT, TCPP_INT_PIN) == GPIO_PIN_RESET;
}

static enum { VB_OFF, VB_ON, VB_FAULT } vb;
static uint32_t vb_since;

static void tcpp_init(void)
{
    GPIO_InitTypeDef g = { 0 };

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* Same setup as CubeMX's board labels give; repeated so this file
     * doesn't depend on them. */
    HAL_GPIO_WritePin(TCPP_EN_PORT, TCPP_EN_PIN, GPIO_PIN_RESET);
    g.Pin   = TCPP_EN_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;     /* EN wants a rise time < 50 us */
    HAL_GPIO_Init(TCPP_EN_PORT, &g);

    g.Pin   = TCPP_INT_PIN;             /* open drain */
    g.Mode  = GPIO_MODE_INPUT;
    g.Pull  = GPIO_PULLUP;
    HAL_GPIO_Init(TCPP_INT_PORT, &g);

    HAL_GPIO_WritePin(TCPP_EN_PORT, TCPP_EN_PIN, GPIO_PIN_SET);
    HAL_Delay(5);                       /* let it wake up before I2C */

    if (HAL_I2C_IsDeviceReady(&USB_TCPP_I2C, TCPP_ADDR_A, 2, 10) != HAL_OK &&
        HAL_I2C_IsDeviceReady(&USB_TCPP_I2C, TCPP_ADDR_B, 2, 10) == HAL_OK)
        tcpp_addr = TCPP_ADDR_B;

    if (!vbus_set(false)) {
        LOG_ERR("TCPP03 not answering on I2C (0x%02X/0x%02X): no VBUS",
                (unsigned)TCPP_ADDR_A, (unsigned)TCPP_ADDR_B);
        vb = VB_FAULT;
    } else {
        LOG_INFO("TCPP03 at 0x%02X", (unsigned)tcpp_addr);
        vb = VB_OFF;
    }
    vb_since = plat_millis();
}

/* ---- clocks ----------------------------------------------------------------- */
static void hse_start(void)
{
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_HSERDY)) return;

    /* PLL_NONE = leave the PLLs alone, as CubeMX's own SystemClock_Config
     * does when it only touches one oscillator. */
    RCC_OscInitTypeDef osc = { 0 };
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState       = RCC_HSE_ON;        /* crystal, not bypass */
    osc.PLL1.PLLState  = RCC_PLL_NONE;
    osc.PLL2.PLLState  = RCC_PLL_NONE;
    osc.PLL3.PLLState  = RCC_PLL_NONE;
    osc.PLL4.PLLState  = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK)
        LOG_ERR("HSE won't start: no USB");
}

/* ---- init ----------------------------------------------------------------- */
void usb_port_init(void)
{
    /* USB1 secure + privileged, like the code driving it (the FSBL).
     * RIMC only matters with DMA, which tusb_config.h leaves off. */
    RIMC_MasterConfig_t m = { 0 };
    m.MasterCID = RIF_CID_1;
    m.SecPriv   = RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV;
    __HAL_RCC_RIFSC_CLK_ENABLE();
    HAL_RIF_RIMC_ConfigMasterAttributes(RIF_MASTER_INDEX_OTG1, &m);
    HAL_RIF_RISC_SetSlaveSecureAttributes(RIF_RISC_PERIPH_INDEX_OTG1HS,
                                          RIF_ATTRIBUTE_SEC | RIF_ATTRIBUTE_PRIV);

    /* USB supply domain (VDD33USB): wait until it's up, then use it */
    __HAL_RCC_PWR_CLK_ENABLE();
    HAL_PWREx_EnableVddUSBVMEN();
    uint32_t t0 = plat_millis();
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_USB33RDY)) {
        if (plat_millis() - t0 > 10u) { LOG_ERR("VDD33USB not ready"); break; }
    }
    HAL_PWREx_EnableVddUSB();

    /* PHY kernel and reference clocks: HSE (48 MHz crystal) / 2 = 24 MHz.
     * The _DIV2 variants also set RCC's HSEDIV2SEL to "really divide by 2";
     * the plain HSE_DIRECT ones set it back to undivided 48 MHz. */
    hse_start();
    RCC_PeriphCLKInitTypeDef clk = { 0 };
    clk.PeriphClockSelection    = RCC_PERIPHCLK_USBPHY1 | RCC_PERIPHCLK_USBOTGHS1;
    clk.UsbPhy1ClockSelection   = RCC_USBPHY1CLKSOURCE_HSE_DIRECT_DIV2;
    clk.UsbOtgHs1ClockSelection = RCC_USBOTGHS1CLKSOURCE_HSE_DIRECT_DIV2;
    if (HAL_RCCEx_PeriphCLKConfig(&clk) != HAL_OK)
        LOG_ERR("USB clock config failed");

    /* Reset sequence. The PHY must be set up before tusb_init(), or the
     * core soft reset never finishes. */
    LL_AHB5_GRP1_ForceReset(LL_AHB5_GRP1_PERIPH_OTG1PHYCTL);
    __HAL_RCC_USB1_OTG_HS_FORCE_RESET();
    __HAL_RCC_USB1_OTG_HS_PHY_FORCE_RESET();
    LL_RCC_HSE_SelectHSEDiv2AsDiv2Clock();  /* same as the _DIV2 above; ST's sequence has it here */
    LL_AHB5_GRP1_ReleaseReset(LL_AHB5_GRP1_PERIPH_OTG1PHYCTL);

    __HAL_RCC_USB1_OTG_HS_CLK_ENABLE();
    for (volatile uint32_t i = 0; i < 100u; i++) __NOP();  /* before PHYC access */

    USB1_HS_PHYC->USBPHYC_CR = PHYC_CR_24MHZ;

    __HAL_RCC_USB1_OTG_HS_PHY_RELEASE_RESET();
    for (volatile uint32_t i = 0; i < 100u; i++) __NOP();  /* before releasing the core */
    __HAL_RCC_USB1_OTG_HS_RELEASE_RESET();
    __HAL_RCC_USB1_OTG_HS_PHY_CLK_ENABLE();

    /* Bring-up check: want 24000000 Hz, PHYC 0x00010025, core id 0x4F54xxxx */
    LOG_INFO("USB1 HS: PHY clk %lu Hz, PHYC_CR 0x%08lX, core id 0x%08lX",
             (unsigned long)HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USBOTGHS1),
             (unsigned long)USB1_HS_PHYC->USBPHYC_CR,
             (unsigned long)USB1_OTG_HS->GSNPSID);

    tcpp_init();
}

/* ---- poll: VBUS state machine ----------------------------------------------
 *   OFF   -> ON     right away (no attach detection)
 *   ON    -> FAULT  INT low: the TCPP03 saw overcurrent/-voltage/-heating
 *   FAULT -> OFF    after FAULT_RETRY_MS */
void usb_port_poll(void)
{
    uint32_t now = plat_millis();

    switch (vb) {
    case VB_OFF:
        if (vbus_set(true)) {
            vb = VB_ON;
            LOG_INFO("VBUS on");
        } else {
            vb = VB_FAULT;
        }
        vb_since = now;
        break;

    case VB_ON:
        if (now - vb_since < VBUS_SETTLE_MS || !vbus_fault()) break;
        LOG_WARN("VBUS fault (TCPP03 flags 0x%02X): off for %u ms",
                 (unsigned)tcpp_flags(), (unsigned)FAULT_RETRY_MS);
        vbus_set(false);
        vb = VB_FAULT;
        vb_since = now;
        break;

    case VB_FAULT:
        if (now - vb_since >= FAULT_RETRY_MS) vb = VB_OFF;
        break;
    }
}

/* ---- debug dump ------------------------------------------------------------------
 * First the channel events counted since the last dump, per device. Through
 * a hub, every poll of a low/full-speed device is a start split (answered
 * "ack") and complete splits answered with data, "nak" (nothing to send) or
 * "nyet" (the hub doesn't have the answer yet). Three nyets in a row and
 * TinyUSB gives that poll up ("gave up"), and the report is lost. A
 * "toggle-err" is a packet thrown away because its DATA0/DATA1 was not the
 * one expected.
 *
 * Then the dwc2 host channels, straight from the registers (offsets: RM0486
 * OTG_HS chapter). One line per channel in use, decoded:
 *   en     HCCHAR.CHENA: the channel is armed and the core is working on it
 *   dev/ep the device address and endpoint, "in"/"out", type, "ls" if low speed
 *   split  HCSPLT: split transaction through hub/port, "start" or "complete" phase
 *   hcint  pending channel interrupts, msk = the ones TinyUSB waits for
 * A device that has gone silent with its request outstanding but no channel
 * here: the driver lost the transfer. A channel that is "en" for ever: the
 * core is stuck on it. */
static void dump_counters(void)
{
    static uint32_t last_ms;
    uint32_t now = plat_millis();
    LOG_INFO("channel events in the last %lu ms (per device):", (unsigned long)(now - last_ms));
    last_ms = now;
    for (unsigned d = 1; d < CNT_DEVS; d++) {
        uint32_t c[C_N], any = 0;
        __disable_irq();
        for (unsigned i = 0; i < C_N; i++) { c[i] = ch_cnt[d][i]; ch_cnt[d][i] = 0; any |= c[i]; }
        __enable_irq();
        if (!any) continue;
        LOG_INFO("  dev %u: ack %lu nak %lu nyet %lu (gave up %lu)  toggle-err %lu xact-err %lu overrun %lu babble %lu stall %lu",
                 d, (unsigned long)c[C_ACK], (unsigned long)c[C_NAK], (unsigned long)c[C_NYET],
                 (unsigned long)c[C_GIVEUP], (unsigned long)c[C_DTERR], (unsigned long)c[C_XERR],
                 (unsigned long)c[C_FRMOR], (unsigned long)c[C_BBERR], (unsigned long)c[C_STALL]);

        /* Which microframe of the 1 ms frame the start splits went out in,
         * and how many of those polls were given up: "uframe:sent/gave up". */
        char line[96];
        int len = 0;
        __disable_irq();
        for (unsigned u = 0; u < 8; u++) {
            unsigned sent = ss_uf[d][u], gave = gu_uf[d][u];
            ss_uf[d][u] = gu_uf[d][u] = 0;
            if (sent || gave)
                len += snprintf(line + len, sizeof line - (size_t)len, " %u:%u/%u", u, sent, gave);
            if (len >= (int)sizeof line) len = (int)sizeof line - 1;
        }
        __enable_irq();
        if (len) LOG_INFO("  dev %u start splits by microframe (sent/gave up):%s", d, line);
    }
}

void usb_port_dump(void)
{
    dump_counters();

    static const char *const type[] = { "ctrl", "iso", "bulk", "int" };
    uint32_t gintmsk = OTG(0x018);
    uint32_t hfnum   = OTG(0x408);
    unsigned nch     = ((OTG(0x048) >> 14) & 0xFu) + 1u;    /* GHWCFG2.NUMHSTCHNL */

    LOG_INFO("otg: gintsts 0x%08lX gintmsk 0x%08lX (sof irq %s) frame %lu, hptxsts 0x%08lX, haint 0x%04lX/0x%04lX",
             (unsigned long)OTG(0x014), (unsigned long)gintmsk, (gintmsk & (1u << 3)) ? "on" : "off",
             (unsigned long)(hfnum & 0xFFFFu), (unsigned long)OTG(0x410),
             (unsigned long)OTG(0x414), (unsigned long)OTG(0x418));

    for (unsigned n = 0; n < nch; n++) {
        uint32_t hcchar = OTG_CH(n, 0x00), hcsplt = OTG_CH(n, 0x04);
        uint32_t hcint  = OTG_CH(n, 0x08), hcmsk  = OTG_CH(n, 0x0C), hctsiz = OTG_CH(n, 0x10);
        if (hcchar == 0 && hcmsk == 0) continue;            /* never used */

        LOG_INFO("ch%u: %s dev %lu ep %lu %s %s%s  split %s hub %lu port %lu  hcint 0x%03lX msk 0x%03lX tsiz 0x%08lX",
                 n, (hcchar & (1u << 31)) ? "EN " : "off",
                 (unsigned long)((hcchar >> 22) & 0x7Fu), (unsigned long)((hcchar >> 11) & 0xFu),
                 (hcchar & (1u << 15)) ? "in" : "out", type[(hcchar >> 18) & 3u],
                 (hcchar & (1u << 17)) ? " ls" : "",
                 !(hcsplt & (1u << 31)) ? "no" : (hcsplt & (1u << 16)) ? "complete" : "start",
                 (unsigned long)((hcsplt >> 7) & 0x7Fu), (unsigned long)(hcsplt & 0x7Fu),
                 (unsigned long)hcint, (unsigned long)hcmsk, (unsigned long)hctsiz);
    }
}