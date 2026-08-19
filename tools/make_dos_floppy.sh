#!/bin/sh
# =====================================================================
#  make_dos_floppy.sh - Assemble a single bootable FreeDOS floppy image that
#  boots straight into the CastaliaOS 98 PE desktop, for QEMU/Bochs testing.
#
#  This is the exact, reproducible recipe used to verify the DOS/VESA path
#  end-to-end (see docs/TESTING.md). It needs:
#    - The built DOS executables (run `wmake -f Makefile.dos` first):
#        dist/cdroot/CASTALIA/BIN/castalia.exe
#        dist/cdroot/CASTALIA/BIN/cboot.exe
#    - Your Open Watcom DOS/4GW extender: $WATCOM/binw/dos4gw.exe
#    - A FreeDOS boot floppy image (1.44 MB), e.g. 144m/x86BOOT.img from the
#      FreeDOS 1.3 FloppyEdition. You supply this; it is not redistributed here.
#    - mtools (mcopy/mmd/mdel) on the build host.
#
#  Nothing Microsoft is involved, and FreeDOS + DOS4GW.EXE are the user's own
#  legally obtained files -- see LEGAL.md / THIRD_PARTY_NOTICES.md.
#
#  Usage:
#    tools/make_dos_floppy.sh <freedos-boot.img> [out.img]
#  Example:
#    WATCOM=/opt/watcom tools/make_dos_floppy.sh x86BOOT.img build/castalia_floppy.img
# =====================================================================
set -e
cd "$(dirname "$0")/.."

FDBOOT="$1"
OUT="${2:-build/castalia_floppy.img}"
: "${WATCOM:?set WATCOM to your Open Watcom install (for binw/dos4gw.exe)}"
DOS4GW="$WATCOM/binw/dos4gw.exe"
CEXE="dist/cdroot/CASTALIA/BIN/castalia.exe"
CBOOT="dist/cdroot/CASTALIA/BIN/cboot.exe"

if [ -z "$FDBOOT" ] || [ ! -f "$FDBOOT" ]; then
    echo "usage: $0 <freedos-boot.img> [out.img]" >&2
    echo "  <freedos-boot.img>: a 1.44MB bootable FreeDOS floppy (e.g. x86BOOT.img)" >&2
    exit 1
fi
for f in "$DOS4GW" "$CEXE" "$CBOOT"; do
    [ -f "$f" ] || { echo "missing: $f (build the DOS target first?)" >&2; exit 1; }
done
command -v mcopy >/dev/null || { echo "mtools not found (apt install mtools)" >&2; exit 1; }

mkdir -p "$(dirname "$OUT")"
cp "$FDBOOT" "$OUT"

# Boot config: no language menu, run our autoexec straight away.
tmp_cfg="$(mktemp)"; tmp_auto="$(mktemp)"
cat > "$tmp_cfg" <<'EOF'
LASTDRIVE=Z
FILES=40
BUFFERS=20
SHELL=\FREEDOS\BIN\COMMAND.COM \FREEDOS\BIN /E:2048 /P=\FDAUTO.BAT
EOF
# Optional DOS mouse driver (INT 33h). Point CTMOUSE at a driver .EXE (e.g.
# FreeDOS CuteMouse CTMOUSE.EXE) to load it before the desktop so the mouse
# works. Verified with CuteMouse `CTMOUSE.EXE /P` in QEMU.
MOUSE_LINE=""
if [ -n "${CTMOUSE:-}" ] && [ -f "$CTMOUSE" ]; then
    MOUSE_LINE="CTMOUSE.EXE /P"
fi

{
    printf '%s\n' '@echo off'
    printf '%s\n' 'SET PATH=A:\CASTALIA\BIN;A:\FREEDOS\BIN'
    printf '%s\n' 'SET CASTALIA_HOME=A:\CASTALIA'
    printf '%s\n' 'CD \CASTALIA\BIN'
    [ -n "$MOUSE_LINE" ] && { printf '%s\n' 'echo Loading mouse driver...'; printf '%s\n' "$MOUSE_LINE"; }
    printf '%s\n' 'echo Starting CastaliaOS 98 PE...'
    printf '%s\n' 'REM CBOOT.EXE   (use CBOOT for the full safe-launcher path)'
    printf '%s\n' 'CASTALIA.EXE'
} > "$tmp_auto"

M="mcopy -i $OUT"
mdel -i "$OUT" ::FDCONFIG.SYS 2>/dev/null || true
mdel -i "$OUT" ::FDAUTO.BAT   2>/dev/null || true
mdel -i "$OUT" ::SETUP.BAT    2>/dev/null || true
$M "$tmp_cfg"  ::FDCONFIG.SYS
$M "$tmp_auto" ::FDAUTO.BAT
rm -f "$tmp_cfg" "$tmp_auto"

for d in CASTALIA CASTALIA/BIN CASTALIA/SYS CASTALIA/LOGS; do
    mmd -i "$OUT" "::$d" 2>/dev/null || true
done
$M "$CEXE"   ::CASTALIA/BIN/CASTALIA.EXE
$M "$CBOOT"  ::CASTALIA/BIN/CBOOT.EXE
$M "$DOS4GW" ::CASTALIA/BIN/DOS4GW.EXE
$M dist/cdroot/CASTALIA/SYS/CASTALIA.INI ::CASTALIA/SYS/CASTALIA.INI
if [ -n "$MOUSE_LINE" ]; then
    $M "$CTMOUSE" ::CASTALIA/BIN/CTMOUSE.EXE
    echo "  (bundled mouse driver: $CTMOUSE)"
fi

echo "Built bootable floppy: $OUT"
echo "Boot it with: emulators/qemu_floppy.sh $OUT"
[ -z "$MOUSE_LINE" ] && echo "Tip: set CTMOUSE=/path/to/CTMOUSE.EXE for INT 33h mouse support."
