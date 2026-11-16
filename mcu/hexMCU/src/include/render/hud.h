#ifndef HUD_H
#define HUD_H

/* hud.c - the in-game HUD: what's on the screen while playing, on top of
 * the simulation. The menu (menu.c) and drawing mode (draw.c) have their
 * own; the cursor is game.c's, since it's shown in every mode.
 *
 *   - two bars at the bottom right, side by side: health (red) against the
 *     right edge, jetpack fuel (orange) to its left, each over a dark grey
 *     "empty" bar
 *   - the test sentence at the top and the demo animation (sprites/spinner.spr)
 *
 * A bar is two sprites the same size: the empty bar, and the fill in front
 * of it. The fill is moved down past the screen's bottom edge so that only
 * its top `percent` shows; the FPGA doesn't draw what's off the screen (no
 * wrap-around). So a bar costs 2 sprites and no new images when it changes,
 * and the bars must touch the bottom edge.
 *
 *   hud_init(w, h);           once, after assets_init(), before the cursor's
 *                             sprite (so the cursor is drawn on top)
 *   health_bar_set(75);       0 = empty .. 100 = full (more counts as 100)
 *   fuel_bar_set(30);
 *   hud_visible(false);       while drawing; true to show it again
 *   hud_frame();              once per game frame (the HUD_DEMO slide) */

#include <stdbool.h>
#include <stdint.h>

/* 1: the bars slide up and down by themselves (in hud_frame()), to show
 * they work. 0: they show what health_bar_set() / fuel_bar_set() say. */
#ifndef HUD_DEMO
#define HUD_DEMO 1
#endif

void hud_init(uint16_t screen_w, uint16_t screen_h);
void hud_visible(bool on);
void hud_frame(void);

void health_bar_set(unsigned percent);
void fuel_bar_set(unsigned percent);

#endif