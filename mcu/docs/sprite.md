# sprite.c: images, sprites and animations

[← back to the overview](README.md)

`sprite.c` manages everything on the FPGA's HUD layer: it uploads images
(including from the SD card), places them as sprites, moves, hides and deletes
them, runs animations, and puts it all back after an error or an FPGA restart.
It replaced `hud.c` and `text.c`.

Files: `sprite.h`, `sprite.c`, `assets.h`/`assets.c` (the game's list of images),
`tools/png2spr.py` (PNG → sprite file). It uses [fpga.c](fpga.md) and
[storage](storage.md).

---

## Words

| Word | Meaning | On the FPGA |
|---|---|---|
| **image** | pixels the FPGA keeps in sprite memory | one or more **memIDs** (WRITE_SPRITE) |
| **sprite** | an image placed on screen; can move and hide | one **spriteID** (SPRITE_WRITE / UPDATE / DELETE) |
| **animation** | one sprite per frame, only the current frame shown | several spriteIDs |

Many sprites can share one image. A sprite's image and size are fixed.

## Using it

```c
sprite_init();                                    /* once, before anything else */
assets_init();                                    /* register the game's images */

sprite_t *cur = sprite_new(assets.cursor, x, y, true);
sprite_ani_t *coin = sprite_ani_file("sprites/coin.spr", 6, 100, 50, true);

/* any time, as often as you like: */
sprite_move(cur, mouse_x, mouse_y);
sprite_show(cur, false);
sprite_set(cur, x, y, true);                      /* all three at once */
sprite_delete(cur);                               /* handle invalid after this */

/* the game loop: */
sprite_frame(n);                                  /* each game frame: animations step */
if (!sprite_poll()) resync();                     /* each poll: sends what's needed */

/* error handling: */
fpga_abort_all(); sprite_lost();                  /* on resync */
sprite_forget();                                  /* new FPGA session */
```

- **Your calls never send anything.** `sprite_new`, `sprite_move` and the rest
  only write down what you want. `sprite_poll()` compares that with what the FPGA
  has and sends whatever closes the gap. All commands come from one place, which
  is what makes error recovery work.
- **Handles are `NULL`** when something couldn't be made: a full pool, no card, a
  missing file (all logged). Every function accepts `NULL` and does nothing, so a
  missing sprite never crashes the game.

### Kinds of image

```c
sprite_img_file("sprites/cursor.spr", frame)       /* from the SD card            */
sprite_img_text(&font, "Game of Life", colour, 2)  /* a whole text as one image   */
sprite_img_solid(16, 16, RGB565(255, 0, 0))        /* one colour                  */
sprite_img_ram(px, w, h)                           /* a pixel buffer you keep     */
sprite_img_fill(w, h, my_fn, ctx)                  /* your function draws it      */
```

**None of these keeps pixels in RAM** (except `ram`, which uses your buffer). The
pixels are made 1 KB at a time while uploading: read from the file, drawn from
the font, or filled in. After an FPGA restart they're simply made again.

**Images can't be freed:** FPGA sprite memory only grows until the FPGA resets.
Make images once (in `assets.c`), never per frame. For changing numbers like a
score, make one image per digit and swap sprites.

Images are uploaded **in the background as soon as INFO has told us N**, whether a
sprite uses them yet or not. Images a sprite is waiting for go first.
`sprite_pending()` says how many aren't on the FPGA yet, and
`sprite_img_ready(img)` checks one.

## The structs (inside sprite.c)

Three pools, all static arrays:

| Pool | Size (default) | Handle |
|---|---|---|
| `imgs[]` | 256 (`SPRITE_IMG_MAX`) | `sprite_img_t *` |
| `sprs[]` | 256 (`SPRITE_MAX`) | `sprite_t *` |
| `anis[]` | 16 (`SPRITE_ANI_MAX`) | `sprite_ani_t *` |

A handle is a pointer to a slot. State 0 means free, so zeroing a pool empties it
(`sprite_init`).

**`struct sprite_img`**
- `state`: FREE, NONE (not on the FPGA), SENDING, UPLOADED or NO_ROOM.
- `src`: FILE, TEXT, SOLID, RAM or FILL.
- `w, h`, plus `mem_id` (the first memID) once uploaded.
- `chunks / sent / acked`: how many WRITE_SPRITEs the image needs, how many have
  been submitted, how many came back OK.
- `union u`: one set of fields per source (file path and byte offset; text, font,
  colour and scale; colour; RAM pointer; fill function). It's a union because an
  image only ever has one source.

**`struct sprite`** has three copies of the position:

| Fields | Meaning |
|---|---|
| `x, y, visible` | what the game wants |
| `s_x, s_y, s_visible` | what the FPGA has, as far as we know |
| `p_x, p_y, p_visible` | what the command in flight is sending |

When a command succeeds, `p_*` is copied to `s_*`. If the game moved the sprite
while the command was in flight, `x` ≠ `s_x` afterwards, so the next poll sends
another update. **At most one command per sprite is in flight**, always carrying
the newest position, so a fast-moving sprite never builds a backlog. `remove`
marks a sprite for deletion; `dirty` marks a failed update to resend.

## Making images (registration)

Every `sprite_img_*` call: `img_new()` takes a free slot and sets the size and
source; the state becomes NONE. Nothing is sent yet.

| Function | Size from | Stored |
|---|---|---|
| `sprite_img_file(path, frame)` | the file's 8-byte header, read right away (`file_header()` also checks the file is long enough) | path, and byte offset `8 + frame × W × H × 2` |
| `sprite_img_text(f, s, colour, scale)` | `font_text_width × scale` by `font height × scale` | copy of the string (< 40 bytes), font, colour, scale |
| `sprite_img_solid(w, h, colour)` | given | colour |
| `sprite_img_ram(px, w, h)` | given | pointer; must stay valid |
| `sprite_img_fill(w, h, fn, ctx)` | given | function and context |

Only file images touch storage at registration, and only to read the header.

## Uploading (first half of `sprite_poll`)

### Choosing the next image: `next_upload()`

- **Chunk size** = `min(fpga_max_payload(WRITE_SPRITE), SPRITE_STAGE_BYTES)`,
  rounded down to an even number so a pixel is never split. With N = 1024 that's
  1022 bytes. Each chunk is one WRITE_SPRITE and one memID.
- **Two passes:** first the images a sprite is waiting for, then the rest.
- **Budget check:** if the image's chunks don't fit in the memIDs left, it becomes
  NO_ROOM and is skipped (logged).
- Otherwise it records the chunk count, marks the image SENDING and adds the
  chunks to `memids_pending`.

### Sending its chunks

For each chunk:
- **RAM image:** sent straight from its buffer (`px + offset`).
- **Anything else:**
  1. Take a staging buffer (`stage_get()`; 4 × 1 KB).
  2. `make_chunk()` fills it with pixel bytes `[offset, offset + len)`.
  3. Send that buffer.
- **Queue full:** the chunk already made waits in `ready_buf` until next poll, so
  a file is never read twice for the same chunk.
- When all of an image's chunks are submitted, the next image may start.

**Why one image at a time:** sprite memory only grows, and each WRITE_SPRITE lands
right after the previous one. An image's chunks must sit next to each other,
because SPRITE_WRITE names only the first memID. Another image's chunk in the
middle would cut it in two.

### `make_chunk()` by source

- **FILE:** `storage_read(path, frame_offset + offset, buf, len)`. If a read fails
  halfway through an image, it's too late to stop (earlier chunks are on the
  FPGA). The rest is filled see-through and the error logged once.
