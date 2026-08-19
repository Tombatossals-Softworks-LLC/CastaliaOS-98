#!/usr/bin/env python3
# png2bmp.py - Convert a PNG icon into the CastaliaOS icon-pack BMP convention.
#
# This is the asset-pipeline bridge that lets a freely-redistributable pixel-art
# icon set (CC0 / MIT / public-domain) be used by the shell WITHOUT any code
# change: convert each PNG to a keyed 24-bit BMP and drop it into a directory
# pointed to by [Assets] Icons= (see assets/icons/README.md).
#
# Transparency handling: a source alpha below --alpha-threshold (or any pixel
# equal to --bg r,g,b for flat-background icons) becomes the CastaliaOS magenta
# key (255,0,255), which the shell blits as transparent. Output is an
# uncompressed 24-bit bottom-up BMP -- exactly what src/gfx/gfx_bmp.c decodes.
#
# Pure Python standard library only (zlib) -- no Pillow/ImageMagick needed.
# Supports 8-bit PNG color types 0 (gray), 2 (RGB), 3 (palette), 6 (RGBA),
# non-interlaced. That covers the common CC0/MIT icon exports.
#
# Usage:
#   python3 tools/png2bmp.py in.png out.bmp [--size N] [--alpha-threshold A]
#                                           [--bg R,G,B] [--pad]
#   # batch a whole folder to 40px icons:
#   for f in pack/*.png; do python3 tools/png2bmp.py "$f" \
#       "assets/icons/thirdparty/$(basename "${f%.png}").bmp" --size 40 --pad; done

import sys, struct, zlib

KEY = (255, 0, 255)  # GFX_COLORKEY


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def load_png(path):
    """Return (w, h, pixels) where pixels is a list of (r,g,b,a) rows-major."""
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG: ' + path)
    pos = 8
    w = h = bitdepth = colortype = interlace = None
    idat = bytearray()
    palette = []
    trns = None
    while pos < len(d):
        (length,) = struct.unpack_from('>I', d, pos)
        ctype = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b'IHDR':
            w, h, bitdepth, colortype, _, _, interlace = struct.unpack('>IIBBBBB', body)
        elif ctype == b'PLTE':
            palette = [(body[i], body[i + 1], body[i + 2]) for i in range(0, len(body), 3)]
        elif ctype == b'tRNS':
            trns = body
        elif ctype == b'IDAT':
            idat += body
        elif ctype == b'IEND':
            break
    if bitdepth != 8 or interlace != 0:
        raise ValueError('unsupported PNG (need 8-bit, non-interlaced): %s (depth=%s interlace=%s)'
                         % (path, bitdepth, interlace))
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colortype]
    raw = zlib.decompress(bytes(idat))
    stride = w * channels
    prev = bytearray(stride)
    rows = []
    off = 0
    for _y in range(h):
        ft = raw[off]; off += 1
        line = bytearray(raw[off:off + stride]); off += stride
        if ft == 1:      # Sub
            for i in range(channels, stride):
                line[i] = (line[i] + line[i - channels]) & 0xFF
        elif ft == 2:    # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:    # Average
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:    # Paeth
            for i in range(stride):
                a = line[i - channels] if i >= channels else 0
                c = prev[i - channels] if i >= channels else 0
                line[i] = (line[i] + _paeth(a, prev[i], c)) & 0xFF
        prev = line
        rows.append(bytes(line))
    # expand to RGBA
    px = []
    for line in rows:
        row = []
        if colortype == 2:      # RGB
            for x in range(w):
                o = x * 3; row.append((line[o], line[o + 1], line[o + 2], 255))
        elif colortype == 6:    # RGBA
            for x in range(w):
                o = x * 4; row.append((line[o], line[o + 1], line[o + 2], line[o + 3]))
        elif colortype == 0:    # gray
            for x in range(w):
                g = line[x]; row.append((g, g, g, 255))
        elif colortype == 3:    # palette
            for x in range(w):
                idx = line[x]; r, g, b = palette[idx]
                a = trns[idx] if (trns and idx < len(trns)) else 255
                row.append((r, g, b, a))
        else:
            raise ValueError('unsupported color type %d' % colortype)
        px.append(row)
    return w, h, px


