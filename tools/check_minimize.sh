#!/bin/sh
# check_minimize.sh - a window with a taskbar button can be minimized.
#
# The taskbar's rule for what gets a button is exactly one line in
# sh_taskbar.c: everything that is not WM_STYLE_POPUP and not WM_STYLE_MODAL.
# Nothing tied that to WM_STYLE_MINIMIZE, and six windows had drifted apart
# from it -- Calculator, Mines, Reversi, Properties, the Welcome tour, and the
# generic text window that hosts System Information, the Log Viewer and the
# rest. Each had a taskbar button and no way to answer it.
#
# What that looks like to somebody using it is a DEAD CLICK, not a missing
# feature. Clicking the button of the window you are already in has put that
# window away since the taskbar was invented; sh_taskbar.c does exactly that,
# guarded by WM_STYLE_MINIMIZE, and without the bit the click falls through to
# "focus it and bring it to the front" -- which it already is. So nothing
# happens, twice, and the third time you assume the mouse is broken.
#
# It is also the failure that hides an animation: minimize and restore fly the
# window down to its button and back, and both are started only when the
# window can be minimized at all. Six windows had no such animation and
# nothing said so.
#
# The rule, then, is the taskbar's own: a window that is neither a popup nor
# modal, and has a title bar, must have WM_STYLE_MINIMIZE. WM_STYLE_APP is the
# shorthand that includes it and is the right thing to use.
#
# Exemptions are listed by name below, each with the reason, because a silent
# exemption is a hole this check cannot see and a named one is an argument
# somebody can disagree with. There are none today.
#
# Usage:  sh tools/check_minimize.sh   (run by: make lint)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

bad=0
scanned=0
titled=0
for f in $(find "$ROOT/src" -name '*.c' | sort); do
    base=$(basename "$f")
    case "$base" in
        # No exemptions. When there is one, it goes here with its reason.
        '') continue ;;
    esac
    grep -q 'wm_create' "$f" 2>/dev/null || continue
    scanned=$((scanned + 1))

    # Each wm_create call, flattened onto one line, so a style list broken
    # across lines reads the same as one that is not.
    # A real call, not a comment that mentions one. Two files talk about
    # wm_create in prose above the function; requiring an ARGUMENT after the
    # paren is what tells "wm_create(title, &frame, ..." from "wm_create()
    # takes the whole frame rect".
    out=$(awk '
        /wm_create[ ]*\(/ { inw = 1; buf = "" }
        inw               { buf = buf " " $0 }
        inw && /;/        {
            gsub(/  +/, " ", buf)
            if (buf ~ /wm_create[ ]*\([^)]/) { print buf }
            inw = 0; buf = ""
        }
    ' "$f" | while IFS= read -r call; do
        case "$call" in
            *WM_STYLE_POPUP*|*WM_STYLE_MODAL*) continue ;;
            *WM_STYLE_APP*)                    continue ;;
            *WM_STYLE_TITLE*)                  ;;
            *)                                 continue ;;
        esac
        case "$call" in
            *WM_STYLE_MINIMIZE*) ;;
            *) echo "$call" ;;
        esac
    done)

    # Count the titled, non-popup windows we actually looked at, so a scan
    # that stopped recognising them cannot pass everything.
    n=$(awk '
        /wm_create[ ]*\(/ { inw = 1; buf = "" }
        inw               { buf = buf " " $0 }
        inw && /;/        {
            if (buf ~ /wm_create[ ]*\([^)]/) { print buf }
            inw = 0; buf = ""
        }
    ' "$f" | grep -c 'WM_STYLE_TITLE\|WM_STYLE_APP' || true)
    titled=$((titled + n))

    if [ -n "$out" ]; then
        echo "  MINIMIZE: $base creates a window with a title bar and a"
        echo "            taskbar button but no WM_STYLE_MINIMIZE. Clicking"
        echo "            that button while the window is focused does"
        echo "            nothing at all. Use WM_STYLE_APP, or add the flag:"
        echo "$out" | cut -c1-96 | sed 's/^/              /'
        bad=$((bad + 1))
    fi
done

# A check that scanned nothing passes everything.
if [ "$scanned" -lt 15 ] || [ "$titled" -lt 15 ]; then
    echo "  MINIMIZE: only $scanned source(s) create windows, $titled of them" >&2
    echo "            titled -- the tree moved and this check stopped looking" >&2
    exit 1
fi

if [ "$bad" -gt 0 ]; then
    echo "MINIMIZE CHECK FAILED: $bad file(s)"
    exit 1
fi
echo "Minimize OK: $titled titled window(s) across $scanned source(s), and"
echo "             every one that gets a taskbar button can answer it."
