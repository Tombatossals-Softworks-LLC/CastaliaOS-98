#!/bin/sh
# check_clip.sh - inside a window's paint, the clip may only ever shrink.
#
# gfx_set_clip REPLACES the clip. The window manager sets one before it sends
# WM_MSG_PAINT, and that clip is the only thing keeping an app's drawing inside
# its own window -- so a control that saves the clip, replaces it with its own
# rectangle and puts it back has, for the length of that draw, no window at all.
#
# ui_draw_button did exactly that for every label it drew. Repainting a small
# region over a window that happens to be BEHIND another one -- which is what
# every mouse move does, since the cursor's old and new rectangles are the
# region -- then painted that window's button text across whatever was in
# front. Measured before the fix: 385 pixels of a Calculator's key labels
# scattered through an open Notepad, from nothing but moving the mouse. It is
# invisible in a screenshot of one window and it never looks like a clipping
# bug; it looks like faint speckle that comes and goes.
#
# gfx_clip_narrow is the same save-and-restore with an intersection instead of
# a replacement, so this refuses the pattern it replaced: no gfx_get_clip
# outside the graphics kernel itself. Reaching for it is the sign somebody is
# rebuilding the save-then-replace idiom by hand.
#
# The shell's own layer painters (taskbar, desktop, launcher, menus) still call
# gfx_set_clip and should: they run between wm_paint and the cursor, with no
# window clip in force, and each computes its own area and resets afterwards.
# They are the authority over their layer, which is the case gfx_set_clip is
# for.
#
# Usage:  sh tools/check_clip.sh   (run by: make lint)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

bad=0
scanned=0
for f in $(find "$ROOT/src" -name '*.c' | sort); do
    case "$f" in
        */src/gfx/*) continue ;;      # the kernel that implements it
    esac
    scanned=$((scanned + 1))
    if grep -n 'gfx_get_clip' "$f" >/dev/null 2>&1; then
        echo "  CLIP: $(basename "$f") calls gfx_get_clip -- use"
        echo "        gfx_clip_narrow(s, &r), which intersects with the clip"
        echo "        already in force instead of replacing it:"
        grep -n 'gfx_get_clip' "$f" | sed 's/^/          /'
        bad=$((bad + 1))
    fi
done

# A check that scanned nothing passes everything.
if [ "$scanned" -lt 50 ]; then
    echo "  CLIP: only $scanned sources scanned -- the tree moved" >&2
    exit 1
fi

if [ "$bad" -gt 0 ]; then
    echo "CLIP CHECK FAILED: $bad file(s)"
    exit 1
fi
echo "Clip OK: $scanned sources outside src/gfx, none of them replaces"
echo "         a clip it did not set."
