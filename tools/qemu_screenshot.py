#!/usr/bin/env python3
"""
qemu_screenshot.py - Drive a headless QEMU over QMP: wait for boot, optionally
press keys, and dump the VGA framebuffer to PNG. Used to capture CastaliaOS
running on FreeDOS without a display (CI / documentation).

Usage:
    qemu_screenshot.py --sock /tmp/qmp.sock --wait 35 --out shot.png [--key esc]

Start QEMU first with, e.g.:
    qemu-system-i386 -M pc -cpu pentium2 -m 128 -fda castalia_floppy.img -boot a \\
        -vga std -display none -qmp unix:/tmp/qmp.sock,server,nowait -no-reboot

Only depends on the Python standard library (socket, json, zlib).
"""
import argparse, socket, json, time, struct, zlib, os, sys


def ppm_to_png(ppm_path, png_path):
    d = open(ppm_path, "rb").read()
    if d[:2] != b"P6":
        raise ValueError("not a P6 PPM")
    idx, fields = 2, []
    while len(fields) < 3:
        while d[idx] in b" \t\n\r":
            idx += 1
        st = idx
        while d[idx] not in b" \t\n\r":
            idx += 1
        fields.append(int(d[st:idx]))
    w, h, _mx = fields
    idx += 1
    px = d[idx:idx + w * h * 3]
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += px[y * w * 3:(y + 1) * w * 3]

    def chunk(t, dat):
        c = t + dat
        return struct.pack(">I", len(dat)) + c + struct.pack(">I", zlib.crc32(c) & 0xffffffff)

    out = b"\x89PNG\r\n\x1a\n"
    out += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    out += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    out += chunk(b"IEND", b"")
    open(png_path, "wb").write(out)
    return w, h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sock", default="/tmp/qmp.sock")
    ap.add_argument("--wait", type=float, default=35.0, help="seconds to wait for boot")
    ap.add_argument("--out", default="castalia_qemu.png")
    ap.add_argument("--key", default=None, help="QMP qcode to send before the shot (e.g. esc)")
    ap.add_argument("--quit", action="store_true", help="quit QEMU after capture")
    args = ap.parse_args()

    s = socket.socket(socket.AF_UNIX)
    for _ in range(120):
        try:
            s.connect(args.sock)
            break
        except OSError:
            time.sleep(0.5)
    else:
        print("could not connect to QMP socket", file=sys.stderr)
        return 1
    f = s.makefile("rwb", buffering=0)

    def send(d):
        f.write((json.dumps(d) + "\r\n").encode())

    def wait_return(timeout=20):
        end = time.time() + timeout
        while time.time() < end:
            line = f.readline()
            if not line:
                return None
            try:
                m = json.loads(line)
            except ValueError:
                continue
            if "return" in m or "error" in m:
                return m
        return None

    f.readline()  # QMP greeting
    send({"execute": "qmp_capabilities"})
    wait_return()
    print("waiting %.0fs for boot..." % args.wait)
    time.sleep(args.wait)

    if args.key:
        send({"execute": "send-key",
              "arguments": {"keys": [{"type": "qcode", "data": args.key}]}})
        wait_return()
        time.sleep(3)

    ppm = args.out + ".ppm"
    if os.path.exists(ppm):
        os.remove(ppm)
    send({"execute": "screendump", "arguments": {"filename": ppm}})
    r = wait_return()
    if r is None or "error" in r:
        print("screendump failed:", r, file=sys.stderr)
        return 1
    w, h = ppm_to_png(ppm, args.out)
    os.remove(ppm)
    print("wrote %s (%dx%d)" % (args.out, w, h))

    if args.quit:
        send({"execute": "quit"})
        time.sleep(0.5)
    return 0


if __name__ == "__main__":
    sys.exit(main())
