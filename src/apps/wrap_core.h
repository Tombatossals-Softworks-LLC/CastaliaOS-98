/*
 * wrap_core.h - Where the lines break.
 *
 * Word wrap is the part of a text editor that is easy to write, easy to get
 * subtly wrong, and impossible to notice when it is: the text is all still
 * there, it just breaks in the wrong place, or a row is skipped, or the caret
 * lands one row off. It lived inside the Notepad window where nothing could
 * reach it.
 *
 * The rule is greedy: fill the row, then back up to the last space that fits
 * and break AFTER it, so the space stays on the row it ended. A word longer
 * than the row has nowhere to break and is cut hard rather than being allowed
 * to run off the edge -- a very long path or URL must still be readable.
 *
 * A logical line is '\n'-terminated. An EMPTY logical line is one visual row,
 * not zero: a blank line in a document is a blank line on the screen, and an
 * editor that swallowed it would put every following line one row too high.
 */
#ifndef CASTALIA_WRAP_CORE_H
#define CASTALIA_WRAP_CORE_H

#include "castalia/ctypes.h"

/* Index of the '\n' ending the logical line containing 'idx', or 'len'. */
int wrap_line_end(const char *buf, int len, int idx);
/* Index just after the '\n' before 'idx', i.e. where its line starts. */
int wrap_line_start(const char *buf, int idx);

/*
 * Fill 'rows' with the byte index each visual row begins at, for a wrap width
 * of 'cols' characters, and return how many were written (never more than
 * 'max_rows', never fewer than 1).
 *
 * A width below 1 is treated as 1 rather than dividing the text into nothing.
 */
int wrap_build(const char *buf, int len, int cols, int *rows, int max_rows);

/*
 * Exclusive end of visual row 'r' -- clamped to its logical line, so the
 * trailing '\n' is never part of a row and never drawn.
 */
int wrap_row_end(const char *buf, int len, const int *rows, int count, int r);

/* Which visual row holds byte 'caret'. */
int wrap_row_of(const char *buf, int len, const int *rows, int count,
                int caret);

#endif /* CASTALIA_WRAP_CORE_H */
