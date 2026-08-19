#!/usr/bin/env python3
"""frames_to_webm.py - assemble recorder BMP frames into a WebM (VP8) video.

The bundled ffmpeg only decodes MJPEG, so we re-encode each frame to JPEG and
feed them through the image2pipe demuxer. Small, high-quality, plays in any
modern browser / Slack / social embed.

Usage: frames_to_webm.py FRAMES_DIR OUT.webm [fps] [bitrate]
Env:   FF=/path/to/ffmpeg   (defaults to the Playwright build)
"""
import sys, os, glob, io, subprocess
from PIL import Image

FF = os.environ.get("FF", "/opt/pw-browsers/ffmpeg-1011/ffmpeg-linux")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    d, out = sys.argv[1], sys.argv[2]
    fps = sys.argv[3] if len(sys.argv) > 3 else "24"
    br = sys.argv[4] if len(sys.argv) > 4 else "1800k"
    files = sorted(glob.glob(os.path.join(d, "f*.bmp")))
    if not files:
        print("no frames in", d)
        sys.exit(1)
    p = subprocess.Popen(
        [FF, "-y", "-f", "image2pipe", "-vcodec", "mjpeg", "-framerate", fps,
         "-i", "pipe:0", "-c:v", "libvpx", "-b:v", br, "-pix_fmt", "yuv420p", out],
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for f in files:
        b = io.BytesIO()
        Image.open(f).convert("RGB").save(b, "JPEG", quality=95)
        p.stdin.write(b.getvalue())
    p.stdin.close()
    rc = p.wait()
    if rc != 0 or not os.path.exists(out):
        print("ffmpeg failed (rc=%d)" % rc)
        sys.exit(1)
    print("wrote %s  (%d KB)" % (out, os.path.getsize(out) // 1024))


if __name__ == "__main__":
    main()
