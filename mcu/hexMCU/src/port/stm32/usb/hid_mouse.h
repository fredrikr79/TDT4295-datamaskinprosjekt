#ifndef HID_MOUSE_H
#define HID_MOUSE_H

/* Mouse reports in HID report protocol.
 *
 * In boot protocol a mouse sends only buttons, X and Y: the boot report has
 * no wheel. The wheel (and tilt/pan) only comes in report protocol, where
 * every mouse lays out its report its own way and says how in its report
 * descriptor. hid_mouse_parse() reads that layout once, at mount;
 * hid_mouse_decode() then pulls the values out of each report. */

#include <stdbool.h>
#include <stdint.h>

#define HID_MOUSE_BUTTONS 5     /* left, right, middle, back, forward */

typedef struct {
    uint16_t bit;               /* first bit, counted from after the report ID */
    uint8_t  size;              /* bits; 0 = the mouse doesn't have it         */
    bool     is_signed;
} hid_mouse_field_t;

typedef struct {
    uint8_t     report_id;      /* 0 = reports start without an ID byte */
    uint16_t    bytes;          /* report length needed, without the ID byte */
    hid_mouse_field_t button[HID_MOUSE_BUTTONS];
    hid_mouse_field_t x, y, wheel, pan;
} hid_mouse_layout_t;

typedef struct {
    uint8_t buttons;            /* bit n = button n+1, as in the boot report */
    int32_t dx, dy, wheel, pan;
} hid_mouse_motion_t;

/* false: no relative X and Y found (not a mouse, or a layout we don't read) */
bool hid_mouse_parse(const uint8_t *desc, uint16_t len, hid_mouse_layout_t *out);

/* false: not this mouse's report (another report ID, or too short) */
bool hid_mouse_decode(const hid_mouse_layout_t *l, const uint8_t *report, uint16_t len,
                      hid_mouse_motion_t *out);

#endif