def resize_nearest(w, h, px, nw, nh):
    out = []
    for y in range(nh):
        sy = y * h // nh
        out.append([px[sy][x * w // nw] for x in range(nw)])
    return nw, nh, out


def resize_area(w, h, px, nw, nh):
    """Box/area downscale with premultiplied-alpha averaging (clean for the
    antialiased source icons). Falls back to nearest when upscaling."""
    if nw >= w or nh >= h:
        return resize_nearest(w, h, px, nw, nh)
    out = []
    for y in range(nh):
        sy0 = y * h // nh
        sy1 = max(sy0 + 1, (y + 1) * h // nh)
        row = []
        for x in range(nw):
            sx0 = x * w // nw
            sx1 = max(sx0 + 1, (x + 1) * w // nw)
            ar = ag = ab = aa = 0
            n = 0
            for yy in range(sy0, sy1):
                for xx in range(sx0, sx1):
                    r, g, b, a = px[yy][xx]
                    ar += r * a; ag += g * a; ab += b * a; aa += a; n += 1
            if aa > 0:
                row.append((ar // aa, ag // aa, ab // aa, aa // n))
            else:
                row.append((KEY[0], KEY[1], KEY[2], 0))
        out.append(row)
    return nw, nh, out


def save_bmp(path, w, h, px):
    row = w * 3
    stride = (row + 3) & ~3
    total = 54 + stride * h
    out = bytearray(total)
    out[0:2] = b'BM'
    struct.pack_into('<I', out, 2, total)
    struct.pack_into('<I', out, 10, 54)
    struct.pack_into('<I', out, 14, 40)
    struct.pack_into('<i', out, 18, w)
    struct.pack_into('<i', out, 22, h)
    struct.pack_into('<H', out, 26, 1)
    struct.pack_into('<H', out, 28, 24)
    struct.pack_into('<I', out, 34, stride * h)
    p = 54
    for y in range(h - 1, -1, -1):   # bottom-up
        line = px[y]
        for x in range(w):
            r, g, b = line[x]
            out[p] = b; out[p + 1] = g; out[p + 2] = r; p += 3
        p += stride - row
    open(path, 'wb').write(out)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    inp, outp = argv[1], argv[2]
    size = None
    alpha_threshold = 128
    bg = None
    pad = False
    gray = False
    i = 3
    while i < len(argv):
        a = argv[i]
        if a == '--size':
            size = int(argv[i + 1]); i += 2
        elif a == '--alpha-threshold':
            alpha_threshold = int(argv[i + 1]); i += 2
        elif a == '--bg':
            bg = tuple(int(v) for v in argv[i + 1].split(',')); i += 2
        elif a == '--pad':
            pad = True; i += 1
        elif a == '--gray':
            gray = True; i += 1
        else:
            print('unknown option', a); return 1

    w, h, rgba = load_png(inp)
    if size:
        if pad and w != h:                      # letterbox to square first
            side = max(w, h)
            sq = [[(KEY[0], KEY[1], KEY[2], 0) for _ in range(side)] for _ in range(side)]
            ox, oy = (side - w) // 2, (side - h) // 2
            for y in range(h):
                for x in range(w):
                    sq[oy + y][ox + x] = rgba[y][x]
            w = h = side; rgba = sq
        w, h, rgba = resize_area(w, h, rgba, size, size)

    # flatten alpha / bg to the magenta key
    rgb = []
    keyed = 0
    for y in range(h):
        row = []
        for x in range(w):
            r, g, b, a = rgba[y][x]
            if a < alpha_threshold or (bg is not None and (r, g, b) == bg):
                row.append(KEY); keyed += 1
            elif gray:
                lum = (r * 30 + g * 59 + b * 11) // 100
                row.append((lum, lum, lum))
            else:
                row.append((r, g, b))
        rgb.append(row)
    save_bmp(outp, w, h, rgb)
    print('wrote %s (%dx%d, %d transparent px)' % (outp, w, h, keyed))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
