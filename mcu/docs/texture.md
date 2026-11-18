# Texture

The FPGA colours each cell by its material, shaded by the cell byte's low 4 bits.
For some materials those bits are the FPGA's own state; for sand and stone they
are free, and the MCU fills them with noise, so a material isn't one flat
colour. Air has no texture. This is `texture.c` / `texture.h`.

![A room textured: a sand pile, stone, water and igneous rock, with the fake FPGA's shading](texture.png)

The picture shows the fake FPGA's stand-in shading; the real look is the FPGA's
colour table.

## Which bits are whose

| Material | Low bits | Texture | Saved in a room file |
|---|---|---|---|
| Air | – | none | no: 0 |
| Sand | `xxxx` | grainy: speckle over faint patches, shades 1–15 | yes: a grain keeps its shade as it falls about |
| Stone | `1xxx` | blotchy: patches with some grit, 8 shades | only the `1`: the texture is made again on load |
| Igneous rock | `0xxx` | none: the FPGA's thickness | yes, all of it |
| Water, lava, acid | `xxxx` | none: the FPGA's speed | no: 0 on load |
| Life, explosive | `xxxx` | none: alive / power | yes, all of it |

Stone and igneous rock are one material on the FPGA (`MAT_STONE`), told apart
by bit 3 (`SIM_STONE_BIT`): stone is 0x18 and up, igneous rock 0x10 and up.

The tables at the top of `texture.c` hold all of this: `tex_sand` and
`tex_stone` say how each texture looks, `save_keep[]` which low bits a save
keeps.

## The noise

Pure random noise looks like static. Each texture is a mix of two kinds,
weighted per material:

- **Smooth:** value noise. A random value every `scale` cells, blended smoothly
  in between, plus a second layer at half the scale for detail. This gives the
  blobs and patches.
- **Grain:** a random value per cell. This gives the speckle.

The mix is stretched (`contrast`) and cut into the material's shades. Everything
comes from a hash of (x, y, room seed), so it's the same every time a room is
loaded. That's why stone doesn't need its texture saved: it comes back the
same. Sand is saved, because its grains move.

**Seeds:** room seed = hash(world seed, room x, room y), so each room looks
different. The world seed is made the first time a room loads and kept in
`save/seed`. Deleting `save/` (F9, or dying) gives a new one: a new world.
Without a card the seed lasts until the MCU restarts.

## Hooks

| Where | Call |
|---|---|
| `rooms.c`, a room just read, before it's sent | `texture_room(tiles, w, h, x, y)` |
| `rooms.c`, before a room is saved | `texture_strip_room(tiles, n)` |
| `draw.c`, each cell drawn | `texture_cell(cell, x, y)` |

Sand from a save already has its shade (never 0), so loading keeps it; sand
with shade 0 (from a `rom/` file, or just drawn) gets one.

## Room files made before igneous rock

Stone used to be 0x10. That byte is now igneous rock, so rooms in `rom/` (and
old saves) written with 0x10 for stone load as igneous rock. Stone needs to be
0x18 in them.