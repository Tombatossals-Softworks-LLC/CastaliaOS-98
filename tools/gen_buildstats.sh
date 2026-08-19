#!/bin/sh
# gen_buildstats.sh - Generate include/castalia/buildstats.h with live source
# statistics (lines of code, file counts, function estimate). Committed so the
# host/DOS builds never depend on running it; regenerate with `make gen-stats`.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

CFILES=$(find src include -name '*.c' | wc -l | tr -d ' ')
HFILES=$(find src include -name '*.h' | wc -l | tr -d ' ')
CLOC=$(find src include -name '*.c' | xargs cat 2>/dev/null | wc -l | tr -d ' ')
HLOC=$(find src include -name '*.h' | xargs cat 2>/dev/null | wc -l | tr -d ' ')
ALLLOC=$(find src include tests tools -name '*.c' -o -name '*.h' | xargs cat 2>/dev/null | wc -l | tr -d ' ')
TESTLOC=$(find tests -name '*.c' -o -name '*.h' | xargs cat 2>/dev/null | wc -l | tr -d ' ')
# Function-definition estimate: lines that look like a C function head at col 0.
FUNCS=$(find src -name '*.c' | xargs grep -hE '^[A-Za-z_].*\)[[:space:]]*$' 2>/dev/null | wc -l | tr -d ' ')
MODULES=$(find src -maxdepth 1 -type d | wc -l | tr -d ' ')
# TODO/FIXME markers. Two exclusions, both because the count was counting its
# own bookkeeping: this generated header holds the number, and app_about.c is
# the window that prints it ("%d TODO/FIXME marker(s) left"). Word boundaries
# keep identifiers like SD_TODOS out of it. Before this, the About window told
# users there were 7 markers left when there were 3 -- the other four were the
# machinery that reports the figure.
TODO=$(find src include -name '*.c' -o -name '*.h' \
        | grep -v 'buildstats\.h' | grep -v 'app_about\.c' \
        | xargs grep -oiE '\b(TODO|FIXME)\b' 2>/dev/null | wc -l | tr -d ' ')
FILES=$((CFILES + HFILES))
LOC=$((CLOC + HLOC))

OUT="${1:-include/castalia/buildstats.h}"
cat > "$OUT" <<HDR
/*
 * buildstats.h - Generated source-tree statistics (do not edit by hand).
 *
 * Produced by tools/gen_buildstats.sh (make gen-stats). Committed so the build
 * never depends on the generator. Consumed by the About / System Information
 * windows to report the size and shape of the codebase honestly.
 */
#ifndef CASTALIA_BUILDSTATS_H
#define CASTALIA_BUILDSTATS_H

#define CASTALIA_STAT_LOC        ${LOC}L   /* lines across src/ + include/       */
#define CASTALIA_STAT_CODE_LOC   ${CLOC}L   /* .c lines                           */
#define CASTALIA_STAT_HEADER_LOC ${HLOC}L   /* .h lines                           */
#define CASTALIA_STAT_TOTAL_LOC  ${ALLLOC}L   /* incl. tests/ + tools/            */
#define CASTALIA_STAT_TEST_LOC   ${TESTLOC}L   /* tests/ lines                     */
#define CASTALIA_STAT_FILES      ${FILES}    /* .c + .h files in src/ + include/   */
#define CASTALIA_STAT_CFILES     ${CFILES}    /* .c files                          */
#define CASTALIA_STAT_HFILES     ${HFILES}    /* .h files                          */
#define CASTALIA_STAT_FUNCS      ${FUNCS}    /* approx. function definitions       */
#define CASTALIA_STAT_MODULES    ${MODULES}    /* subsystem directories under src/  */
#define CASTALIA_STAT_TODOS      ${TODO}    /* remaining TODO/FIXME markers        */

#endif /* CASTALIA_BUILDSTATS_H */
HDR
echo "  STATS $OUT  (${LOC} LOC, ${FILES} files)"
