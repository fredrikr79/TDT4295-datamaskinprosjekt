#ifndef DISPLAY_HOST_H
#define DISPLAY_HOST_H

/* ======================================================================
 * display_host.h - the host build's "VGA output": an SDL window showing
 * two RGB565 layers, the way the FPGA composites them.
 *
 *   SIM layer   the simulated world
 *   HUD layer   drawn over it; pixels equal to DISPLAY_HUD_CLEAR are
 *               see-through and show the SIM pixel below
 *
 * Both are DISPLAY_W x DISPLAY_H, row-major, one uint16_t per pixel
 * (RRRRRGGG GGGBBBBB). Write into them directly through display_layer();
 * the window picks the changes up at its next frame (30 Hz).
 * ====================================================================== */

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_W  640
#define DISPLAY_H  360

/* HUD colour key: magenta (255, 0, 255). Any HUD pixel with exactly this
 * value is empty. */
#define DISPLAY_HUD_CLEAR  0xF81Fu

typedef enum { DISPLAY_SIM = 0, DISPLAY_HUD = 1 } display_layer_t;

/* 8-bit-per-channel colour -> RGB565. */
static inline uint16_t display_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

bool      display_init(const char *title, int scale);  /* also clears both layers */
uint16_t *display_layer(display_layer_t layer);        /* DISPLAY_W*DISPLAY_H RGB565 */
void      display_clear(display_layer_t layer);        /* SIM: black, HUD: all clear */
bool      display_poll(void);                          /* false once the window closes */
void      display_shutdown(void);

#endif /* DISPLAY_HOST_H */