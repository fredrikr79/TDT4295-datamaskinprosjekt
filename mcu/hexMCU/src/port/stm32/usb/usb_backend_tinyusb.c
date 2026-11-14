/* MCU backend: TinyUSB host HID -> usb_input.c
 *
 * Keyboards stay in boot protocol: a fixed 8-byte report, no descriptor to
 * read. Mice start in boot protocol too, which has no wheel, so a mouse
 * whose report descriptor we can read (hid_mouse.c) is switched to report
 * protocol right after it's mounted. Controllers (DualShock 4 and clones)
 * are recognised by VID:PID and their reports translated here.
 *
 * This file doesn't know the chip. Clocks, pins, the IRQ handler and VBUS
 * are in usb_port_<chip>.c; the TinyUSB settings in tusb_config.h.
 *
 * Written against TinyUSB master as of Oct 2026 (tusb_init(rhport, &init)).
 * On an older release, use tuh_init(USB_INPUT_RHPORT) instead. */

#define LOG_TAG "usb"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cmsis_compiler.h"     /* __get_PRIMASK(), __disable_irq() */
#include "tusb.h"
#include "host/hcd.h"           /* hcd_setup_send(), hcd_edpt_xfer(): wrapped below */

#include "io.h"
#include "log.h"
#include "platform.h"
#include "hid_mouse.h"
#include "usb_backend.h"
#include "usb_input.h"          /* PAD_*, INPUT_PADS */
#include "usb_port.h"

#if CFG_TUSB_DEBUG
/* TinyUSB's own log (CFG_TUSB_DEBUG in tusb_config.h), straight to the
 * UART: it prints partial lines, so no log prefix is added. */
int tusb_log_printf(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n <= 0) return n;
    io_write(buf, (uint16_t)(n < (int)sizeof buf ? n : (int)sizeof buf - 1));
    return n;
}
#endif

#ifndef USB_INPUT_RHPORT
#define USB_INPUT_RHPORT 0      /* root hub port the devices are on */
#endif

#define MAX_HID 8               /* HID interfaces we keep track of */

/* What each mounted interface is. Looked up on every report, and on
 * unmount, when TinyUSB may already have forgotten it. */
static struct {
    bool              used;
    uint8_t           addr, inst;
    usb_device_kind_t kind;
    uint32_t          last_report;  /* plat_millis() of the last report                      */
    uint32_t          reports;      /* how many so far                                       */
    uint32_t          restarts;     /* how often the watchdog had to restart it              */
    enum { IDLE_NONE, IDLE_TO_SEND, IDLE_SENT, IDLE_ON, IDLE_REFUSED } idle;
    enum { PROTO_BOOT, PROTO_TO_SEND, PROTO_SENT, PROTO_REPORT } proto;  /* mice */
    enum { PAD_INIT_NONE, PAD_INIT_CALIB, PAD_INIT_CALIB_SENT,
           PAD_INIT_LED, PAD_INIT_LED_SENT, PAD_INIT_DONE } pad_init;     /* controllers */
    uint8_t           pad_feature;  /* controllers: which start-up feature report is next */
    hid_mouse_layout_t layout;      /* mice: where the report protocol puts what  */
    uint8_t           pad;          /* controllers: 0 .. INPUT_PADS-1             */
} hid[MAX_HID];

/* Keyboard watchdog. Keyboards are asked (HID Set Idle) to repeat their
 * report every KBD_IDLE_MS even when nothing changes. Then:
 *   - a keyboard silent for KBD_SILENT_MS with a request outstanding has
 *     lost its transfer in the controller driver: abort it, ask again;
 *   - a lost report (a key-up, say) is corrected by the next repeat.
 * Repeats change nothing in usb_input.c: same keys, no events. Mice don't
 * need it and many don't support it. */
#define KBD_IDLE_MS     500u    /* Set Idle duration, in 4 ms units on the wire */
#define KBD_SILENT_MS   1500u
#ifndef USB_KBD_WATCHDOG
#define USB_KBD_WATCHDOG 1      /* -DUSB_KBD_WATCHDOG=0: log a lost transfer, don't fix it
                                   (to inspect the stuck state in the debugger) */
#endif

static void hubs_poll(void);
static void hid_keep_polling(void);
static void hid_control_poll(void);
static void ds4_init_step(int i);
static void kbd_watchdog(void);
void usb_debug_dump(void);

