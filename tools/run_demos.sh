#!/bin/sh
# run_demos.sh - run every scripted end-to-end demo and fail on any MISMATCH.
#
# `make test` proves the pure logic. This proves the WIRING: that a click in a
# real window reaches the real store and the real file on disk. Each demo in
# src/main.c drives a scene through synthetic input and then checks the result
# it can actually observe -- the pixels in a saved bitmap, the bytes in a saved
# file, the frames that came back off the simulated wire -- and logs "(OK)" or
# "(MISMATCH)". This script runs them all and reports the first kind as a pass
# and the second as a failure.
#
# It also refuses a demo that prints nothing at all, because a demo whose
# scene silently stopped matching the UI would otherwise look like a pass.
#
# Everything runs against a throwaway CASTALIA_HOME, so a run leaves the
# working tree exactly as it found it.
#
# Usage:  sh tools/run_demos.sh        (or: make demos)
# Env:    BIN=/path/to/castalia
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${BIN:-$ROOT/build/castalia}"
case "$BIN" in
    /*) ;;                       # already absolute
    *)  BIN="$ROOT/$BIN" ;;      # each scene runs from a temp cwd
esac
HOME_DIR="$ROOT/build/demo-home"

if [ ! -x "$BIN" ]; then
    echo "run_demos: $BIN is missing -- run make first" >&2
    exit 1
fi

rm -f "$ROOT/build/.demo-fails"

# A small world for the scenes that browse or open files. It is rebuilt before
# EVERY scene: several demos save into it (a bitmap, an agenda, a theme), and a
# leftover file would change what the next scene's keyboard walk lands on.
make_world() {
    rm -rf "$HOME_DIR"
    mkdir -p "$HOME_DIR/DOCS" "$HOME_DIR/SYS" "$HOME_DIR/MEDIA" "$HOME_DIR/PHOTOS"
    printf 'hello\n' > "$HOME_DIR/NOTES.TXT"
    printf 'A,B\n1,2\n' > "$HOME_DIR/BUDGET.CSV"
    printf 'CWRITE1\n.P 0\nHello\n' > "$HOME_DIR/LETTER.DOC"
    cp "$ROOT/assets/icons/tango/computer.bmp" "$HOME_DIR/PHOTO.BMP" 2>/dev/null || true
    # A real, playable WAV -- not a stub. The media scenes need something the
    # decoder will actually accept, and `cp a b c dest` with several matches
    # silently left a four-byte file here for a long time.
    for w in "$ROOT/dist/cdroot/CASTALIA/MEDIA/"*.WAV; do
        [ -f "$w" ] || continue
        cp "$w" "$HOME_DIR/SONG.WAV"
        cp "$w" "$HOME_DIR/MEDIA/"
    done
    [ -s "$HOME_DIR/SONG.WAV" ] || printf 'RIFF' > "$HOME_DIR/SONG.WAV"
}

# One scene per line: "flag:human name".
DEMOS='paint-demo:Paint (draw, save, cut/paste, undo)
clock-demo:Clock (pick a day, add an appointment, read it back)
theme-demo:Theme Editor (edit, apply, save, reload)
assoc-demo:File associations (one file of each kind)
filedlg-demo:File dialog (browse and pick with the keyboard)
thumbs-demo:File Manager thumbnails
nav-demo:Folder navigation (down and back up, File Manager and Console agree)
undo-demo:Notepad undo and redo (the exact text comes back, not its length)
menuaccel-demo:Menu shortcuts (a right-aligned column, not a missing-glyph box)
freecell-demo:FreeCell (a card moves to a free cell and none are lost)
reversi-demo:Reversi (an illegal move changes nothing; no disc is lost)
winicon-demo:Window icons (every app window carries one, title bar and taskbar):--icons @ROOT@/assets/icons/tango
open-freecell:FreeCell (the deal, for the screenshot)
console-demo:Console (a real scrollback and a command history, not one line)
dlg-exhaust:Every dialog refused with the window pool full (and none leaked)
net-demo:Networking (ARP, ping, UDP echo)
paint-demo-keep:Paint composition (for the screenshot)
net-ping-demo:Network window (ping through its UI)
help-demo:Help (every topic drawn)
taskman-demo:Task Manager (End Task, then sample the graphs)
clock-tick-demo:Clock (partial repaint draws what a full one would)
repaint-demo:Media deck (everything that moves is inside the strip)
cz-demo:Compression (squeeze, restore, and ask before replacing anything)
hotbtn-demo:Buttons (lit under the pointer, sunken when held, back on leaving)
focus-demo:Focus (an unfocused window stops claiming the keyboard):--icons @ROOT@/assets/icons/tango
shrink-demo:Every window at the smallest size the manager allows
cost-demo:Typing (what one keystroke costs, in pixels composited)
wheel-demo:Mouse wheel (the window under the pointer, not the focused one)
stale-demo:Stale pixels (every window agrees with a full repaint)
vale-demo:Wallpaper (the castle is lit from where the sun actually is)
launch-demo:Start menu (every entry opens something, and it all fits 640x480):--width 640 --height 480
lscroll-demo:Start menu overflow (a column too tall still reaches its last entry):--width 640 --height 320
compare-demo:File Compare (two real files, and what changed between them)
bigfile-demo:Editors (a file bigger than the editor is never saved back short)
renmany-demo:Batch rename (ren *.TXT *.BAK renames a folder, or nothing at all)
unsaved-demo:Unsaved changes (closing a changed document asks before losing it)
open-welcome:Welcome tour (secondary text is not the disabled grey):--icons @ROOT@/assets/icons/tango
trash-demo:Recycle Bin (two files of the same name, both restored to their own folder)
orb-demo:Start button (a sphere with three states, whole under an open menu):--icons @ROOT@/assets/icons/tango
cpu-demo:Processor identification (the real CPUID through the real decoder)
switch-demo:Alt+Tab (most-recently-used order, with its panel)
sysmenu-demo:Window menu (every window command, from the keyboard)
menulook-demo:Menu treatment (glossy gutter and sheen, flat when it must be)
deskcache-demo:Desktop cache (rebaked only when the picture would differ)
archive-demo:Archive viewer (list a .CAR without unpacking it)
deskkeys-demo:Desktop icons reached with the keyboard alone
sysinfo-demo:System report (F2 saves more than the screen shows):--height 240
cursor-demo:Pointer (draws an arrow, leaves no trail)
hex-demo:Hex Viewer (reads a window of a file, not the file)
datetime-demo:Date & Time (set the machine clock through its real buttons)
mousekey-demo:Mouse & Keyboard (the panels change real behaviour)
open-calc:Calculator (12+34= typed at the window reads 46)
mem-check:Memory budget (idle ceiling, and an open costs nothing after the first)
car-demo:Archives (pack a folder, wipe it, restore it, refuse an escape)
crash-demo:Crash screen (says something a person can act on)
diskuse-demo:Disk Usage (treemap area really is share)
capture-demo:Screen capture (save a BMP, read it back)
dual-demo:File Manager two-pane (the panes are two independent places)
capp-demo:Add-on package (code inside the package opens its own window)
clip-demo:Clipboard (cut in one app, paste in another)
vdesk-demo:Virtual desktops (windows stay on the desktop they were opened on)
anim-demo:Window animation (the outline grows, then shrinks)
cc-demo:Control Center (every category drawn)
hover-demo:Hover feedback
min-demo:Minimize and restore
mines-demo:Mines
ql-demo:Quick Launch
saver-demo:Screen saver
search-demo:Find (by name, and by what is inside a file)
shutdown-demo:Shutdown dialog
snap-demo:Window snapping
splash-demo:Boot splash
tooltip-demo:Tooltips
wall-demo:Wallpaper modes'

fails=0
runs=0
echo "$DEMOS" | while IFS= read -r row; do
    [ -n "$row" ] || continue
    flag="${row%%:*}"
    rest="${row#*:}"
    # An optional third field is extra arguments for that scene alone. One
    # scene needs a short video mode, because what it is testing is what
    # happens when the report does not fit the screen -- and at the default
    # 800x600 it always does, so the check would pass without ever exercising
    # the path. Names contain no colons, which is what makes this parse.
    case "$rest" in
        *:*) name="${rest%%:*}"; extra="${rest#*:}" ;;
        *)   name="$rest";       extra="" ;;
    esac
    # @ROOT@ stands in for the repo root: the table is single-quoted (its text
    # is data, not shell), so a variable written there would arrive verbatim.
    # One scene needs it, to point at an icon pack that lives in the tree
    # rather than in the throwaway world each scene runs in.
    case "$extra" in
        *@ROOT@*) extra=$(printf '%s' "$extra" | sed "s|@ROOT@|$ROOT|g") ;;
    esac
    runs=$((runs + 1))
    make_world
    out="$(cd "$HOME_DIR" && CASTALIA_HOME="$HOME_DIR" "$BIN" --headless \
           "--$flag" $extra --frames 6 --shot /dev/null 2>&1 || true)"
    # ...and the session log, which is where those lines actually go.
    #
    # A scene reports through SYS_LOGI. That reaches stderr ONLY when the log
    # file cannot be opened -- which was the case here for a long time, purely
    # because the throwaway world above has no LOGS folder and nothing created
    # one. Every "(OK)" this harness has ever counted arrived through that
    # fallback. The moment the log started working, all 49 scenes reported
    # "produced no output" instead, which is exactly the failure the guard at
    # the bottom of this loop exists to catch.
    #
    # Both sources are read now, so the harness works whether or not the log
    # file opens rather than depending on which.
    out="$out
$(cat "$HOME_DIR/LOGS/castalia.log" 2>/dev/null || true)"
    # A scene that overflowed the host's event queue lost the END of a burst
    # of synthetic input -- the keys it cared about most, usually. It is not a
    # failure the scene can see: it just does not get what it typed, and then
    # reports the product not doing what it was told. Treated as a failure
    # here so nobody has to work that out from the symptoms again.
    bad=$(printf '%s\n' "$out" | grep -c 'MISMATCH\|event queue full' || true)
    good=$(printf '%s\n' "$out" | grep -c '(OK)' || true)
    if [ "$bad" -gt 0 ]; then
        printf '  FAIL  %-16s %s\n' "$flag" "$name"
        printf '%s\n' "$out" | grep 'MISMATCH\|event queue full' \
            | sed 's/^/        /'
        echo failed >> "$ROOT/build/.demo-fails"
    elif [ "$good" -gt 0 ]; then
        printf '  ok    %-16s %s -- %s checks\n' "$flag" "$name" "$good"
    elif printf '%s\n' "$out" | grep -q 'exited to DOS'; then
        # The scene ran and drew, but asserts nothing of its own.
        #
        # That used to be allowed anywhere, and ten scenes had drifted into
        # it: the shutdown dialog, the wallpaper modes, the tooltips, the
        # window animation, the taskbar hover, Mines, Quick Launch, the
        # Control Center, the File Manager's thumbnails, window snapping and
        # minimize. Every one of them drove a real gesture, took a
        # screenshot, and reported success whatever happened -- which is how
        # a wallpaper that had never once loaded went unnoticed for as long
        # as its scene existed.
        #
        # Only the scenes below may do it now, and each says why. Anything
        # else that asserts nothing is a failure, so the next scene written
        # this way is caught while somebody is still looking at it.
        case "$flag" in
            open-freecell)
                ;;   # deals a hand for the README screenshot and stops there;
                     # what FreeCell DOES is checked by --freecell-demo
            *)
                printf '  FAIL  %-16s %s -- ran but asserted nothing\n' \
                       "$flag" "$name"
                echo "        Every scene must log at least one (OK) or"
                echo "        (MISMATCH); a screenshot nobody compares is not"
                echo "        a check. Add the assertion, or name the scene in"
                echo "        the screenshot list in tools/run_demos.sh."
                echo failed >> "$ROOT/build/.demo-fails"
                continue
                ;;
        esac
        printf '  ok    %-16s %s -- drawn only\n' "$flag" "$name"
    else
        # No output at all means the binary never ran. That is a failure, not
        # a quiet pass -- it is exactly how a broken harness looks like a
        # green one.
        printf '  FAIL  %-16s %s -- produced no output\n' "$flag" "$name"
        echo failed >> "$ROOT/build/.demo-fails"
    fi
done

runs=$(echo "$DEMOS" | grep -c ':')
fails=0
[ -f "$ROOT/build/.demo-fails" ] && fails=$(wc -l < "$ROOT/build/.demo-fails")

rm -rf "$HOME_DIR"
rm -f "$ROOT/build/.demo-fails"
if [ "$fails" -gt 0 ]; then
    echo "DEMOS FAILED: $fails of $runs"
    exit 1
fi
echo "Demos OK: $runs scenes, no mismatches."
