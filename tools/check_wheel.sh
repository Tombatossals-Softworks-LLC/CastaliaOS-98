#!/bin/sh
# check_wheel.sh - a window that scrolls must answer the mouse wheel.
#
# The wheel arrived after nineteen windows already scrolled, so it was added
# to them one at a time -- and "one at a time" is exactly how the twentieth
# gets forgotten. A wheel that works in every list except one is worse than a
# wheel that works nowhere: the exception reads as a broken window rather than
# as a missing feature, and nobody reports it because they assume they are
# doing it wrong.
#
# The test for "this window scrolls" is that it keeps a scroll offset it walks
# a list or a document with. Two spellings cover every one in the tree: a
# member named top / scroll / view, or the spreadsheet's TOPR() accessor over
# its per-sheet array. Either of those in a file that also creates a window
# means the wheel belongs there.
#
# Exemptions are listed by name below, each with the reason, because a silent
# exemption is a hole this check cannot see and a named one is an argument
# somebody can disagree with.
#
# Usage:  sh tools/check_wheel.sh   (run by: make lint)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

bad=0
scanned=0
scrolls=0
for f in $(find "$ROOT/src" -name '*.c' | sort); do
    base=$(basename "$f")
    case "$base" in
        # The scroll-bar control itself: it computes positions, it does not
        # own one.
        ui_scroll.c) continue ;;
        # The single-line text edit scrolls its own text sideways by a
        # character. A wheel over a name field is not a request to scroll it.
        ui_edit.c) continue ;;
        # ui_controls.c draws a list box for callers who hand it a position;
        # the window that OWNS that position is the one the wheel reaches.
        ui_controls.c) continue ;;
        # The desktop's icon "view" is which virtual desktop is showing, not a
        # scroll offset -- and a wheel over the wallpaper switching desktops
        # would be a surprise, not a feature.
        sh_desktop.c) continue ;;
    esac

    # Does it own a window at all? A pure core does not.
    grep -q 'wm_create' "$f" 2>/dev/null || continue
    scanned=$((scanned + 1))

    if grep -Eq '(->top\b|->scroll\b|->view\b|TOPR\()' "$f" 2>/dev/null; then
        scrolls=$((scrolls + 1))
        if ! grep -q 'WM_MSG_MOUSEWHEEL' "$f" 2>/dev/null; then
            echo "  WHEEL: $base keeps a scroll offset but never answers"
            echo "         WM_MSG_MOUSEWHEEL. Handle it -- ui_scroll_wheel()"
            echo "         is the shared rule (three lines a notch, clamped,"
            echo "         and CTRUE only when something actually moved):"
            grep -nE '(->top\b|->scroll\b|->view\b|TOPR\()' "$f" \
                | head -3 | sed 's/^/           /'
            bad=$((bad + 1))
        fi
    fi
done

# A check that scanned nothing passes everything.
if [ "$scanned" -lt 15 ] || [ "$scrolls" -lt 10 ]; then
    echo "  WHEEL: only $scanned window source(s), $scrolls of them scrolling" >&2
    echo "         -- the tree moved and this check stopped looking" >&2
    exit 1
fi

if [ "$bad" -gt 0 ]; then
    echo "WHEEL CHECK FAILED: $bad window(s)"
    exit 1
fi
echo "Wheel OK: $scrolls of $scanned window sources scroll, and every one"
echo "          of them answers the wheel."
