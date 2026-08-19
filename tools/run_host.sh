#!/bin/sh
# =====================================================================
#  tools/run_host.sh - Build the host shell and render a demo desktop
#                      screenshot at a chosen resolution.
#
#  Usage:
#    tools/run_host.sh [WIDTH HEIGHT]
#  Examples:
#    tools/run_host.sh            # 800x600 (preferred mode)
#    tools/run_host.sh 640 480    # 640x480 fallback mode
#
#  Output: build/castalia.bmp (24-bit BMP, open with any image viewer).
# =====================================================================
set -e
cd "$(dirname "$0")/.."

W="${1:-800}"
H="${2:-600}"

make all
mkdir -p build/SYS build/LOGS
CASTALIA_HOME=build build/castalia \
    --headless --open-launcher --open-sysinfo \
    --width "$W" --height "$H" --frames 10 \
    --shot "build/castalia_${W}x${H}.bmp"

echo "Wrote build/castalia_${W}x${H}.bmp"
