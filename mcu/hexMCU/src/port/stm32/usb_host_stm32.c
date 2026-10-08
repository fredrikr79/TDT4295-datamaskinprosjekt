#include "tusb.h"
#include "stm32u5xx_hal.h"

uint32_t tusb_time_millis_api(void) { return HAL_GetTick(); }  // required with OPT_OS_NONE

// U545: name from startup_stm32u545xx.s. U595: OTG_HS_IRQHandler
void USB_IRQHandler(void) { tusb_int_handler(0, true); }

void usb_host_init(void) {
    HAL_PWREx_EnableVddUSB();               // U5 has a separate VDDUSB domain
    // + USB clock enable, PA11/PA12 AF (or let CubeMX do it)
    tusb_rhport_init_t init = { .role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO };
    tusb_init(0, &init);
}

void usb_host_poll(void) { tuh_task(); }   // call every main-loop iteration

void tuh_hid_mount_cb(uint8_t dev, uint8_t inst, uint8_t const *desc, uint16_t len) {
    (void)desc; (void)len;
    if (tuh_hid_interface_protocol(dev, inst) == HID_ITF_PROTOCOL_KEYBOARD)
        tuh_hid_receive_report(dev, inst);
}

void tuh_hid_umount_cb(uint8_t dev, uint8_t inst) { (void)dev; (void)inst; }

void tuh_hid_report_received_cb(uint8_t dev, uint8_t inst, uint8_t const *rpt, uint16_t len) {
    if (len >= sizeof(hid_keyboard_report_t)) {
        hid_keyboard_report_t const *kb = (hid_keyboard_report_t const *)rpt;
        // kb->modifier, kb->keycode[0..5] -> push into your input queue
    }
    tuh_hid_receive_report(dev, inst);    // re-arm
}