/* ---- TinyUSB fix: Get Hub Descriptor ------------------------------------------
 * TinyUSB's hub driver (hub.c, config_get_hub_descriptor) sends Get Hub
 * Descriptor with wValue = 0. USB 2.0 spec 11.24.2.5 wants the descriptor
 * type there (0x29 in the high byte, index 0 in the low byte), and strict
 * hubs (the StarTech 5G2A2CPDB, for one) STALL the request otherwise.
 *
 * That function is static, so it can't be replaced. Instead every setup
 * packet passes through here on its way from usbh.c to the controller
 * driver, and that one request gets corrected. Needs the linker option
 *   -Wl,--wrap=hcd_setup_send
 * (stm32u5.cmake and n6_usbtest.cmake add it). Without it the
 * link fails with "undefined reference to __real_hcd_setup_send". */
bool __real_hcd_setup_send(uint8_t rhport, uint8_t daddr, uint8_t const setup_packet[8]);
bool __wrap_hcd_setup_send(uint8_t rhport, uint8_t daddr, uint8_t const setup_packet[8]);

bool __wrap_hcd_setup_send(uint8_t rhport, uint8_t daddr, uint8_t const setup_packet[8])
{
    /* The controller driver keeps the pointer until the packet has gone
     * out, so the corrected copy must outlive this call. TinyUSB runs one
     * control transfer at a time, so one buffer is enough. */
    static uint8_t fixed[8] TU_ATTR_ALIGNED(4);

    bool get_hub_desc = setup_packet[0] == 0xA0 &&            /* IN, class, device */
                        setup_packet[1] == TUSB_REQ_GET_DESCRIPTOR &&
                        setup_packet[2] == 0 && setup_packet[3] == 0;   /* wValue = 0 */
    if (get_hub_desc) {
        memcpy(fixed, setup_packet, sizeof fixed);
        fixed[3] = 0x29;                /* wValue high byte: hub descriptor type */
        setup_packet = fixed;
    }
    return __real_hcd_setup_send(rhport, daddr, setup_packet);
}

/* ---- TinyUSB fix: lost periodic transfer (dwc2) ---------------------------------
 * hcd_dwc2.c, edpt_xfer_kickoff(): starts the channel with
 * channel_xfer_start(), and only afterwards marks the endpoint "no transfer
 * pending" (edpt->xfer_pending = 0). Called from the main loop (usbh, when
 * a HID report is asked for), the USB interrupt can run in between: for a
 * low-speed device behind a high-speed hub a NAK on the split transaction
 * makes it defer the transfer to the next interval (xfer_pending = 1) and
 * free the channel. The main loop then overwrites xfer_pending with 0: no
 * channel, nothing pending, and usbh still waits for the transfer. That
 * device goes silent until replugged. It takes a busy interrupt (a second
 * device streaming reports, SOF every 125 us) to land in that window, so it
 * happens now and then, with two devices.
 *
 * Fix without patching TinyUSB: run hcd_edpt_xfer() with interrupts
 * masked, so the interrupt's deferral can only come after kickoff is done.
 * It doesn't wait on the interrupt, so this is safe; the window is a few
 * microseconds. PRIMASK is saved and restored, so a caller that already
 * masked interrupts keeps them masked. Needs -Wl,--wrap=hcd_edpt_xfer. */
bool __real_hcd_edpt_xfer(uint8_t rhport, uint8_t daddr, uint8_t ep_addr, uint8_t *buffer, uint16_t buflen);
bool __wrap_hcd_edpt_xfer(uint8_t rhport, uint8_t daddr, uint8_t ep_addr, uint8_t *buffer, uint16_t buflen);

bool __wrap_hcd_edpt_xfer(uint8_t rhport, uint8_t daddr, uint8_t ep_addr, uint8_t *buffer, uint16_t buflen)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    bool ok = __real_hcd_edpt_xfer(rhport, daddr, ep_addr, buffer, buflen);
    __set_PRIMASK(primask);
    return ok;
}

static int hid_find(uint8_t addr, uint8_t inst)
{
    for (int i = 0; i < MAX_HID; i++)
        if (hid[i].used && hid[i].addr == addr && hid[i].inst == inst) return i;
    return -1;
}

/* TinyUSB with CFG_TUSB_OS = OPT_OS_NONE has no clock of its own. */
uint32_t tusb_time_millis_api(void)
{
    return plat_millis();
}

