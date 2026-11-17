#ifndef TEXTURE_H
#define TEXTURE_H

/* ======================================================================
 * texture.h - texture in the low bits of cells, and what a save keeps.
 *
 * A cell byte is the material (high 4 bits) and 4 low bits. For some
 * materials the low bits are the FPGA's (its state); for others they are
 * free, and the MCU fills them with noise so a material isn't one flat
 * colour (the FPGA's colour table shows them as shades):
 *
 *   material       low bits  texture             saved in a room file
 *   air            -         none                no: 0
 *   sand           xxxx      grainy              yes: a grain keeps its shade
 *                                                as it falls about
 *   stone          1xxx      blotchy             the 1 only: the texture is
 *                                                made again on load
 *   igneous rock   0xxx      - (FPGA: thickness) yes, all of it
 *   water/lava/acid xxxx     - (FPGA: speed)     no: 0 on load
 *   life, explosive xxxx     - (alive / power)   yes, all of it
 *
 * The tables at the top of texture.c say this; change them there.
 *
 * The noise: value noise (random values on a coarse lattice, smoothly
 * blended between) for the larger shapes, mixed with per-cell noise for
 * grain. All of it comes from (x, y, room seed), so the same room gets the
 * same texture every time it's loaded, and that's why stone doesn't need
 * its saved. room seed = hash(world seed, room x, y): each room
 * looks different. The world seed is made the first time it's needed and
 * kept in save/seed; deleting save/ (F9, dying) makes a new one, a new
 * world. Without a card it lasts until the MCU restarts.
 *
 * Hooks:
 *   rooms.c, a room loaded, before sending:  texture_room(tiles, w, h, x, y)
 *   rooms.c, before saving a room:           texture_strip_room(tiles, n)
 *   draw.c, each cell drawn:                 texture_cell(cell, x, y)
 * ====================================================================== */

#include <stdint.h>

/* A room's cells (row after row, w per row) just read from its file:
 * texture where it belongs. Also makes this the current room for
 * texture_cell(). */
void    texture_room(uint8_t *cells, unsigned w, unsigned h, int room_x, int room_y);

/* Cell byte `cell` at (x, y) in the current room, textured. Cells without
 * texture come back as they are. */
uint8_t texture_cell(uint8_t cell, unsigned x, unsigned y);

/* What a room file keeps of a cell (see the table above), and the same
 * for n cells in place, before saving. */
uint8_t texture_strip(uint8_t cell);
void    texture_strip_room(uint8_t *cells, uint32_t n);

#endif