#!/usr/bin/env python3
"""ttf2c.py - turn a pixel font (TTF/OTF) into a 1-bit C bitmap font.

    python3 tools/ttf2c.py kiwisoda.ttf --size 16 --name kiwisoda \\
        --credit "Kiwi Soda by jeti, CC BY 4.0" --out src/app/fonts

writes font_kiwisoda.c and font_kiwisoda.h for font.h / font.c.

Pixel fonts are drawn on a grid and only come out clean at their design
size (or a multiple). Not sure what that is? Ask:

    python3 tools/ttf2c.py kiwisoda.ttf --find-size

Needs Pillow:  pip install pillow
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

DEFAULT_CHARS = "".join(chr(c) for c in range(32, 127)) + "ÆØÅæøå"


# ---- rendering ---------------------------------------------------------------
def render(font, ch, height, asc, mode):
    """Glyph as an image `height` rows tall with the baseline at row `asc`.
    Placed by the baseline, which is what pixel fonts line up on.
    mode "1": on/off pixels, "L": antialiased (to check pixel alignment)."""
    adv = round(font.getlength(ch))
    bbox = font.getbbox(ch, anchor="ls")        # ink extent around the baseline
    w = max(adv, bbox[2], 1)
    img = Image.new("L", (w, height), 0)
    d = ImageDraw.Draw(img)
    d.fontmode = mode
    d.text((0, asc), ch, font=font, fill=255, anchor="ls")
    return img, adv, bbox


def bits(img):
    w, h = img.size
    px = img.load()
    return [[1 if px[x, y] >= 128 else 0 for x in range(w)] for y in range(h)]


def grey_pixels(img):
    """Pixels close to half on: the outline cuts through them, so whether
    they end up on or off is a coin toss -- the font isn't on the pixel grid
    at this size. Nearly-off or nearly-on pixels (a hair of overlap in the
    outlines) still come out right and don't count."""
    return sum(1 for v in img.tobytes() if 48 < v < 208)


def find_sizes(path, chars, lo=5, hi=64):
    sample = [c for c in chars if c.strip()][:60]
    good = []
    for size in range(lo, hi + 1):
        font = ImageFont.truetype(path, size)
        asc, desc = font.getmetrics()
        if sum(grey_pixels(render(font, c, asc + desc, asc, "L")[0]) for c in sample) == 0:
            good.append(size)
    return good