void usb_backend_init(void)
{
    const tusb_rhport_init_t host = {
        .role  = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_AUTO,
    };

    /* Makes the link fail if -Wl,--wrap=hcd_setup_send or
     * -Wl,--wrap=hcd_edpt_xfer is missing, instead of the fixes above
     * silently not being used. */
    bool (*volatile need_wrap1)(uint8_t, uint8_t, uint8_t const *) = __real_hcd_setup_send;
    bool (*volatile need_wrap2)(uint8_t, uint8_t, uint8_t, uint8_t *, uint16_t) = __real_hcd_edpt_xfer;
    (void)need_wrap1;
    (void)need_wrap2;

    usb_port_init();
    tuh_hid_set_default_protocol(HID_PROTOCOL_BOOT);   /* before init */
    if (!tusb_init(USB_INPUT_RHPORT, &host))
        LOG_ERR("tusb_init failed");
}

void usb_backend_poll(void)
{
    usb_port_poll();            /* VBUS */
    tuh_task();                 /* the callbacks below run from in here */
    hid_control_poll();
    kbd_watchdog();
    hid_keep_polling();
    hubs_poll();
}

/* ---- keyboard Set Idle ----------------------------------------------------------
 * Sent once per keyboard, by hid_control_poll() below. */
static void set_idle_done(tuh_xfer_t *xfer)
{
    int i = (int)xfer->user_data;
    if (i < 0 || i >= MAX_HID || !hid[i].used || hid[i].addr != xfer->daddr) return;

    if (xfer->result == XFER_RESULT_SUCCESS) {
        hid[i].idle = IDLE_ON;
        hid[i].last_report = plat_millis();   /* the watchdog's clock starts now */
        LOG_INFO("hid %u.%u: repeats its report every %u ms (watchdog on)",
                 hid[i].addr, hid[i].inst, (unsigned)KBD_IDLE_MS);
    } else {
        hid[i].idle = IDLE_REFUSED;
        LOG_WARN("hid %u.%u: refused Set Idle (result %u): no keyboard watchdog",
                 hid[i].addr, hid[i].inst, (unsigned)xfer->result);
    }
}

/* ---- mouse Set Protocol ---------------------------------------------------------
 * Report protocol for a mouse whose layout we know, for the wheel. Until
 * TinyUSB confirms the switch, reports are still read as boot reports. */
void tuh_hid_set_protocol_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t protocol)
{
    int i = hid_find(dev_addr, idx);
    if (i < 0 || hid[i].kind != USB_MOUSE) return;

    if (protocol == HID_PROTOCOL_REPORT) {
        hid[i].proto = PROTO_REPORT;
        LOG_INFO("hid %u.%u: report protocol%s", dev_addr, idx,
                 hid[i].layout.wheel.size ? ", wheel on" : " (no wheel in its report)");
    } else {
        hid[i].proto = PROTO_BOOT;
        LOG_WARN("hid %u.%u: refused report protocol, staying in boot (no wheel)", dev_addr, idx);
    }
}

/* One control transfer at a time, from the main loop rather than from
 * tuh_hid_mount_cb(): during enumeration the control pipe may still be in
 * use, and tuh_control_xfer() then just refuses. */
static void hid_control_poll(void)
{
    for (int i = 0; i < MAX_HID; i++) {
        if (!hid[i].used) continue;

        if (hid[i].pad_init == PAD_INIT_CALIB || hid[i].pad_init == PAD_INIT_LED) {
            ds4_init_step(i);
            return;
        }
        if (hid[i].proto == PROTO_TO_SEND) {
            if (tuh_hid_set_protocol(hid[i].addr, hid[i].inst, HID_PROTOCOL_REPORT))
                hid[i].proto = PROTO_SENT;
            return;                       /* false: control pipe busy, try again next poll */
        }
        if (hid[i].idle != IDLE_TO_SEND) continue;

        tuh_itf_info_t info;
        if (!tuh_hid_itf_get_info(hid[i].addr, hid[i].inst, &info)) continue;

        const tusb_control_request_t req = {
            .bmRequestType_bit = { .recipient = TUSB_REQ_RCPT_INTERFACE,
                                   .type      = TUSB_REQ_TYPE_CLASS,
                                   .direction = TUSB_DIR_OUT },
            .bRequest = HID_REQ_CONTROL_SET_IDLE,
            .wValue   = tu_htole16((uint16_t)((KBD_IDLE_MS / 4u) << 8)),  /* duration; report id 0 = all */
            .wIndex   = tu_htole16(info.desc.bInterfaceNumber),
            .wLength  = 0,
        };
        tuh_xfer_t xfer = {
            .daddr = hid[i].addr, .ep_addr = 0, .setup = &req, .buffer = NULL,
            .complete_cb = set_idle_done, .user_data = (uintptr_t)i,
        };
        if (tuh_control_xfer(&xfer))      /* false: control pipe busy, try again next poll */
            hid[i].idle = IDLE_SENT;
        return;
    }
}

