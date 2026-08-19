#!/usr/bin/env python3
"""frames_to_gif.py - assemble recorder BMP frames into an animated GIF.

The recorder (`castalia --headless --record DIR --scene N`) writes fNNNNN.bmp
frames, one per shell frame. This packs them into a single looping GIF with a
shared 256-colour palette (no inter-frame palette flicker), Floyd-Steinberg
dithering for the XP-style gradients, and consecutive-duplicate coalescing so
the long "wait" holds do not bloat the file.

Usage:
  frames_to_gif.py FRAMES_DIR OUT.gif [stride] [ms_per_frame] [WxH]
    stride         keep every Nth frame (default 1)
    ms_per_frame   base delay per kept frame (default 55)
    WxH            scale, e.g. 640x480 (default: native)
"""
import sys, os, glob
from PIL import Image


def load_frames(d, stride, scale):
    files = sorted(glob.glob(os.path.join(d, "f*.bmp")))[::stride]
    out = []
    for f in files:
        im = Image.open(f).convert("RGB")
        if scale:
            im = im.resize(scale, Image.LANCZOS)
        out.append(im)
    return out


def shared_palette(frames, colors=256):
    n = min(len(frames), 24)
    idxs = [int(i * (len(frames) - 1) / (n - 1)) for i in range(n)] if n > 1 else [0]
    w, h = frames[0].size
    sheet = Image.new("RGB", (w, h * len(idxs)))
    for j, i in enumerate(idxs):
        sheet.paste(frames[i], (0, h * j))
    return sheet.quantize(colors=colors, method=Image.MEDIANCUT)


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    d, out = sys.argv[1], sys.argv[2]
    stride = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    dur = int(sys.argv[4]) if len(sys.argv) > 4 else 55
    scale = None
    if len(sys.argv) > 5:
        sw, sh = sys.argv[5].lower().split("x")
        scale = (int(sw), int(sh))

    frames = load_frames(d, stride, scale)
    if not frames:
        print("no frames in", d)
        sys.exit(1)
    pal = shared_palette(frames)
    q = [f.quantize(palette=pal, dither=Image.FLOYDSTEINBERG) for f in frames]

    kept, durs, prev = [], [], None
    for qf in q:
        data = qf.tobytes()
        if prev is not None and data == prev:
            durs[-1] += dur
        else:
            kept.append(qf)
            durs.append(dur)
            prev = data

    kept[0].save(out, save_all=True, append_images=kept[1:],
                 duration=durs, loop=0, optimize=True, disposal=2)
    kb = os.path.getsize(out) // 1024
    print("wrote %s  (%d unique frames, %d KB)" % (out, len(kept), kb))


if __name__ == "__main__":
    main()
