/*
 * diff_core.c - Comparing two text files, line by line (see diff_core.h).
 */
#include "diff_core.h"
#include "castalia/sys.h"

/*
 * Where each line starts and how long it is. Two of these, one per side, held
 * as file-scope arrays rather than on the stack: 2000 lines x 8 bytes x 2 is
 * 32K, and the DOS build's stack is not the place for it.
 *
 * This makes diff_compare non-reentrant, which is true of it anyway -- it
 * writes a single caller-supplied result and there is one comparison window.
 */
typedef struct {
    cu32 off;
    cu32 len;
} DiffLine;

static DiffLine g_a[DIFF_MAX_LINES];
static DiffLine g_b[DIFF_MAX_LINES];

/*
 * Split a buffer into lines. Returns how many were stored; sets *over when
 * there were more than fit.
 *
 * A trailing '\r' is dropped, which is what makes a DOS file and a Unix file
 * with the same text compare equal. A final line with no newline still counts:
 * a file that ends mid-line has that line, and reporting it as absent would
 * point at the wrong place.
 */
static int diff_split(const char *buf, cu32 n, DiffLine *out, cbool *over)
{
    cu32 i = 0, start = 0;
    int count = 0;
    if (over != NULL) { *over = CFALSE; }
    if (buf == NULL) { return 0; }
    while (i <= n) {
        if (i == n || buf[i] == '\n') {
            cu32 len = i - start;
            if (len > 0 && buf[start + len - 1] == '\r') { len--; }
            /* The empty tail after a final newline is not a line: "a\n" is
             * one line, not two. But "a\nb" is two, and so is "a\n\n". An
             * EMPTY buffer is zero lines -- the i > 0 this guard used to carry
             * let the empty file through as one blank line, so two empty files
             * compared as one line each and diff_count_lines("") answered 1. */
            if (i == n && len == 0 && (i == 0 || buf[i - 1] == '\n')) { break; }
            if (count >= DIFF_MAX_LINES) {
                if (over != NULL) { *over = CTRUE; }
                return count;
            }
            out[count].off = start;
            out[count].len = len;
            count++;
            if (i == n) { break; }
            start = i + 1;
        }
        i++;
    }
    return count;
}

static cbool line_eq(const char *a, const DiffLine *la,
                     const char *b, const DiffLine *lb)
{
    cu32 k;
    if (la->len != lb->len) { return CFALSE; }
    for (k = 0; k < la->len; k++) {
        if (a[la->off + k] != b[lb->off + k]) { return CFALSE; }
    }
    return CTRUE;
}

/* Do DIFF_SYNC lines starting at a[i] and b[j] all match? Running off the end
 * of either side is NOT a match: the tail is handled by the caller, and
 * treating "both ended" as synchronised would swallow a real difference. */
static cbool in_step(const char *a, int na, int i,
                     const char *b, int nb, int j)
{
    int k;
    for (k = 0; k < DIFF_SYNC; k++) {
        if (i + k >= na || j + k >= nb) { return CFALSE; }
        if (!line_eq(a, &g_a[i + k], b, &g_b[j + k])) { return CFALSE; }
    }
    return CTRUE;
}

static void hunk_add(DiffResult *r, int as, int ac, int bs, int bc)
{
    if (r->count >= DIFF_MAX_HUNKS) { r->hunk_limit = CTRUE; return; }
    r->hunk[r->count].a_start = as + 1;   /* 1-based for a person to read */
    r->hunk[r->count].a_count = ac;
    r->hunk[r->count].b_start = bs + 1;
    r->hunk[r->count].b_count = bc;
    r->count++;
}

