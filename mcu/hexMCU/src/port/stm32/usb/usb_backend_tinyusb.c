/* MCU backend: TinyUSB host HID -> usb_input.c
 *
 * Devices are put in boot protocol, so reports have the fixed layout
 * usb_input.c expects and no report descriptor has to be parsed.
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

#include "tusb.h"
#include "host/hcd.h"           /* hcd_setup_send(), wrapped below */

#include "io.h"
#include "log.h"
#include "platform.h"
#include "usb_backend.h"
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
    uint32_t          last_report;  /* plat_millis() of the last report, for usb_debug_dump() */
    uint32_t          reports;      /* how many so far                                       */
} hid[MAX_HID];

static void hubs_poll(void);
static void hid_keep_polling(void);

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
 * (n6_usbtest.cmake and the main CMakeLists.txt add it). Without it the
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

    /* Makes the link fail if -Wl,--wrap=hcd_setup_send is missing, instead
     * of the hub fix above silently not being used. */
    bool (*volatile need_wrap)(uint8_t, uint8_t, uint8_t const *) = __real_hcd_setup_send;
    (void)need_wrap;

    usb_port_init();
    tuh_hid_set_default_protocol(HID_PROTOCOL_BOOT);   /* before init */
    if (!tusb_init(USB_INPUT_RHPORT, &host))
        LOG_ERR("tusb_init failed");
}

void usb_backend_poll(void)
{
    usb_port_poll();            /* VBUS */
    tuh_task();                 /* the callbacks below run from in here */
    hid_keep_polling();
    hubs_poll();
}

/* Safety net: every keyboard and mouse must always have a report request
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

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const *desc_report, uint16_t desc_len)
{
    (void)desc_report;
    (void)desc_len;

    usb_device_kind_t kind;
    switch (tuh_hid_interface_protocol(dev_addr, instance)) {
    case HID_ITF_PROTOCOL_KEYBOARD: kind = USB_KEYBOARD; break;
    case HID_ITF_PROTOCOL_MOUSE:    kind = USB_MOUSE;    break;
    default:
        LOG_INFO("hid %u.%u: not a keyboard or mouse, ignored", dev_addr, instance);
        return;
    }

    int slot = -1;
    for (int i = 0; i < MAX_HID; i++)
        if (!hid[i].used) { slot = i; break; }
    if (slot < 0) {
        LOG_WARN("hid %u.%u: too many devices, ignored", dev_addr, instance);
        return;
    }
    hid[slot].used = true;
    hid[slot].addr = dev_addr;
    hid[slot].inst = instance;
    hid[slot].kind = kind;
    hid[slot].last_report = plat_millis();
    hid[slot].reports = 0;

    LOG_INFO("hid %u.%u: %s connected", dev_addr, instance,
             kind == USB_KEYBOARD ? "keyboard" : "mouse");
    usb_input_connected(kind, true);

    if (!tuh_hid_receive_report(dev_addr, instance))
        LOG_WARN("hid %u.%u: can't request reports", dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
    int slot = hid_find(dev_addr, instance);
    if (slot < 0) return;
    hid[slot].used = false;
    LOG_INFO("hid %u.%u: disconnected", dev_addr, instance);
    usb_input_connected(hid[slot].kind, false);
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const *report, uint16_t len)
{
    int slot = hid_find(dev_addr, instance);
    if (slot < 0) return;

    hid[slot].last_report = plat_millis();
    hid[slot].reports++;

    if (hid[slot].kind == USB_KEYBOARD) usb_input_keyboard_report(report, len);
    else                                usb_input_mouse_report(report, len);

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
        LOG_INFO("hid %u.%u %s: %lu reports, last %lu ms ago, %s", hid[i].addr, hid[i].inst,
                 hid[i].kind == USB_KEYBOARD ? "keyboard" : "mouse",
                 (unsigned long)hid[i].reports, (unsigned long)(now - hid[i].last_report),
                 tuh_hid_receive_ready(hid[i].addr, hid[i].inst) ? "NO request outstanding"
                                                                : "request outstanding");
    }
    usb_port_dump();
    LOG_INFO("-------------------");
}