#!/bin/sh
# =====================================================================
#  smoke_test.sh - Boot the kernel-lab image in QEMU and assert it lives.
#
#  Boots castalia-klab.img headless, dumps VGA text memory twice over the
#  QEMU monitor, and checks that (1) the PIT (IRQ0) tick counter advanced on
#  its own and (2) an injected keypress produced a scancode (IRQ1). This is
#  the milestone-4 verification, scripted so CI runs it on every push.
#
#  Requires: qemu-system-i386, python3, and a built kernel-lab/castalia-klab.img
#  (run `make -C kernel-lab image` first).
# =====================================================================
set -e
DIR=$(dirname "$0")
IMG="$DIR/castalia-klab.img"
SOCK=/tmp/klab-mon.$$.sock
V1=/tmp/klab-vga1.$$.bin
V2=/tmp/klab-vga2.$$.bin

[ -f "$IMG" ] || { echo "missing $IMG (run: make -C kernel-lab image)"; exit 1; }
rm -f "$SOCK" "$V1" "$V2"

qemu-system-i386 -fda "$IMG" -display none \
    -monitor "unix:$SOCK,server,nowait" -no-reboot >/dev/null 2>&1 &
QPID=$!
trap 'kill $QPID 2>/dev/null || true; rm -f "$SOCK" "$V1" "$V2"' EXIT

# Wait for the monitor socket, then let the kernel run.
i=0
while [ ! -S "$SOCK" ] && [ $i -lt 50 ]; do sleep 0.1; i=$((i + 1)); done
sleep 2

SOCK="$SOCK" V1="$V1" V2="$V2" python3 - <<'PY'
import os, socket, time
s = socket.socket(socket.AF_UNIX); s.connect(os.environ['SOCK']); time.sleep(0.2)
def cmd(c): s.sendall((c + '\n').encode()); time.sleep(0.3); return s.recv(65536)
cmd('pmemsave 0xb8000 4000 "%s"' % os.environ['V1'])
time.sleep(1.0)
cmd('sendkey a'); time.sleep(0.3)
cmd('pmemsave 0xb8000 4000 "%s"' % os.environ['V2'])
s.close()
PY

V1="$V1" V2="$V2" python3 - <<'PY'
import os
def tick(p):
    d = open(p, 'rb').read(); b = 320 + 13 * 2
    return int(''.join(chr(d[b + i * 2]) for i in range(4)), 16)
def scan(p):
    d = open(p, 'rb').read(); b = 480 + 15 * 2
    return ''.join(chr(d[b + i * 2]) for i in range(2))
t1 = tick(os.environ['V1']); t2 = tick(os.environ['V2']); sc = scan(os.environ['V2'])
print("kernel-lab: ticks %04X -> %04X, scancode=%s" % (t1, t2, sc))
assert t2 > t1, "PIT IRQ0 tick did not advance (%04X -> %04X)" % (t1, t2)
assert sc.upper() in ("1E", "9E"), "keyboard IRQ1 scancode wrong: %s" % sc
print("kernel-lab smoke test PASSED (IRQ0 timer + IRQ1 keyboard live)")
PY
