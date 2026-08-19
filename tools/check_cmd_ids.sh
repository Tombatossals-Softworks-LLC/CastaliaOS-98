#!/bin/sh
# check_cmd_ids.sh - shell command ids must be unique, and must not land in the
# range the window menu claims.
#
# sh_dispatch_command answers the WINDOW-MENU span before it looks at the table
# of things to open. An id inside that span is therefore not merely wrong: the
# command silently does nothing at all. There is no error, no log line, no
# missing symbol -- the menu entry is drawn, it is clickable, and clicking it
# has no effect.
#
# That is not hypothetical. SH_CMD_FREECELL was 145 and SH_CMD_REVERSI was 146,
# both inside 137..150, and both games were unreachable from the Start menu for
# as long as they had existed. Every scene that tested them called
# app_freecell_open() directly -- correct for testing the game, and blind to
# whether the menu entry arrives.
#
# --launch-demo catches this end to end, and is the stronger check because it
# proves a window really appears. This one is the cheaper and more EXHAUSTIVE
# half: it reads every id in the header, including commands that open no window
# and commands not yet on any surface, which a scene cannot reach.
#
# Usage:  sh tools/check_cmd_ids.sh   (run by: make lint)
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
H="$ROOT/include/castalia/shell.h"

awk '
/^#define[ \t]+SH_CMD_[A-Z_0-9]+[ \t]+[0-9]+/ {
    name = $2; val = $3 + 0;
    ids[name] = val;
    n++;
    # The window family and its span bounds.
    if (name ~ /^SH_CMD_WIN_/) { win[name] = val }
    next
}
END {
    fails = 0;
    # A check that parsed nothing passes everything. Refuse instead.
    if (n < 20) {
        printf("  CMD IDS: found only %d SH_CMD_* defines -- the header moved\n", n);
        printf("           or changed shape; this check is blind.\n");
        exit 1;
    }
    lo = win["SH_CMD_WIN_RESTORE"];
    hi = win["SH_CMD_WIN_DESKTOP"] + ids["SH_CMD_WIN_DESKTOP_N"];
    if (lo == 0 || hi == 0) {
        print "  CMD IDS: cannot find the window-menu span bounds.";
        exit 1;
    }
    for (name in ids) {
        v = ids[name];
        if (name ~ /^SH_CMD_WIN_/) { continue }
        if (name == "SH_CMD_NONE" || name == "SH_CMD_ADDON_BASE") { continue }
        if (v >= lo && v < hi) {
            printf("  IN THE WINDOW-MENU SPAN: %s = %d (span is %d..%d)\n",
                   name, v, lo, hi - 1);
            printf("        sh_dispatch_command answers that span first, so\n");
            printf("        this command silently does nothing at all.\n");
            fails++;
        }
    }
    # Two commands sharing an id is the other way one of them disappears.
    for (a in ids) {
        for (b in ids) {
            if (a < b && ids[a] == ids[b]) {
                printf("  DUPLICATE id %d: %s and %s\n", ids[a], a, b);
                fails++;
            }
        }
    }
    if (fails > 0) {
        printf("Command id check FAILED: %d problem(s).\n", fails);
        exit 1;
    }
    printf("Command ids OK: %d commands, none inside the window-menu span\n", n);
    printf("                (%d..%d) and none sharing an id.\n", lo, hi - 1);
}
' "$H"