/* ---- keyboard / controller watchdog ---------------------------------------------
 * Keyboards with Set Idle on, and controllers (a DualShock 4 reports every
 * few ms whether anything changed or not): both should never be silent. */
static void kbd_watchdog(void)
{
    uint32_t now = plat_millis();

    for (int i = 0; i < MAX_HID; i++) {
        if (!hid[i].used || (hid[i].idle != IDLE_ON && hid[i].kind != USB_GAMEPAD)) continue;
        if (now - hid[i].last_report < KBD_SILENT_MS) continue;
        if (tuh_hid_receive_ready(hid[i].addr, hid[i].inst)) continue;  /* hid_keep_polling's job */

        /* Silent, yet a request is outstanding: the transfer is lost.
         * Aborting clears TinyUSB's busy flag even when the controller
         * driver has nothing left of it; hid_keep_polling() then asks again. */
        hid[i].restarts++;
        LOG_WARN("hid %u.%u: no report for %lu ms, transfer lost: %s (%lu so far)",
                 hid[i].addr, hid[i].inst, (unsigned long)(now - hid[i].last_report),
                 USB_KBD_WATCHDOG ? "restarting it" : "left stuck (USB_KBD_WATCHDOG=0)",
                 (unsigned long)hid[i].restarts);
        /* Log the state on the first two restarts, so the event counters in
         * the second dump cover exactly the 1.5 s the keyboard was dead. */
        if (hid[i].restarts <= 2u) usb_debug_dump();
#if USB_KBD_WATCHDOG
        tuh_hid_receive_abort(hid[i].addr, hid[i].inst);
#endif
        hid[i].last_report = now;         /* give the new request a full period */
    }
}

/* Safety net: every keyboard, mouse and controller must always have a report request
 * outstanding, or it goes silent until replugged. The callbacks normally
 * keep the chain going; if it breaks anyway (a request that failed, a
 * failed transfer TinyUSB didn't hand back), it's restarted here and
 * logged, at most every 100 ms per device. */
static void hid_keep_polling(void)
{
    static uint32_t last_try[MAX_HID], last_log;
    uint32_t now = plat_millis();

    for (int i = 0; i < MAX_HID; i++) {
        if (!hid[i].used || now - last_try[i] < 100u) continue;
        if (!tuh_hid_receive_ready(hid[i].addr, hid[i].inst)) continue;   /* request outstanding: fine */

        last_try[i] = now;
        bool ok = tuh_hid_receive_report(hid[i].addr, hid[i].inst);
        if (now - last_log >= 1000u) {
            LOG_WARN("hid %u.%u: reports had stopped, %s", hid[i].addr, hid[i].inst,
                     ok ? "asked again" : "and asking again failed");
            last_log = now;
        }
    }
}

/* ---- TinyUSB callbacks -------------------------------------------------------
 * TinyUSB declares these weak; defining them here replaces its empty ones. */

static const char *speed_name(uint8_t speed)
{
    switch (speed) {
    case TUSB_SPEED_LOW:  return "low";
    case TUSB_SPEED_FULL: return "full";
    case TUSB_SPEED_HIGH: return "high";
    default:              return "?";
    }
}

/* Any device, HID or not: shows enumeration worked even when the device
 * turns out not to be a keyboard or mouse. TinyUSB doesn't call this for
 * hubs; hubs_poll() below logs those. */
