#!/usr/bin/env python3
"""icon_contrast.py - will this icon be VISIBLE where the shell actually draws it?

An icon pack is authored against whatever background the author happened to
have open. The shell draws each group somewhere specific, and two of the Tango
glyphs turned out to be near-white pages with no outline placed on near-white
surfaces: `actions-edit-copy` on the File Manager's glossy button face, and
`text-x-generic` on the Start menu's white panel. Neither is broken art. Both
were invisible, and nothing said so -- an icon that cannot be seen still loads,
still blits, and still passes every check that asks whether it exists.

So this asks the other question. For each icon it counts how many of its
pixels stand clear of the surface behind it, against the background the shell
really paints there -- sampled from a render, not assumed.

WHAT THIS CATCHES, AND WHAT IT DOES NOT.

It reliably catches a BLANK -- an icon with almost no pixels that stand clear
of the surface at all. m-logview had four, against 23 for the next faintest
icon in that menu and a median of 104. That gap is not a judgement call.

It does NOT reliably catch a WASH. Tango's actions-edit-copy, which was the
worst-looking glyph on the toolbar, scores 85 -- above tb-cut at 62 and tb-up
at 70, both of which look fine. Its strong pixels are thin mid-grey outlines
spread over a large pale area: individually they clear the threshold, and
together they still read as a smudge. That one was found by looking at a
zoomed render, and measured afterwards.

So both numbers are printed. The FLOOR is enforced on the count, because a
blank is unambiguous. The share is advisory: a low share with a healthy count
means the icon is mostly background-coloured, which is worth a look but is
also exactly what a legible playing-card icon looks like (mostly white card,
carried by a small solid spade and heart -- 41 strong of 190, and perfectly
readable). This tool narrows down where to look. It does not replace looking.

Two notes on the measurement itself:

  * Luminance alone is not enough. A brown filing cabinet on a blue taskbar
    differs by 29 in luminance and is perfectly legible; judged on brightness
    it scores as invisible. The distance below ("redmean") weights the
    channels so chroma counts, which is what makes the taskbar group readable.

  * The backgrounds are sampled from a render, not assumed. Measuring the
    toolbar against the classic (212,208,200) button grey -- which this theme
    does not use -- put a perfectly legible magnifier second-worst in the pack.

This is NOT part of `make lint`, deliberately: every lint step is POSIX sh
with no interpreter dependency, and this needs Python. Run it by hand when
mapping a new icon into a pack.

Usage:  python3 tools/icon_contrast.py [pack-dir]      (default: the Tango pack)
"""
import math
import os
import struct
import sys

# The surface the shell paints behind each group, sampled from a real render
# (--open-launcher --open-fileman) rather than assumed. Several groups have
# more than one: the Start menu paints one column white and the other pale
# blue, and an icon has to survive whichever it lands in.
BACKGROUNDS = {
    "tb-":   [(238, 242, 247)],                    # File Manager toolbar button
    "m-":    [(255, 255, 255), (217, 228, 247)],   # Start menu, both columns
    "ql-":   [(60, 105, 202)],                     # Quick Launch, taskbar blue
    "tray-": [(216, 224, 238)],                    # notification area
}
DEFAULT_BG = [(58, 110, 165)]        # desktop icons, over the wallpaper sky
KEY = (255, 0, 255)                  # GFX_COLORKEY: transparent, not ink
NEAR = 110                           # below this a pixel does not read as ink
FLOOR = 12                           # fewer strong pixels than this is a blank


def redmean(a, b):
    """Perceptual-ish distance. Chroma counts, which luminance alone misses."""
    rm = (a[0] + b[0]) / 2.0
    dr, dg, db = a[0] - b[0], a[1] - b[1], a[2] - b[2]
    return math.sqrt((2 + rm / 256.0) * dr * dr + 4 * dg * dg +
                     (2 + (255 - rm) / 256.0) * db * db)


def read_bmp(path):
    """The 24-bit bottom-up BMPs this project writes. Stdlib only, like
    png2bmp.py -- an icon check that needs a pip install would not get run."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"BM":
        raise ValueError("not a BMP")
    off = struct.unpack_from("<I", data, 10)[0]
    w, h = struct.unpack_from("<ii", data, 18)
    bpp = struct.unpack_from("<H", data, 28)[0]
    if bpp != 24:
        raise ValueError("expected 24bpp, got %d" % bpp)
    stride = (w * 3 + 3) & ~3
    px = []
    for row in range(abs(h)):
        y = (abs(h) - 1 - row) if h > 0 else row
        base = off + y * stride
        for x in range(w):
            b, g, r = data[base + x * 3: base + x * 3 + 3]
            px.append((r, g, b))
    return w, abs(h), px


def backgrounds_for(name):
    for prefix, bgs in BACKGROUNDS.items():
        if name.startswith(prefix):
            return bgs
    return DEFAULT_BG


def main():
    pack = sys.argv[1] if len(sys.argv) > 1 else "assets/icons/tango"
    names = sorted(n for n in os.listdir(pack) if n.endswith(".bmp"))
    if not names:
        print("no BMPs in %s" % pack)
        return 1
    rows, worst = [], 0
    for n in names:
        try:
            _, _, px = read_bmp(os.path.join(pack, n))
        except ValueError as e:
            print("  %-20s unreadable: %s" % (n, e))
            worst += 1
            continue
        ink = [p for p in px if p != KEY]
        if not ink:
            rows.append((n, 0, 0))
            continue
        # The worst background it can land on is the one that decides it.
        strong = min(sum(1 for p in ink if redmean(p, bg) > NEAR)
                     for bg in backgrounds_for(n[:-4]))
        rows.append((n, strong, len(ink)))

    rows.sort(key=lambda r: r[1])
    print("%-22s%8s%8s%8s" % ("icon", "strong", "ink", "share"))
    for n, strong, ink in rows:
        share = (100.0 * strong / ink) if ink else 0.0
        flag = ""
        if strong < FLOOR:
            flag = "   <-- reads as blank"
            worst += 1
        elif share < 50.0:
            flag = "   (mostly background -- look at it)"
        print("%-22s%8d%8d%7.0f%%%s" % (n, strong, ink, share, flag))
    print("\n%d icon(s) below the %d-strong-pixel floor." % (worst, FLOOR))
    print("Icons marked 'look at it' are NOT failures -- see the note at the")
    print("top of this file about what the share does and does not mean.")
    return 1 if worst else 0


if __name__ == "__main__":
    sys.exit(main())