- **TEXT:** `make_text()`. For each pixel in the range it works out x and y, then
  walks along the row's letters adding up their widths (`pen`) until it reaches
  the letter covering x. `font_pixel(glyph, (x − pen) / scale, y / scale)` picks
  between the text colour and see-through. At the start of each row it begins
  again from the first letter.
- **SOLID:** every pixel the same colour.
- **FILL:** your function. `funny_man.c` uses this to draw the cube straight from
  its ASCII art.

### When a chunk comes back: `image_done()`

1. `stage_put(cmd->tx)`: the staging buffer is free again.
2. **Budget:**
   - OK → `memids_known = the answer's memID`. That's exact, because the FPGA
     hands out memIDs in order.
   - Cancelled or NACK → the chunk never happened, so nothing is counted.
   - Unknown (a lost answer) → counted as used to be safe; the next OK answer
     corrects it.
3. **The first chunk's memID names the image.** When every chunk is acked, the
   image is UPLOADED.
4. **On failure:**
   - The image goes back to NONE and is uploaded whole again. The memory the
     failed attempt used is lost until the FPGA resets.
   - A "resource exhausted" NACK makes it NO_ROOM instead.
   - `failed = true`, which tells the game to resync.

## Sprites (second half of `sprite_poll`)

At most one command per sprite per poll:

| State | Condition | Sends → new state |
|---|---|---|
| NONE | its image is UPLOADED | SPRITE_WRITE (memID, visible, x, y, w, h) → CREATING |
| LIVE | `remove` set | SPRITE_DELETE → DELETING |
| LIVE | `x/y/visible` ≠ `s_*`, or `dirty` | SPRITE_UPDATE → UPDATING |

**`sprite_done()`**:
- **Create OK:** the answer is the spriteID; the state becomes LIVE.
- **Update OK:** LIVE, with `p_*` copied to `s_*`.
- **Delete OK:** the slot is FREE.
- **Failure:** a failed create goes back to NONE and is retried. A failed update or
  delete goes back to LIVE with `dirty`, so it's sent again. `failed = true`.

Negative coordinates are sent as 0. A sprite that never reached the FPGA is freed
straight away by `sprite_delete`. One deleted while its create is in flight is
deleted as soon as the create finishes.

## Animations

- **`sprite_ani_new(frames[], n, ticks, x, y, visible)`** makes one sprite per
  frame (up to `SPRITE_ANI_FRAMES` = 16), all at the same spot, with only frame 0
  visible. All frames should be the same size.
- **`sprite_ani_file(path, ticks, …)`** reads the frame count from the file
  header, registers `sprite_img_file(path, i)` for each frame, then calls
  `sprite_ani_new`.

**`sprite_frame(n)`**, once per game frame, for each playing animation:
- `count += n`, then `steps = count / ticks`, `count %= ticks`.
- Using `n` keeps the speed right when several frames pass at once.
- If a step is due, `ani_to(new_frame)` hides the current sprite and calls
  `sprite_set(new sprite, ani x, ani y, visible)`.

`ticks` is game frames per animation frame: at 60 Hz, `ticks = 6` gives 10 frames
a second. The fake FPGA on the PC runs at 30 Hz, so there it's 5 a second.

`sprite_ani_move` moves only the visible frame. A hidden frame gets its position
when it's next shown, in that same `sprite_set`. So **a move costs one update and
a frame change costs two**, however many frames there are. A frame change's hide
and show are separate commands and can land in different FPGA frames, so for one
frame both or neither may show.