void tuh_mount_cb(uint8_t dev_addr)
{
    uint16_t vid = 0, pid = 0;
    tuh_bus_info_t bus = { 0 };
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    tuh_bus_info_get(dev_addr, &bus);

    if (bus.hub_addr)
        LOG_INFO("dev %u: %04x:%04x mounted, %s speed, hub %u port %u",
                 dev_addr, vid, pid, speed_name(bus.speed), bus.hub_addr, bus.hub_port);
    else
        LOG_INFO("dev %u: %04x:%04x mounted, %s speed, root port",
                 dev_addr, vid, pid, speed_name(bus.speed));
}

/* Hubs get the addresses above CFG_TUH_DEVICE_MAX. Their state is checked
 * every poll and changes are logged. */
#if CFG_TUH_HUB
static void hubs_poll(void)
{
    static bool was[CFG_TUH_HUB];

    for (uint8_t i = 0; i < CFG_TUH_HUB; i++) {
        uint8_t addr = (uint8_t)(CFG_TUH_DEVICE_MAX + 1 + i);
        bool now = tuh_mounted(addr);
        if (now == was[i]) continue;
        was[i] = now;

        if (now) {
            uint16_t vid = 0, pid = 0;
            tusb_desc_device_t desc = { 0 };
            tuh_vid_pid_get(addr, &vid, &pid);
            tuh_descriptor_get_device_local(addr, &desc);
            /* high-speed hubs: bDeviceProtocol 1 = one transaction translator
             * shared by all ports, 2 = one per port */
            LOG_INFO("hub %u: %04x:%04x connected, %s speed%s", addr, vid, pid,
                     speed_name((uint8_t)tuh_speed_get(addr)),
                     desc.bDeviceProtocol == 2 ? ", multi-TT" :
                     desc.bDeviceProtocol == 1 ? ", single-TT" : "");
        } else {
            LOG_INFO("hub %u: gone", addr);
        }
    }
}
#else
static void hubs_poll(void) {}
#endif

void tuh_umount_cb(uint8_t dev_addr)
{
    LOG_INFO("dev %u: unmounted", dev_addr);
}

/* ---- controllers -------------------------------------------------------------
 * DualShock 4 and controllers that copy its USB report. Over USB it sends
 * report 0x01 every few ms, 64 bytes:
 *   [0] 0x01  [1..4] left x, left y, right x, right y (0..255, 128 = centre)
 *   [5] d-pad hat (low 4 bits, 0 = up, clockwise, 8 = none), square, cross,
 *       circle, triangle  [6] L1 R1 L2 R2 share options L3 R3
 *   [7] PS, touchpad click, counter  [8] L2 analog  [9] R2 analog
 *   [13..24] gyro, accelerometer  [30] battery  [33..] touchpad
 * (Linux hid-playstation.c, https://www.psdevwiki.com/ps4/DS4-USB)
 * It only starts sending once the host has read its report descriptor, as
 * every PC does: hence CFG_TUH_ENUMERATION_BUFSIZE >= 507 in tusb_config.h. */
static bool is_ds4(uint16_t vid, uint16_t pid)
{
    return (vid == 0x054c && (pid == 0x05c4 || pid == 0x09cc || pid == 0x0ba0))  /* Sony DS4 v1, v2, USB dongle */
        || (vid == 0x0f0d && (pid == 0x005e || pid == 0x00ee));                   /* Hori FC4, Hori PS4 Mini */
}

