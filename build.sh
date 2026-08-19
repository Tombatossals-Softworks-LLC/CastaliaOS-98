#!/bin/sh
# =====================================================================
#  build.sh - One-command host build, test, and screenshot for
#             CastaliaOS 98 PE.
#
#  This builds the PORTABLE stack with the headless host backend using a
#  normal C compiler, runs the unit tests, and renders a demo desktop to
#  build/castalia.bmp. It does NOT build the DOS product (that needs Open
#  Watcom: `wmake -f Makefile.dos`).
#
#  Usage:  ./build.sh [clean]
# =====================================================================
set -e

cd "$(dirname "$0")"

if [ "$1" = "clean" ]; then
    make clean
    exit 0
fi

echo "== Building host shell =="
make all

echo "== Running unit tests =="
make test

echo "== Rendering demo desktop =="
make run

echo ""
echo "Done."
echo "  Shell binary : build/castalia"
echo "  Screenshot   : build/castalia.bmp (800x600, open with any viewer)"
echo "  Log          : build/LOGS/castalia.log"
echo ""
echo "For the real DOS product, run under Open Watcom:  wmake -f Makefile.dos"
