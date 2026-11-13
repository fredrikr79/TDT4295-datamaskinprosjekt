#!/usr/bin/env python3
"""png2spr.py - PNG(s) -> sprite file for sprite_img_file() / sprite_ani_file().

File format (all 16-bit fields MSB first):
    "SP"  W  H  FRAMES          8-byte header
    FRAMES x W x H pixels       RGB565 MSB first, frame after frame, row by row

Transparent pixels (alpha < 128, or the --key colour) become 0xF81F, the
FPGA's see-through colour. An opaque pixel that happens to be exactly
0xF81F is nudged to 0xF81E so it doesn't vanish.

    # a still image
    png2spr.py cursor.png -o sd/sprites/cursor.spr
    # an animation: a strip of 8 frames side by side, shown 2x bigger
    png2spr.py coin_strip.png --frames 8 --scale 2 -o sd/sprites/coin.spr
    # an animation from one PNG per frame
    png2spr.py walk_0.png walk_1.png walk_2.png -o sd/sprites/walk.spr

Needs Pillow (pip install pillow).
"""
import argparse
import struct
import sys

from PIL import Image

try:
    NEAREST = Image.Resampling.NEAREST      # Pillow 9.1+
except AttributeError:
    NEAREST = Image.NEAREST                 # older Pillow

CLEAR = 0xF81F


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def frames_of(path, n, scale):
    img = Image.open(path).convert("RGBA")
    if img.width % n:
        sys.exit(f"{path}: width {img.width} doesn't split into {n} frames")
    fw = img.width // n
    out = []
    for i in range(n):
        f = img.crop((i * fw, 0, (i + 1) * fw, img.height))
        if scale != 1:
            f = f.resize((f.width * scale, f.height * scale), NEAREST)
        out.append(f)
    return out


def encode(frame, key):
    data = bytearray()
    nudged = 0
    px = frame.tobytes()
    for i in range(0, len(px), 4):
        r, g, b, a = px[i:i + 4]
        if a < 128 or (key is not None and (r, g, b) == key):
            c = CLEAR
        else:
            c = rgb565(r, g, b)
            if c == CLEAR:
                c, nudged = CLEAR - 1, nudged + 1
        data += struct.pack(">H", c)
    return data, nudged


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("png", nargs="+", help="input PNG(s); several = one frame (or strip) each")
    ap.add_argument("-o", "--out", required=True, help="output .spr")
    ap.add_argument("--frames", type=int, default=1, help="frames side by side in each PNG")
    ap.add_argument("--scale", type=int, default=1, help="make it this many times bigger")
    ap.add_argument("--key", help="colour that means see-through, e.g. ff00ff")
    a = ap.parse_args()

    key = tuple(int(a.key[i:i + 2], 16) for i in (0, 2, 4)) if a.key else None
    frames = [f for p in a.png for f in frames_of(p, a.frames, a.scale)]
    w, h = frames[0].size
    for i, f in enumerate(frames):
        if f.size != (w, h):
            sys.exit(f"frame {i} is {f.width}x{f.height}, frame 0 is {w}x{h}")
    if len(frames) > 65535 or w > 65535 or h > 65535:
        sys.exit("too big")

    body, nudged = bytearray(), 0
    for f in frames:
        d, n = encode(f, key)
        body += d
        nudged += n
    with open(a.out, "wb") as fh:
        fh.write(b"SP" + struct.pack(">HHH", w, h, len(frames)) + body)

    per = w * h * 2
    print(f"{a.out}: {w}x{h} px, {len(frames)} frame(s), {per} bytes each "
          f"(~{-(-per // 1022)} memID(s) per frame at N=1024)")
    if nudged:
        print(f"note: {nudged} opaque pixel(s) were exactly the see-through colour; nudged to 0x{CLEAR - 1:04X}")


if __name__ == "__main__":
    main()