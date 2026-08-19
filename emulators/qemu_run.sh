#!/bin/sh
# =====================================================================
#  qemu_run.sh - Boot a FreeDOS + CastaliaOS disk image in QEMU for a
#                quick smoke test on Linux/macOS.
#
#  Prerequisites (see docs/TESTING.md):
#    - qemu-system-i386
#    - A FAT16/FAT32 hard-disk image 'castalia.img' with FreeDOS installed,
#      the CastaliaOS files copied to C:\CASTALIA, and AUTOEXEC.BAT calling
#      CBOOT (use dist/AUTOEXEC.BAT as the template).
#
#  This models the IBM Pentium II / 440BX target: i440FX PC, 128 MB RAM,
#  PS/2 input, IDE disk, VBE-capable VGA. Sound is optional and off by
#  default (the desktop must never require it).
#
#  Usage:  ./qemu_run.sh [disk-image]   (default: castalia.img)
# =====================================================================
IMG="${1:-castalia.img}"

if [ ! -f "$IMG" ]; then
    echo "Disk image '$IMG' not found."
    echo "Build one per docs/TESTING.md (FreeDOS + C:\\CASTALIA + AUTOEXEC)."
    exit 1
fi

exec qemu-system-i386 \
    -M pc \
    -cpu pentium2 \
    -m 128 \
    -hda "$IMG" \
    -boot c \
    -vga std \
    -display gtk \
    -serial stdio
# Add '-device sb16' for optional Sound Blaster testing.
# '-vga std' provides a Bochs/standard VBE BIOS; try '-vga cirrus' as a
# second data point closer to period S3-class behavior.