# ---- output ------------------------------------------------------------------
def c_char(cp):
    if cp == ord("\\"):
        return "backslash"
    if cp == ord("*"):
        return "'*'"
    return "'%s'" % chr(cp) if 32 < cp < 127 else "U+%04X" % cp


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("font", help="TTF/OTF file")
    ap.add_argument("--size", type=int, help="pixel size (the font's design size)")
    ap.add_argument("--name", help="C name: font_<name>, files font_<name>.c/.h")
    ap.add_argument("--chars", default=DEFAULT_CHARS,
                    help="characters to include (default: ASCII 32-126 + ÆØÅæøå)")
    ap.add_argument("--credit", default="", help="licence/credit line for the file header")
    ap.add_argument("--out", default=".", help="output directory")
    ap.add_argument("--no-trim", action="store_true",
                    help="keep rows that are empty in every glyph")
    ap.add_argument("--find-size", action="store_true",
                    help="list the sizes where the font lands on whole pixels, then stop")
    ap.add_argument("--preview", metavar="TEXT", help="print TEXT in the font, then stop")
    a = ap.parse_args()

    if a.find_size:
        sizes = find_sizes(a.font, a.chars)
        print("clean at: " + (", ".join(map(str, sizes)) if sizes else "none in 5..64 "
              "(not a pixel font? any size works, but it won't be crisp)"))
        return 0
    if not a.size:
        ap.error("--size is needed (try --find-size)")

    font = ImageFont.truetype(a.font, a.size)
    asc, desc = font.getmetrics()
    height = asc + desc

    # A character the font doesn't have renders as its "missing" box.
    missing_ref = bits(render(font, "", height, asc, "1")[0])

    glyphs, grey, skipped = [], 0, []
    for ch in dict.fromkeys(a.chars):           # unique, keep order
        img, adv, bbox = render(font, ch, height, asc, "1")
        b = bits(img)
        if ch.strip() and b == missing_ref:
            skipped.append(ch)
            continue
        if bbox[0] < 0:
            print("warning: %r sticks out %d px to the left, cut off" % (ch, -bbox[0]),
                  file=sys.stderr)
        grey += grey_pixels(render(font, ch, height, asc, "L")[0])
        glyphs.append((ord(ch), adv, b))

    if skipped:
        print("warning: not in the font, left out: " + " ".join(skipped), file=sys.stderr)
    if grey:
        print("warning: %d pixels don't land on the grid at size %d -- edges may be "
              "off by one. Sizes that are clean: %s" %
              (grey, a.size, find_sizes(a.font, a.chars) or "none"), file=sys.stderr)

    # Trim rows that are empty in every glyph (line gap, unused descender room).
    top, bottom = 0, height
    if not a.no_trim:
        used = [any(any(g[2][y]) for g in glyphs) for y in range(height)]
        if any(used):
            top = used.index(True)
            bottom = height - used[::-1].index(True)
    rows = bottom - top
    baseline = asc - top
    if rows > 255 or baseline > 255:
        sys.exit("font too tall for the format (max 255 rows)")

    maxw = max(len(g[2][0]) for g in glyphs)
    row_bytes = (maxw + 7) // 8

    if a.preview is not None:
        lookup = {cp: (adv, b) for cp, adv, b in glyphs}
        line = [[] for _ in range(rows)]
        for ch in a.preview:
            adv, b = lookup.get(ord(ch), lookup.get(ord("?")))
            for y in range(rows):
                line[y] += ["#" if v else "." for v in b[top + y]][:max(adv, 1)]
        print("\n".join("".join(r) for r in line))
        return 0

    if not a.name:
        ap.error("--name is needed")
    name = "font_" + a.name
    os.makedirs(a.out, exist_ok=True)

    # Lookup in font.c: glyphs 32..126 first and in order, the rest sorted.
    glyphs.sort(key=lambda g: (not (32 <= g[0] <= 126), g[0]))

    cmd = "python3 tools/ttf2c.py %s --size %d --name %s%s" % (
        os.path.basename(a.font), a.size, a.name,
        "" if a.chars == DEFAULT_CHARS else " --chars ...")
    head = ["/* %s - GENERATED by tools/ttf2c.py, do not edit." % (name + ".c"),
            " *",
            " *   %s" % cmd,
            " *",
            " * From %s at %d px: %d glyphs, %d rows, baseline at row %d." %
            (os.path.basename(a.font), a.size, len(glyphs), rows, baseline)]
    if a.credit:
        head += [" * Font: %s" % a.credit]
    head += [" */"]

    out = head + ['', '#include "%s.h"' % name, '',
                  "static const uint8_t bits[] = {"]
    for cp, adv, b in glyphs:
        out.append("    /* %s, width %d */" % (c_char(cp), adv))
        for y in range(top, bottom):
            row = b[y]
            val = 0
            for x, v in enumerate(row):
                val |= v << x
            data = ", ".join("0x%02X" % ((val >> (8 * i)) & 0xFF) for i in range(row_bytes))
            art = "".join("#" if v else "." for v in row[:max(adv, len(row))])
            out.append("    %s,   /* %s */" % (data, art))
    out += ["};", "",
            "static const font_glyph_t glyphs[] = {"]
    for cp, adv, _ in glyphs:
        out.append("    { 0x%04X, %2d },   /* %s */" % (cp, adv, c_char(cp)))
    out += ["};", "",
            "const font_t %s = {" % name,
            "    .height    = %d," % rows,
            "    .baseline  = %d," % baseline,
            "    .row_bytes = %d," % row_bytes,
            "    .count     = %d," % len(glyphs),
            "    .glyphs    = glyphs,",
            "    .bits      = bits,",
            "};", ""]

    hdr = ["/* %s - GENERATED by tools/ttf2c.py, do not edit. */" % (name + ".h"),
           "#ifndef %s_H" % name.upper(),
           "#define %s_H" % name.upper(),
           "",
           '#include "font.h"',
           "",
           "extern const font_t %s;" % name,
           "",
           "#endif", ""]

    for fname, lines in ((name + ".c", out), (name + ".h", hdr)):
        with open(os.path.join(a.out, fname), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines))
    print("wrote %s.c / .h: %d glyphs, %d rows, %d byte(s) per row, %d bytes of pixels" %
          (os.path.join(a.out, name), len(glyphs), rows, row_bytes,
           len(glyphs) * rows * row_bytes))
    return 0


if __name__ == "__main__":
    sys.exit(main())
