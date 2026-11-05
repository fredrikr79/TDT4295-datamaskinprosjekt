/* See usb_dwc2_debug.h. Register offsets: OTG_HS chapter of RM0456 (U5) or
 * RM0486 (N6); the dwc2 core is the same in both. */

#define LOG_TAG "usb-port"

#include <stdint.h>
#include <stdio.h>

#include "cmsis_compiler.h"     /* __disable_irq() */
#include "log.h"
#include "platform.h"
#include "usb_dwc2_debug.h"

#define OTG(off)        (*(volatile uint32_t *)(base + (off)))
#define OTG_CH(n, off)  OTG(0x500u + (n) * 0x20u + (off))

/* ---- event counters ----------------------------------------------------------
 * Per device address, for dwc2_debug_dump(). Read from
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

void dwc2_debug_irq(uintptr_t base)
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
                nyet_run[dev] = 0;                          /* a new poll starts its own count */
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


/* ---- dump ----------------------------------------------------------------------
 * First the channel events counted since the last dump, per device. Through
 * a hub, every poll of a low/full-speed device is a start split (answered
 * "ack") and complete splits answered with data, "nak" (nothing to send) or
 * "nyet" (the hub doesn't have the answer yet). SPLIT_NYET_MAX nyets in a
 * row and TinyUSB gives that poll up ("gave up"), and the report is lost. A
 * "toggle-err" is a packet thrown away because its DATA0/DATA1 was not the
 * one expected.
 *
 * Then the dwc2 host channels, straight from the registers (offsets: the OTG_HS chapter
 * of RM0456 (U5) / RM0486 (N6), same layout). One line per channel in use, decoded:
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

void dwc2_debug_dump(uintptr_t base)
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
