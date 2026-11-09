/* Reads a mouse's HID report descriptor (HID 1.11, chapter 6.2.2) far
 * enough to find its buttons, X, Y, wheel and pan, and decodes its reports.
 *
 * Only the Input items inside a Mouse application collection count, so a
 * mouse that also describes media keys or a vendor report in the same
 * descriptor still works: those reports have another report ID and are
 * skipped by hid_mouse_decode(). */

#include <string.h>

#include "hid_mouse.h"

#define PAGE_DESKTOP  0x01u
#define PAGE_BUTTON   0x09u
#define PAGE_CONSUMER 0x0Cu
#define U(page, id)   (((uint32_t)(page) << 16) | (id))

#define MAX_USAGES 16u
#define MAX_IDS    8u
#define MAX_PUSH   4u

typedef struct {
    uint16_t page;
    int32_t  logical_min;
    uint32_t size, count;
    uint8_t  id;
} globals_t;

typedef struct {
    hid_mouse_field_t f;
    uint8_t     id;
} found_t;

bool hid_mouse_parse(const uint8_t *d, uint16_t len, hid_mouse_layout_t *out)
{
    globals_t g = { 0 }, stack[MAX_PUSH];
    unsigned  sp = 0;

    uint32_t usages[MAX_USAGES];        /* local items, cleared after each main item */
    unsigned n_usages = 0;
    uint32_t short_usage = 0;           /* bit k: usages[k] was 1-2 bytes, page still to add */
    uint32_t umin = 0, umax = 0;
    bool     have_min = false, have_max = false, short_min = false, short_max = false;

    uint8_t  ids[MAX_IDS];              /* bit position per report ID */
    uint16_t pos[MAX_IDS];
    unsigned n_ids = 0;

    int depth = 0, mouse_depth = -1;
    found_t button[HID_MOUSE_BUTTONS] = { 0 }, x = { 0 }, y = { 0 }, wheel = { 0 }, pan = { 0 };

    if (!d || !out) return false;

    for (uint16_t i = 0; i < len;) {
        uint8_t b = d[i];
        if (b == 0xFE) {                                  /* long item: skip */
            if (i + 1u >= len) break;
            i = (uint16_t)(i + 3u + d[i + 1]);
            continue;
        }
        unsigned n = (b & 3u) == 3u ? 4u : (b & 3u);
        if (i + 1u + n > len) break;
        uint32_t u = 0;
        for (unsigned k = 0; k < n; k++) u |= (uint32_t)d[i + 1 + k] << (8 * k);
        int32_t s = n == 1 ? (int8_t)u : n == 2 ? (int16_t)u : (int32_t)u;
        unsigned type = (b >> 2) & 3u, tag = b >> 4;
        i = (uint16_t)(i + 1u + n);

        if (type == 1) {                                  /* global */
            switch (tag) {
            case 0x0: g.page = (uint16_t)u; break;
            case 0x1: g.logical_min = s; break;
            case 0x7: g.size = u; break;
            case 0x8: g.id = (uint8_t)u; break;
            case 0x9: g.count = u; break;
            case 0xA: if (sp < MAX_PUSH) stack[sp++] = g; break;
            case 0xB: if (sp) g = stack[--sp]; break;
            default: break;
            }
            continue;
        }
        if (type == 2) {                                  /* local */
            switch (tag) {
            case 0x0:
                if (n_usages < MAX_USAGES) {
                    if (n < 4u) short_usage |= 1u << n_usages;
                    usages[n_usages++] = u;
                }
                break;
            case 0x1: umin = u; have_min = true; short_min = n < 4u; break;
            case 0x2: umax = u; have_max = true; short_max = n < 4u; break;
            default: break;
            }
            continue;
        }
        if (type != 0) continue;                          /* reserved */

        /* main item: a 1- or 2-byte usage takes the usage page in force now */
        for (unsigned k = 0; k < n_usages; k++)
            if (short_usage & (1u << k)) usages[k] = U(g.page, usages[k] & 0xFFFFu);
        if (have_min && short_min) umin = U(g.page, umin & 0xFFFFu);
        if (have_max && short_max) umax = U(g.page, umax & 0xFFFFu);

        if (tag == 0xA) {                                 /* Collection */
            depth++;
            bool application = (u & 0xFFu) == 0x01u;
            if (application && mouse_depth < 0 && n_usages &&
                usages[0] == U(PAGE_DESKTOP, 0x02))       /* Generic Desktop / Mouse */
                mouse_depth = depth;
        } else if (tag == 0xC) {                          /* End Collection */
            if (depth == mouse_depth) mouse_depth = -1;
            if (depth) depth--;
        } else if (tag == 0x8) {                          /* Input */
            unsigned slot = 0;
            while (slot < n_ids && ids[slot] != g.id) slot++;
            if (slot == n_ids && n_ids < MAX_IDS) { ids[n_ids] = g.id; pos[n_ids] = 0; n_ids++; }
            if (slot < n_ids) {
                bool constant = (u & 1u) != 0;
                for (uint32_t k = 0; k < g.count && k < 1024u; k++) {
                    uint32_t usage = 0;
                    if (n_usages)      usage = usages[k < n_usages ? k : n_usages - 1];
                    else if (have_min) usage = (have_max && umin + k > umax) ? umax : umin + k;

                    if (!constant && mouse_depth >= 0 && g.size >= 1u && g.size <= 32u) {
                        found_t f = { { pos[slot], (uint8_t)g.size, g.logical_min < 0 }, g.id };
                        uint16_t page = (uint16_t)(usage >> 16), id = (uint16_t)usage;
                        if (page == PAGE_BUTTON && id >= 1 && id <= HID_MOUSE_BUTTONS) {
                            if (!button[id - 1].f.size) button[id - 1] = f;
                        } else if (usage == U(PAGE_DESKTOP, 0x30)) { if (!x.f.size) x = f; }
                        else if (usage == U(PAGE_DESKTOP, 0x31)) { if (!y.f.size) y = f; }
                        else if (usage == U(PAGE_DESKTOP, 0x38)) { if (!wheel.f.size) wheel = f; }
                        else if (usage == U(PAGE_CONSUMER, 0x238)) { if (!pan.f.size) pan = f; }
                    }
                    pos[slot] = (uint16_t)(pos[slot] + g.size);
                }
            }
        }
        /* Output and Feature items describe other reports: no bits taken here */

        n_usages = 0;
        short_usage = 0;
        have_min = have_max = false;
    }

    if (!x.f.size || !y.f.size || x.id != y.id) return false;

    memset(out, 0, sizeof *out);
    out->report_id = x.id;
    uint32_t end = 0;
    found_t *all[] = { &x, &y, &wheel, &pan, &button[0], &button[1], &button[2], &button[3], &button[4] };
    hid_mouse_field_t *dst[] = { &out->x, &out->y, &out->wheel, &out->pan, &out->button[0], &out->button[1],
                           &out->button[2], &out->button[3], &out->button[4] };
    for (unsigned k = 0; k < sizeof all / sizeof all[0]; k++) {
        if (!all[k]->f.size || all[k]->id != x.id) continue;    /* in another report: ignore */
        *dst[k] = all[k]->f;
        uint32_t e = (uint32_t)all[k]->f.bit + all[k]->f.size;
        if (e > end) end = e;
    }
    out->bytes = (uint16_t)((end + 7u) / 8u);
    return true;
}

static int32_t field(const hid_mouse_field_t *f, const uint8_t *r)
{
    if (!f->size) return 0;
    uint32_t v = 0;
    for (unsigned k = 0; k < f->size; k++) {
        unsigned bit = f->bit + k;
        v |= (((uint32_t)r[bit >> 3] >> (bit & 7u)) & 1u) << k;
    }
    if (f->is_signed && f->size < 32u && (v >> (f->size - 1u)) & 1u)
        v |= ~0u << f->size;                                   /* sign-extend */
    return (int32_t)v;
}

bool hid_mouse_decode(const hid_mouse_layout_t *l, const uint8_t *r, uint16_t len,
                      hid_mouse_motion_t *out)
{
    if (l->report_id) {
        if (len < 1u || r[0] != l->report_id) return false;
        r++;
        len--;
    }
    if (len < l->bytes) return false;

    out->buttons = 0;
    for (unsigned k = 0; k < HID_MOUSE_BUTTONS; k++)
        if (field(&l->button[k], r)) out->buttons |= (uint8_t)(1u << k);
    out->dx    = field(&l->x, r);
    out->dy    = field(&l->y, r);
    out->wheel = field(&l->wheel, r);
    out->pan   = field(&l->pan, r);
    return true;
}
