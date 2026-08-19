/*
 * wrap_core.c - Where the lines break (see wrap_core.h).
 */
#include "wrap_core.h"

int wrap_line_end(const char *buf, int len, int idx)
{
    if (buf == NULL) { return 0; }
    if (idx < 0) { idx = 0; }
    while (idx < len && buf[idx] != '\n') { idx++; }
    return idx;
}

int wrap_line_start(const char *buf, int idx)
{
    if (buf == NULL) { return 0; }
    while (idx > 0 && buf[idx - 1] != '\n') { idx--; }
    return idx;
}

int wrap_build(const char *buf, int len, int cols, int *rows, int max_rows)
{
    int count = 0, i = 0;
    if (buf == NULL || rows == NULL || max_rows < 1) { return 0; }
    if (len < 0) { len = 0; }
    if (cols < 1) { cols = 1; }
    while (i <= len && count < max_rows) {
        int le = wrap_line_end(buf, len, i);
        int rs = i;
        if (rs == le) {
            /* An empty logical line is one visual row, not none. */
            rows[count++] = rs;
        } else {
            while (rs < le && count < max_rows) {
                int remain = le - rs;
                int take = (remain > cols) ? cols : remain;
                if (remain > cols) {
                    int sp = -1, k;
                    /* The last space that fits. Starting at rs+1 means a row
                     * beginning with a space cannot break to nothing. */
                    for (k = rs + 1; k < rs + take; k++) {
                        if (buf[k] == ' ') { sp = k; }
                    }
                    /* Break AFTER the space, so it stays on the row it ended.
                     * With no space at all the word is longer than the row and
                     * takes a hard break rather than running off the edge. */
                    if (sp > rs) { take = sp - rs + 1; }
                }
                rows[count++] = rs;
                rs += take;
            }
        }
        if (le >= len) { break; }
        i = le + 1;
    }
    if (count == 0) { rows[count++] = 0; }
    return count;
}

int wrap_row_end(const char *buf, int len, const int *rows, int count, int r)
{
    int le;
    if (buf == NULL || rows == NULL || r < 0 || r >= count) { return 0; }
    le = wrap_line_end(buf, len, rows[r]);
    if (r + 1 < count && rows[r + 1] <= le) { return rows[r + 1]; }
    return le;
}

int wrap_row_of(const char *buf, int len, const int *rows, int count,
                int caret)
{
    int r;
    if (buf == NULL || rows == NULL || count < 1) { return 0; }
    for (r = 0; r < count; r++) {
        int end = wrap_row_end(buf, len, rows, count, r);
        if (caret <= end && (r + 1 >= count || caret < rows[r + 1])) {
            return r;
        }
    }
    return count - 1;
}