The rest: `sprite_ani_show`, `sprite_ani_play` (pause or resume),
`sprite_ani_speed`, `sprite_ani_goto` and `sprite_ani_delete` (deletes every
frame's sprite; the images stay).

## Errors and new sessions

**A command fails:**
1. A callback sets `failed`, and `sprite_poll()` returns false.
2. The game calls `fpga_abort_all()`. Every queued command's callback runs with
   CANCELLED and puts its image or sprite back as described above.
3. `sprite_lost()` tidies up what had no callback: an image partway through
   submitting goes back to NONE and gives back its unsent chunks' memIDs. The
   staging buffers are freed and `failed` is cleared.
4. The next polls send whatever is missing.

**New FPGA session** (`sprite_forget()`): FPGA memory is empty.
- Every image goes back to NONE.
- Every sprite goes back to NONE, except ones already being deleted, which are
  simply freed.
- The memID count goes back to 0.
- Everything is uploaded and created again. File images are read from the card
  again and texts drawn again; that's the payoff of not keeping pixels in RAM.

**What can't be fixed from the MCU side:** if an answer is lost, a create or
upload is done again, and the first copy may already exist on the FPGA. That
leaves a stray sprite on screen or wastes memIDs until the FPGA resets. A "delete
all sprites" command, or letting the MCU choose spriteIDs, would fix it.

## Worked example: the cursor, from file to screen

1. `assets_init()` calls `sprite_img_file("sprites/cursor.spr", 0)`.
   `file_header()` reads 8 bytes: 12 × 16 px, 1 frame. The slot is set to FILE,
   offset 8, state NONE.
2. `sprite_new(assets.cursor, 0, 0, true)` gives a sprite in state NONE.
3. INFO arrives (N = 1024). In the next `sprite_poll`, `next_upload()` picks the
   cursor first, because a sprite needs it. 384 bytes is one chunk:
   `memids_pending = 1`, and the image is SENDING.
4. The chunk loop takes a staging buffer, `make_chunk` → `storage_read(path, 8,
   buf, 384)` → WRITE_SPRITE.
5. `image_done`: buffer freed, `memids_known = 1`, `mem_id = 1`, UPLOADED.
6. In the next poll, the sprite loop sees NONE with an uploaded image and sends
   SPRITE_WRITE(memID 1, x, y, 12, 16). The answer gives spriteID 0, and the
   sprite is LIVE.
7. The mouse moves, and `sprite_move` changes `x/y`. The next poll sees
   `x ≠ s_x` and sends SPRITE_UPDATE(0, …), one at a time with the newest
   position.

## Sprite files and tools/png2spr.py

```
"SP"  W  H  FRAMES            8-byte header, 16-bit fields MSB first
FRAMES × W × H pixels         RGB565 MSB first, frame after frame, row by row
```

The pixels are already in wire format, so they're sent as they are.

```sh
python3 tools/png2spr.py cursor.png -o sd/sprites/cursor.spr                  # still image
python3 tools/png2spr.py coin_strip.png --frames 8 --scale 2 -o sd/sprites/coin.spr   # strip of 8
python3 tools/png2spr.py walk_0.png walk_1.png -o sd/sprites/walk.spr         # one PNG per frame
```

- Transparent pixels (alpha < 128, or the `--key` colour) become 0xF81F,
  see-through.
- An opaque pixel that happens to be exactly 0xF81F is nudged to 0xF81E so it
  doesn't vanish.
- The tool prints how many memIDs each frame takes: about one per 1022 bytes at
  N = 1024.

## assets.c

The game's images in one place: the files on the card (with a stand-in if one is
missing), the texts (one image each) and generated ones. `game.c` calls
`sprite_init()` then `assets_init()` on the first room load, and from then on uses
`assets.cursor`, `assets.material[MAT_SAND]` and so on. `ASSET_FONT` (default
`font_kiwisoda`) sets the font for texts. Add new images here.

## Settings and costs

| Define | Default | What |
|---|---|---|
| `SPRITE_IMG_MAX` | 256 | images |
| `SPRITE_MAX` | 256 | sprites |
| `SPRITE_ANI_MAX` / `SPRITE_ANI_FRAMES` | 16 / 16 | animations / frames each |
| `SPRITE_NAME_MAX` | 40 | longest file path or text, including the end byte |
| `SPRITE_STAGE_BYTES` | 1024 | staging buffer size = largest chunk; keep ≥ N − 1 |
| `SPRITE_STAGE_BUFS` | 4 | chunks in flight at once |
| `SPRITE_MEMIDS` | 1023 | memIDs per FPGA session (assumed 1–1023, 0 = none) |

With these defaults `sprite.c` uses about 30 KB of RAM on the MCU. Lower the pool
sizes with `-D` if needed.

- **memID cost:** one per 1022 bytes of an image. A 12 × 16 cursor takes 1; a
  32 × 32 frame (2048 bytes) takes 3; a long text at scale 2 can take 10 or more.
- **Commands per frame:** one per sprite that moved, two per animation frame
  change.