static void ds4_report(uint8_t pad, const uint8_t *r, uint16_t len)
{
    static const uint32_t hat[8] = {
        PAD_UP, PAD_UP | PAD_RIGHT, PAD_RIGHT, PAD_DOWN | PAD_RIGHT,
        PAD_DOWN, PAD_DOWN | PAD_LEFT, PAD_LEFT, PAD_UP | PAD_LEFT,
    };
    if (len < 10 || r[0] != 0x01) return;       /* other reports: nothing for us */

    usb_gamepad_t g = {
        .lx = (int8_t)(r[1] - 128), .ly = (int8_t)(r[2] - 128),
        .rx = (int8_t)(r[3] - 128), .ry = (int8_t)(r[4] - 128),
        .lt = r[8], .rt = r[9],
    };
    if ((r[5] & 0x0F) < 8) g.buttons |= hat[r[5] & 0x0F];
    if (r[5] & 0x10) g.buttons |= PAD_WEST;     /* square   */
    if (r[5] & 0x20) g.buttons |= PAD_SOUTH;    /* cross    */
    if (r[5] & 0x40) g.buttons |= PAD_EAST;     /* circle   */
    if (r[5] & 0x80) g.buttons |= PAD_NORTH;    /* triangle */
    static const uint32_t b6[8] = { PAD_L1, PAD_R1, PAD_L2, PAD_R2, PAD_SELECT, PAD_START, PAD_L3, PAD_R3 };
    for (unsigned b = 0; b < 8; b++)
        if (r[6] & (1u << b)) g.buttons |= b6[b];
    if (r[7] & 0x01) g.buttons |= PAD_HOME;
    if (r[7] & 0x02) g.buttons |= PAD_TOUCH;

    /* Touchpad: [33] how many touch records follow (0..3, oldest first),
     * each 9 bytes from [34]: a timestamp, then two fingers of 4 bytes:
     * id (bit 7 set = not touching), x 12 bits, y 12 bits. */
    uint8_t n = len >= 34 ? r[33] : 0;
    if (n > 3) n = 3;
    if (n && len >= 34u + 9u * n) {
        const uint8_t *t = &r[34 + 9 * (n - 1)] + 1;  /* newest record, past its timestamp */
        g.touch_valid = true;
        for (unsigned f = 0; f < 2; f++, t += 4) {
            g.touch[f].down = !(t[0] & 0x80);
            g.touch[f].x = (uint16_t)(t[1] | ((t[2] & 0x0F) << 8));
            g.touch[f].y = (uint16_t)((t[2] >> 4) | (t[3] << 4));
        }
    }

    usb_input_gamepad(pad, &g);
}

/* Start-up sequence, as Linux's driver does it over USB (hid-playstation.c,
 * dualshock4_create): read the pairing info (0x81), firmware info (0xa3)
 * and calibration (0x02) feature reports, then send an output report that
 * sets the light bar. The light bar shows the controller number: blue =
 * pad 0, red = pad 1. Runs from hid_control_poll(), one step per control
 * transfer. A refused feature report is logged and skipped. */
static const struct { uint8_t id, len; const char *name; } ds4_features[] = {
    { 0x81, 16, "pairing info" },       /* lengths include the report ID byte */
    { 0xa3, 49, "firmware info" },
    { 0x02, 37, "calibration" },
};
#define DS4_OUT_REPORT   0x05
#define DS4_OUT_LEN      32u           /* USB: report ID + 31 bytes */

static TU_ATTR_ALIGNED(4) uint8_t ds4_buf[64];   /* one control transfer at a time */

static void ds4_init_step(int i)
{
    if (hid[i].pad_init == PAD_INIT_CALIB) {
        unsigned f = hid[i].pad_feature;
        if (tuh_hid_get_report(hid[i].addr, hid[i].inst, ds4_features[f].id, HID_REPORT_TYPE_FEATURE,
                               ds4_buf, ds4_features[f].len))
            hid[i].pad_init = PAD_INIT_CALIB_SENT;
        return;                         /* false: control pipe busy, next poll */
    }

    static const uint8_t colour[INPUT_PADS][3] = { { 0, 0, 64 }, { 64, 0, 0 } };
    memset(ds4_buf, 0, sizeof ds4_buf);
    ds4_buf[0] = DS4_OUT_REPORT;
    ds4_buf[1] = 0x03;                  /* valid: rumble (both off), light bar */
    memcpy(&ds4_buf[6], colour[hid[i].pad % INPUT_PADS], 3);

    /* interrupt OUT endpoint if it has one (takes the data without the ID
     * byte), otherwise Set Report over the control pipe */
    if (tuh_hid_send_report(hid[i].addr, hid[i].inst, DS4_OUT_REPORT, &ds4_buf[1], DS4_OUT_LEN - 1u) ||
        tuh_hid_set_report(hid[i].addr, hid[i].inst, DS4_OUT_REPORT, HID_REPORT_TYPE_OUTPUT,
                           ds4_buf, DS4_OUT_LEN))
        hid[i].pad_init = PAD_INIT_LED_SENT;
}

void tuh_hid_get_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id,
                                    uint8_t report_type, uint16_t len)
{
    int i = hid_find(dev_addr, idx);
    if (i < 0 || hid[i].pad_init != PAD_INIT_CALIB_SENT) return;
    (void)report_type;
    unsigned f = hid[i].pad_feature;
    LOG_INFO("hid %u.%u: %s (feature report 0x%02x): %s", dev_addr, idx, ds4_features[f].name,
             report_id, len ? "ok" : "refused");
    hid[i].pad_feature = (uint8_t)(f + 1u);
    hid[i].pad_init = (hid[i].pad_feature < sizeof ds4_features / sizeof ds4_features[0])
                      ? PAD_INIT_CALIB : PAD_INIT_LED;
}

