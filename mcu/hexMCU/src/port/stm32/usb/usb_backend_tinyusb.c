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

#include "tusb.h"

#include "log.h"
#include "platform.h"
#include "usb_backend.h"
#include "usb_port.h"

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
} hid[MAX_HID];

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

    usb_port_init();
    tuh_hid_set_default_protocol(HID_PROTOCOL_BOOT);   /* before init */
    if (!tusb_init(USB_INPUT_RHPORT, &host))
        LOG_ERR("tusb_init failed");
}

void usb_backend_poll(void)
{
    usb_port_poll();            /* VBUS */
    tuh_task();                 /* the callbacks below run from in here */
}

/* ---- TinyUSB callbacks -------------------------------------------------------
 * TinyUSB declares these weak; defining them here replaces its empty ones. */
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

    if (hid[slot].kind == USB_KEYBOARD) usb_input_keyboard_report(report, len);
    else                                usb_input_mouse_report(report, len);

    /* Each request gets one report: ask again, or this was the last. */
    tuh_hid_receive_report(dev_addr, instance);
}