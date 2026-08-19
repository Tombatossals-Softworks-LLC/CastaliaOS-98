/*
 * grep_core.c - Finding text inside a file (see grep_core.h).
 */
#include "grep_core.h"
#include "castalia/sys.h"

static int gc_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

/*
 * Does 'needle' start at buf[at]? Compared case-insensitively, and bounded by
 * 'n' rather than by a NUL -- the buffer is a file, and a file is allowed to
 * contain zeroes.
 */
static cbool gc_at(const char *buf, cu32 n, cu32 at, const char *needle)
{
    cu32 k = 0;
    while (needle[k] != '\0') {
        if (at + k >= n) { return CFALSE; }
        if (gc_lower((unsigned char)buf[at + k]) !=
            gc_lower((unsigned char)needle[k])) {
            return CFALSE;
        }
        k++;
    }
    return CTRUE;
}

int grep_find_line(const char *buf, cu32 n, const char *needle)
{
    cu32 i;
    int line = 1;
    if (buf == NULL || needle == NULL || needle[0] == '\0') { return 0; }
    for (i = 0; i < n; i++) {
        if (gc_at(buf, n, i, needle)) { return line; }
        /* Counted AFTER the test, so a match on the newline itself belongs to
         * the line it ends rather than to the one after it. */
        if (buf[i] == '\n') { line++; }
    }
    return 0;
}

int grep_count_lines(const char *buf, cu32 n, const char *needle, int cap)
{
    cu32 i;
    int line = 1, hits = 0, last_hit = 0;
    if (buf == NULL || needle == NULL || needle[0] == '\0') { return 0; }
    if (cap < 1) { return 0; }
    for (i = 0; i < n && hits < cap; i++) {
        if (line != last_hit && gc_at(buf, n, i, needle)) {
            hits++;
            last_hit = line;      /* one hit per line, however many it holds */
        }
        if (buf[i] == '\n') { line++; }
    }
    return hits;
}