static void ds4_led_done(uint8_t dev_addr, uint8_t idx, bool ok)
{
    int i = hid_find(dev_addr, idx);
    if (i < 0 || hid[i].pad_init != PAD_INIT_LED_SENT) return;
    LOG_INFO("hid %u.%u: light bar %s", dev_addr, idx, ok ? "set" : "refused");
    hid[i].pad_init = PAD_INIT_DONE;
}

void tuh_hid_report_sent_cb(uint8_t dev_addr, uint8_t idx, const uint8_t *report, uint16_t len)
{
    (void)report;
    ds4_led_done(dev_addr, idx, len != 0);
}

void tuh_hid_set_report_complete_cb(uint8_t dev_addr, uint8_t idx, uint8_t report_id,
                                    uint8_t report_type, uint16_t len)
{
    (void)report_id;
    (void)report_type;
    ds4_led_done(dev_addr, idx, len != 0);
}

/* lowest controller number not in use, or -1 */
static int pad_alloc(void)
{
    for (uint8_t p = 0; p < INPUT_PADS; p++) {
        bool taken = false;
        for (int i = 0; i < MAX_HID; i++)
            if (hid[i].used && hid[i].kind == USB_GAMEPAD && hid[i].pad == p) taken = true;
        if (!taken) return p;
    }
    return -1;
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const *desc_report, uint16_t desc_len)
{
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(dev_addr, &vid, &pid);

    usb_device_kind_t kind;
    if (is_ds4(vid, pid)) {
        kind = USB_GAMEPAD;
    } else {
        switch (tuh_hid_interface_protocol(dev_addr, instance)) {
        case HID_ITF_PROTOCOL_KEYBOARD: kind = USB_KEYBOARD; break;
        case HID_ITF_PROTOCOL_MOUSE:    kind = USB_MOUSE;    break;
        default:
            LOG_INFO("hid %u.%u: %04x:%04x is not a keyboard, mouse or known controller, ignored",
                     dev_addr, instance, vid, pid);
            return;
        }
    }

    int slot = -1;
    for (int i = 0; i < MAX_HID; i++)
        if (!hid[i].used) { slot = i; break; }
    int pad = kind == USB_GAMEPAD ? pad_alloc() : 0;
    if (slot < 0 || pad < 0) {
        LOG_WARN("hid %u.%u: too many devices, ignored", dev_addr, instance);
        return;
    }
    memset(&hid[slot], 0, sizeof hid[slot]);
    hid[slot].used = true;
    hid[slot].addr = dev_addr;
    hid[slot].inst = instance;
    hid[slot].kind = kind;
    hid[slot].last_report = plat_millis();
    hid[slot].idle = (kind == USB_KEYBOARD) ? IDLE_TO_SEND : IDLE_NONE;
    hid[slot].pad  = (uint8_t)pad;
    hid[slot].pad_init = (kind == USB_GAMEPAD) ? PAD_INIT_CALIB : PAD_INIT_NONE;

    switch (kind) {
    case USB_KEYBOARD:
        LOG_INFO("hid %u.%u: keyboard connected", dev_addr, instance);
        usb_input_connected(kind, true);
        break;
    case USB_MOUSE:
        /* boot protocol has no wheel: switch to report protocol if we can
         * read this mouse's layout (hid_control_poll sends the request) */
        if (hid_mouse_parse(desc_report, desc_len, &hid[slot].layout)) {
            hid[slot].proto = PROTO_TO_SEND;
            LOG_INFO("hid %u.%u: mouse connected", dev_addr, instance);
        } else {
            LOG_INFO("hid %u.%u: mouse connected (report layout %s: boot protocol, no wheel)",
                     dev_addr, instance, desc_report ? "not understood" : "too long to read");
        }
        usb_input_connected(kind, true);
        break;
    case USB_GAMEPAD:
        LOG_INFO("hid %u.%u: controller %04x:%04x connected as pad %d (report descriptor %s, %u bytes)",
                 dev_addr, instance, vid, pid, pad, desc_report ? "read" : "skipped", desc_len);
        usb_input_gamepad_connected((uint8_t)pad, true);
        break;
    }

    if (!tuh_hid_receive_report(dev_addr, instance))
        LOG_WARN("hid %u.%u: can't request reports", dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    int slot = hid_find(dev_addr, instance);
    if (slot < 0) return;
    hid[slot].used = false;
    LOG_INFO("hid %u.%u: disconnected", dev_addr, instance);
    if (hid[slot].kind == USB_GAMEPAD) usb_input_gamepad_connected(hid[slot].pad, false);
    else                               usb_input_connected(hid[slot].kind, false);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const *report, uint16_t len)
{
    int slot = hid_find(dev_addr, instance);
    if (slot < 0) return;

    hid[slot].last_report = plat_millis();
    hid[slot].reports++;

    switch (hid[slot].kind) {
    case USB_KEYBOARD:
        usb_input_keyboard_report(report, len);
        break;
    case USB_MOUSE: {
        hid_mouse_motion_t m;
        if (hid[slot].proto != PROTO_REPORT)
            usb_input_mouse_report(report, len);                /* boot layout */
        else if (hid_mouse_decode(&hid[slot].layout, report, len, &m))
            usb_input_mouse_motion(m.buttons, m.dx, m.dy, m.wheel, m.pan);
        /* else: another report of this mouse (media keys, say): skip */
        break;
    }
    case USB_GAMEPAD:
        ds4_report(hid[slot].pad, report, len);
        break;
    }

    /* Each request gets one report: ask again, or this was the last. */
    if (!tuh_hid_receive_report(dev_addr, instance))
        LOG_WARN("hid %u.%u: can't request the next report", dev_addr, instance);
}

/* A failed interrupt transfer (a transaction error or timeout, e.g. a
 * glitch on a low-speed device behind a high-speed hub) isn't a report, so
 * TinyUSB calls this instead of tuh_hid_report_received_cb(). The next
 * report still has to be asked for here, or that device goes silent until
 * it's replugged. Failures are counted and logged at most once a second.
 * Older TinyUSB (before this callback existed) hands failed transfers to
 * tuh_hid_report_received_cb() instead and never calls this. */
void tuh_hid_report_failed_cb(uint8_t dev_addr, uint8_t instance, xfer_result_t result);
void tuh_hid_report_failed_cb(uint8_t dev_addr, uint8_t instance, xfer_result_t result)
{
    static uint32_t fails, last_log;

    if (hid_find(dev_addr, instance) < 0) return;
    fails++;

    uint32_t now = plat_millis();
    if (fails == 1 || now - last_log >= 1000u) {
        LOG_WARN("hid %u.%u: report failed (result %u: 1 failed, 2 stalled, 3 timeout), %lu so far",
                 dev_addr, instance, (unsigned)result, (unsigned long)fails);
        last_log = now;
    }

    if (!tuh_hid_receive_report(dev_addr, instance))
        LOG_WARN("hid %u.%u: can't request the next report", dev_addr, instance);
}
/* ---- debug dump ------------------------------------------------------------------
 * Logs every keyboard/mouse (time since its last report, whether a report
 * request is outstanding) and then the controller's own state through
 * usb_port_dump(). Meant for a button press when a device has gone silent:
 * "request outstanding" plus a long silence means the transfer is stuck in
 * the controller driver, and the channel dump shows where. */
void usb_debug_dump(void);

TU_ATTR_WEAK void usb_port_dump(void) {}   /* chips without one */

void usb_debug_dump(void)
{
    uint32_t now = plat_millis();

    LOG_INFO("---- USB state ----");
    for (int i = 0; i < MAX_HID; i++) {
        if (!hid[i].used) continue;
        LOG_INFO("hid %u.%u %s: %lu reports, last %lu ms ago, %lu restarts, %s", hid[i].addr, hid[i].inst,
                 hid[i].kind == USB_KEYBOARD ? "keyboard" : hid[i].kind == USB_MOUSE ? "mouse" : "controller",
                 (unsigned long)hid[i].reports, (unsigned long)(now - hid[i].last_report),
                 (unsigned long)hid[i].restarts,
                 tuh_hid_receive_ready(hid[i].addr, hid[i].inst) ? "NO request outstanding"
                                                                : "request outstanding");
    }
    usb_port_dump();
    LOG_INFO("-------------------");
}