void diff_compare(const char *a, cu32 na, const char *b, cu32 nb,
                  DiffResult *out)
{
    int la, lb, i = 0, j = 0;
    cbool over_a = CFALSE, over_b = CFALSE;

    if (out == NULL) { return; }
    out->count = 0;
    out->identical = CTRUE;
    out->line_limit = CFALSE;
    out->hunk_limit = CFALSE;
    out->a_lines = 0;
    out->b_lines = 0;
    if (a == NULL) { na = 0; }
    if (b == NULL) { nb = 0; }

    la = diff_split(a, na, g_a, &over_a);
    lb = diff_split(b, nb, g_b, &over_b);
    out->a_lines = la;
    out->b_lines = lb;
    out->line_limit = (over_a || over_b) ? CTRUE : CFALSE;

    while (i < la && j < lb) {
        int d, found_k = -1, found_m = -1;
        if (line_eq(a, &g_a[i], b, &g_b[j])) { i++; j++; continue; }

        /*
         * Out of step. Look for the nearest place they come back into step,
         * nearest meaning the smallest d = max(skip in A, skip in B) -- so a
         * one-line change is preferred to a one-line insert plus a one-line
         * delete, and neither side is favoured over the other.
         */
        for (d = 1; d <= DIFF_WINDOW && found_k < 0; d++) {
            int k;
            for (k = 0; k <= d; k++) {
                /* (k, d): skip k in A and d in B, and the mirror (d, k). */
                if (i + k < la && j + d < lb &&
                    in_step(a, la, i + k, b, lb, j + d)) {
                    found_k = k; found_m = d; break;
                }
                if (i + d < la && j + k < lb &&
                    in_step(a, la, i + d, b, lb, j + k)) {
                    found_k = d; found_m = k; break;
                }
            }
        }

        if (found_k < 0) {
            /* Never came back into step inside the window: everything left on
             * both sides is one difference. Saying so is honest; pretending
             * to have resynchronised is what produces a page of nonsense. */
            hunk_add(out, i, la - i, j, lb - j);
            out->identical = CFALSE;
            i = la; j = lb;
            break;
        }
        hunk_add(out, i, found_k, j, found_m);
        out->identical = CFALSE;
        i += found_k;
        j += found_m;
    }

    /* Whatever is left on one side after the other ran out. */
    if (i < la || j < lb) {
        hunk_add(out, i, la - i, j, lb - j);
        out->identical = CFALSE;
    }
}

cbool diff_line(const char *buf, cu32 n, int lineno, char *dst, cu32 dstsz)
{
    cu32 i = 0, start = 0;
    int count = 0;
    if (dst == NULL || dstsz == 0u) { return CFALSE; }
    dst[0] = '\0';
    if (buf == NULL || lineno < 1) { return CFALSE; }
    while (i <= n) {
        if (i == n || buf[i] == '\n') {
            cu32 len = i - start;
            if (len > 0 && buf[start + len - 1] == '\r') { len--; }
            if (i == n && len == 0 && (i == 0 || buf[i - 1] == '\n')) { break; }
            count++;
            if (count == lineno) {
                cu32 k, m = 0;
                for (k = 0; k < len && m + 1u < dstsz; k++) {
                    char c = buf[start + k];
                    /* A control byte in what claimed to be text would drive
                     * the glyph renderer somewhere strange; show it as a dot,
                     * the way the hex viewer's text column does. */
                    dst[m++] = (c >= 32 || c == '\t') ? c : '.';
                }
                dst[m] = '\0';
                return CTRUE;
            }
            if (i == n) { break; }
            start = i + 1;
        }
        i++;
    }
    return CFALSE;
}

int diff_count_lines(const char *buf, cu32 n)
{
    cbool over = CFALSE;
    if (buf == NULL) { return 0; }
    return diff_split(buf, n, g_a, &over);
}

void diff_summary(const DiffResult *r, char *dst, cu32 dstsz)
{
    if (dst == NULL || dstsz == 0u) { return; }
    dst[0] = '\0';
    if (r == NULL) { return; }
    if (r->identical) {
        sys_strlcpy(dst, "The files are identical", dstsz);
        return;
    }
    sys_snprintf(dst, dstsz, "%d difference%s", r->count,
                 (r->count == 1) ? "" : "s");
    /* Both limits are stated, because a truncated report that looks complete
     * is worse than no report: somebody concludes the rest of the file
     * matches. */
    if (r->hunk_limit) {
        sys_strlcat(dst, " (more than this window holds)", dstsz);
    }
    if (r->line_limit) {
        sys_strlcat(dst, " -- only the first 2000 lines were compared", dstsz);
    }
}
