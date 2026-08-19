#!/bin/sh
#
# check_ctrl_keys.sh - Ctrl+letter shortcuts must answer BOTH encodings.
#
# The two backends disagree about what a Ctrl+letter keystroke looks like:
#
#     DOS  (INT 16h)   key = the ASCII control code   -- Ctrl+Z arrives as 26
#     host (SDL-ish)   ch  = the letter, mods = CTRL  -- Ctrl+Z arrives as 'z'
#
# A handler written as `if (key == 26)` answers only the first of those, so it
# works on the target and ignores every host-encoded press. Today the host
# backend is headless, so no user meets that directly -- but demo scenes drive
# it, any future interactive host front-end would use it, and a handler that
# honours half a contract is a bug whether or not anyone has tripped on it
# yet. It will not show up under the sanitizers, and it will not show up in a
# scene that happens to drive the other encoding -- which is how two of these
# survived.
#
# It has happened twice. Notepad and Paint got it right with an NP_CTRL /
# PT_CTRL macro; CastaliaWrite and CastaliaSheet were both written testing the
# control code alone, and Sheet's Ctrl+Z had been dead on the host since the
# day it was added.
#
# So the shape is checked mechanically: comparing 'key' or 'ch' against a bare
# control-code CONSTANT is refused. Go through a *_CTRL macro, which takes the
# modifier word and answers both encodings at once.
#
# Exempt are the codes that are not Ctrl+letter at all -- a real key sends
# them, and testing for them is not a shortcut:
#
#     8  Backspace   9  Tab   10 LF   13 Enter   27 Esc
#
# Exit: 0 clean, 1 if a bare control-code comparison is found.
#
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# The Ctrl+letter codes worth flagging: 1..26 minus the real keys above.
CODES='1|2|3|4|5|6|7|11|12|14|15|16|17|18|19|20|21|22|23|24|25|26'

scan() {
    # 'key' or 'ch' compared to one of those constants, in either order.
    grep -nE "\<(key|ch)\>[[:space:]]*[!=]=[[:space:]]*($CODES)\>" \
        "$@" 2>/dev/null || true
}

SRC=$(find src include -name '*.c' -o -name '*.h' | sort)

# A comparison inside a *_CTRL macro definition is the fix, not the fault.
HITS=$(scan $SRC | grep -v '_CTRL(key, ch, mods' | grep -v '^\S*:[0-9]*: \*' || true)

CONTROL_SEEN=no
# ---- the control ------------------------------------------------------------
# A check nobody has watched fail is not yet a check. Feed the scanner a line
# with the exact shape it hunts and confirm it bites.
TMP="${TMPDIR:-/tmp}/ctrlkeys.$$.c"
printf 'static int f(int key, int ch) { if (key == 26) { return ch; } return 0; }\n' > "$TMP"
if scan "$TMP" | grep -q 'key == 26'; then
    CONTROL_SEEN=yes
fi
rm -f "$TMP"

if [ "$CONTROL_SEEN" != yes ]; then
    echo "Ctrl-key check FAILED: the scanner did not flag its own control line." >&2
    echo "  The pattern is broken -- a clean report here means nothing." >&2
    exit 1
fi

if [ -n "$HITS" ]; then
    echo "Ctrl-key check FAILED: a shortcut tests the control code alone." >&2
    echo "$HITS" | sed 's/^/  /' >&2
    echo "" >&2
    echo "  These work on DOS and do nothing on the host backend, which" >&2
    echo "  delivers Ctrl+letter as the letter plus PLAT_MOD_CTRL." >&2
    echo "  Use the file's *_CTRL(key, ch, mods, code, letter) macro." >&2
    exit 1
fi

N=$(printf '%s\n' "$SRC" | wc -l | tr -d ' ')
echo "Ctrl-key check OK: $N sources answer both keystroke encodings,"
echo "                   and the control line was flagged (this can fail)."
