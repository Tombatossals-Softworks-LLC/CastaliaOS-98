#!/usr/bin/env python3
# bmp2png.py - Convert a screenshot this project produced into a PNG for docs/.
#
# The shell writes uncompressed 24-bit BMPs (src/gfx/gfx_bmp.c); the README and
# the press kit want PNGs. That conversion was being done by hand, which is why
# docs/media went stale relative to the code that draws it: nothing recorded
# how a picture had been made, so nobody could remake it.
#
# Now the pictures the docs use most are reproducible:
#
#   make run                                  # -> build/castalia.bmp
#   python3 tools/bmp2png.py build/castalia.bmp docs/media/desktop-800x600.png
#
# Pure Python standard library (zlib only) -- no Pillow, no ImageMagick, same
# rule as png2bmp.py beside it. Reads the 24-bit uncompressed bottom-up BMPs
# gfx_bmp.c writes, and nothing else: this is a tool for our own screenshots,
# not a general converter, and it says so by refusing anything else rather than
# guessing.
#
# Usage:  python3 tools/bmp2png.py in.bmp out.png [--scale N]
import struct
import sys
import zlib


def read_bmp(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 54 or data[0:2] != b"BM":
        raise SystemExit("bmp2png: %s is not a BMP" % path)
    off = struct.unpack("<I", data[10:14])[0]
    w, h = struct.unpack("<ii", data[18:26])
    planes, bpp = struct.unpack("<HH", data[26:30])
    comp = struct.unpack("<I", data[30:34])[0]
    if bpp != 24 or comp != 0 or planes != 1:
        raise SystemExit("bmp2png: only uncompressed 24-bit BMPs (got %d bpp, "
                         "compression %d)" % (bpp, comp))
    if w <= 0 or h == 0:
        raise SystemExit("bmp2png: bad dimensions %dx%d" % (w, h))
    top_down = h < 0
    h = abs(h)
    stride = ((w * 3 + 3) // 4) * 4
    if off + stride * h > len(data):
        raise SystemExit("bmp2png: file is shorter than its header claims")
    rows = []
    for y in range(h):
        src = off + (y if top_down else (h - 1 - y)) * stride
        line = bytearray()
        for x in range(w):
            p = src + x * 3
            line += bytes((data[p + 2], data[p + 1], data[p]))   # BGR -> RGB
        rows.append(bytes(line))
    return w, h, rows


def write_png(path, w, h, rows, scale):
    if scale > 1:
        wide = []
        for line in rows:
            out = bytearray()
            for x in range(w):
                out += line[x * 3:x * 3 + 3] * scale
            for _ in range(scale):
                wide.append(bytes(out))
        rows = wide
        w *= scale
        h *= scale
    raw = b"".join(b"\x00" + r for r in rows)     # filter byte 0 per scanline

    def chunk(tag, payload):
        body = tag + payload
        return (struct.pack(">I", len(payload)) + body +
                struct.pack(">I", zlib.crc32(body)))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(raw, 9)) +
           chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def main(argv):
    if len(argv) < 3:
        raise SystemExit("usage: bmp2png.py in.bmp out.png [--scale N]")
    scale = 1
    if "--scale" in argv:
        scale = int(argv[argv.index("--scale") + 1])
        if scale < 1:
            raise SystemExit("bmp2png: --scale must be at least 1")
    w, h, rows = read_bmp(argv[1])
    write_png(argv[2], w, h, rows, scale)
    print("bmp2png: %s -> %s (%dx%d%s)" %
          (argv[1], argv[2], w * scale, h * scale,
           "" if scale == 1 else ", %dx" % scale))


if __name__ == "__main__":
    main(sys.argv)
