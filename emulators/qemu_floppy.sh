#!/bin/sh
# =====================================================================
#  qemu_floppy.sh - Boot the CastaliaOS bootable FreeDOS floppy in QEMU.
#
#  Build the floppy first with tools/make_dos_floppy.sh. Models the
#  Pentium II / 440BX target: i440FX PC, 128 MB, PS/2, standard VBE VGA.
#
#  Interactive:
#    emulators/qemu_floppy.sh [floppy.img]
#  Headless capture (writes a PNG of the desktop, needs Python 3):
#    emulators/qemu_floppy.sh [floppy.img] --shot out.png [--key esc]
#
#  A DOS mouse driver (e.g. CuteMouse CTMOUSE) is NOT included on the demo
#  floppy; load one for INT 33h mouse support, as on real DOS. Keyboard
#  (INT 16h) works with no driver.
# =====================================================================
IMG="${1:-build/castalia_floppy.img}"
shift 2>/dev/null || true

if [ ! -f "$IMG" ]; then
    echo "Floppy '$IMG' not found. Build it with tools/make_dos_floppy.sh." >&2
    exit 1
fi

SHOT=""; KEY=""
while [ $# -gt 0 ]; do
    case "$1" in
        --shot) SHOT="$2"; shift 2 ;;
        --key)  KEY="$2";  shift 2 ;;
        *) shift ;;
    esac
done

COMMON="-M pc -cpu pentium2 -m 128 -fda $IMG -boot a -vga std -rtc base=localtime -no-reboot"

if [ -n "$SHOT" ]; then
    SOCK="$(mktemp -u /tmp/castalia-qmp.XXXXXX.sock)"
    # shellcheck disable=SC2086
    qemu-system-i386 $COMMON -display none \
        -qmp "unix:$SOCK,server,nowait" >/tmp/qemu-castalia.log 2>&1 &
    QPID=$!
    KEYARG=""; [ -n "$KEY" ] && KEYARG="--key $KEY"
    python3 "$(dirname "$0")/../tools/qemu_screenshot.py" \
        --sock "$SOCK" --wait 35 --out "$SHOT" $KEYARG --quit
    kill "$QPID" 2>/dev/null || true
    rm -f "$SOCK"
else
    # shellcheck disable=SC2086
    exec qemu-system-i386 $COMMON -display gtk
fi
