#!/bin/sh
# run_memcheck.sh - look for memory errors that no other check in this tree can see.
#
# Everything else here proves behaviour: the unit tests prove the logic, the
# demos prove the wiring, the abuse worlds prove nothing hostile brings the
# shell down. None of them can see an invalid read, a write one byte past a
# buffer, or a branch on an uninitialised value -- a program does all three
# and still produces exactly the right answer, right up until the day it does
# not. This is C89 with hand-rolled parsers for four file formats and its own
# allocator, which is precisely the shape where those live.
#
# The targets are chosen rather than exhaustive, because valgrind costs about
# forty times native and a full sweep of 49 scenes would stop anyone running
# it. What is here is where the risk actually is:
#
#   - the unit tests, which walk every pure module including the parsers
#   - the scenes that read a FILE FORMAT this project wrote itself
#     (.CAR, .CZ, BMP, WAV) and the ones that walk a directory tree
#   - the network stack, which parses frames off a simulated wire
#   - hostile worlds, where the input is deliberately malformed
#
# A run that reports zero is only worth having if it could have reported more
# than zero, so tools/check_memcheck_control.sh exists to prove that -- it
# plants a one-byte overrun and requires this script to catch it.
#
# Usage:  sh tools/run_memcheck.sh        (or: make memcheck)
# Env:    BIN=/path/to/castalia
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${BIN:-$ROOT/build/castalia}"
case "$BIN" in /*) ;; *) BIN="$ROOT/$BIN" ;; esac
TESTS="$ROOT/build/run_tests"
WORK="$ROOT/build/memcheck-home"

if ! command -v valgrind >/dev/null 2>&1; then
    echo "memcheck: valgrind is not installed -- skipping (not a failure)."
    exit 0
fi
if [ ! -x "$BIN" ]; then
    echo "memcheck: $BIN is missing -- run make first" >&2
    exit 1
fi

# --leak-check=no is deliberate, and worth saying out loud because the line
# this script prints at the end is easy to read as more than it claims. These
# runs check invalid access and uninitialised reads. They do NOT check leaks,
# and they never have; leaks are covered by tools/run_sanitize.sh
# (LeakSanitizer) and by the accounted-allocator scenes in main.c, which is
# where a 12 KB canvas abandoned by tests/test_paint.c was eventually found.
#
# Nor does valgrind see everything ASan sees even within its own remit: it
# puts no redzones around globals, so a read one byte past a string literal
# is invisible to all thirteen of these runs. calc_press() had exactly that,
# and it took ASan to find it.
VG="valgrind --tool=memcheck --leak-check=no --track-origins=yes --quiet"
fails=0
runs=0

# Report a valgrind run. Zero errors passes; anything else prints what it saw.
check() {
    label="$1"; what="$2"; shift 2
    runs=$((runs + 1))
    out="$("$@" 2>&1 || true)"
    n=$(printf '%s\n' "$out" | grep -cE '^==[0-9]+== (Invalid|Conditional|Use of|Syscall|Mismatched|Source and destination)' || true)
    if [ "$n" -gt 0 ]; then
        printf '  FAIL  %-16s %s -- %s finding(s)\n' "$label" "$what" "$n"
        printf '%s\n' "$out" | grep -E '^==[0-9]+==' | head -20 | sed 's/^/        /'
        fails=$((fails + 1))
    else
        printf '  ok    %-16s %s\n' "$label" "$what"
    fi
}

make_world() {
    rm -rf "$WORK"
    mkdir -p "$WORK/DOCS" "$WORK/SYS" "$WORK/MEDIA" "$WORK/PHOTOS"
    printf 'hello\nworld\n' > "$WORK/NOTES.TXT"
    printf 'A,B\n1,2\n' > "$WORK/BUDGET.CSV"
    printf 'CWRITE1\n.P 0\nHello\n' > "$WORK/LETTER.DOC"
    cp "$ROOT/assets/icons/tango/computer.bmp" "$WORK/PHOTO.BMP" 2>/dev/null || true
    for w in "$ROOT/dist/cdroot/CASTALIA/MEDIA/"*.WAV; do
        [ -f "$w" ] || continue
        cp "$w" "$WORK/SONG.WAV"; cp "$w" "$WORK/MEDIA/"
    done
}

scene() {
    make_world
    check "$1" "$2" env CASTALIA_HOME="$WORK" $VG "$BIN" --headless \
          "--$1" --frames 8 --shot /dev/null
}

# The pure modules, every one of them, in one run.
if [ -x "$TESTS" ]; then
    check unit-tests "every pure module, including the parsers" $VG "$TESTS"
fi

# Formats this project wrote itself, and the trees it walks.
scene car-demo     "the .CAR archiver, packing and unpacking"
scene cz-demo      "the .CZ compressor, round-tripping a file"
scene archive-demo "listing an archive without unpacking it"
scene hex-demo     "reading a window of a file, not the file"
scene paint-demo   "the BMP round trip, and the canvas"
scene repaint-demo "the WAV decoder behind the media deck"
scene diskuse-demo "walking a directory tree and sizing it"
scene net-demo     "frames parsed off the simulated wire"

# Deliberately malformed input, where a parser is most likely to step out of
# its buffer. These worlds mirror tools/run_abuse.sh.
hostile() {
    name="$1"; what="$2"
    home="$WORK-$name"
    rm -rf "$home"; mkdir -p "$home/SYS" "$home/DOCS"
    case "$name" in
        garbage_ini)
            head -c 3000 /dev/urandom > "$home/SYS/CASTALIA.INI" 2>/dev/null ||
                printf '\001\002\003\377' > "$home/SYS/CASTALIA.INI" ;;
        truncated_ini) printf '[Shell]\nAnim' > "$home/SYS/CASTALIA.INI" ;;
        bad_wallpaper)
            printf 'BM\377\377\377\377\000\000\000\000' > "$home/BROKEN.BMP"
            printf '[Shell]\nWallpaper=BROKEN.BMP\nWallpaperMode=3\n' \
                > "$home/SYS/CASTALIA.INI" ;;
        lying_archive)
            printf 'CZ\001\000\377\377\377\177garbage' > "$home/BIG.CZ" ;;
    esac
    runs=$((runs + 1))
    out="$(cd "$home" && env CASTALIA_HOME="$home" $VG "$BIN" --headless \
           --screen-check --frames 6 --shot /dev/null 2>&1 || true)"
    n=$(printf '%s\n' "$out" | grep -cE '^==[0-9]+== (Invalid|Conditional|Use of|Syscall|Mismatched|Source and destination)' || true)
    if [ "$n" -gt 0 ]; then
        printf '  FAIL  %-16s %s -- %s finding(s)\n' "$name" "$what" "$n"
        printf '%s\n' "$out" | grep -E '^==[0-9]+==' | head -20 | sed 's/^/        /'
        fails=$((fails + 1))
    else
        printf '  ok    %-16s %s\n' "$name" "$what"
    fi
    rm -rf "$home"
}

hostile garbage_ini    "a config of random bytes"
hostile truncated_ini  "a config cut off mid-key"
hostile bad_wallpaper  "a bitmap that is one only by its name"
hostile lying_archive  "a .CZ claiming two gigabytes"

rm -rf "$WORK"
if [ "$fails" -gt 0 ]; then
    echo "MEMCHECK FAILED: $fails of $runs"
    exit 1
fi
echo "Memcheck OK: $runs runs, no invalid access and no uninitialised reads."
