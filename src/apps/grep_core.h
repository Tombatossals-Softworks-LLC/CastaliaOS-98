/*
 * grep_core.h - Finding text INSIDE a file.
 *
 * The File Manager could find a file by its name and could not find it by what
 * is in it. On a machine with no search indexer, no second computer and a few
 * hundred files, "which one did I write that in" is a question you answer by
 * opening them one at a time -- which is exactly the job a computer should be
 * doing. This is the matching half of it; the walking half is the File
 * Manager's, which already walks a tree looking at names.
 *
 * Case-insensitive, because the name search is and two kinds of Find in one
 * window that disagree about case would be a worse surprise than either
 * choice on its own.
 *
 * It reports the LINE, not the byte offset. A byte offset is not somewhere a
 * person can go: every editor in this system counts lines, and the answer has
 * to be one you can act on.
 *
 * Pure: the caller reads the file and hands over a buffer, so tests/test_grep.c
 * drives the cases that go wrong -- a match on the first line, on the last, a
 * needle spanning what looks like a line break, an empty needle, a file with
 * no newline at all -- without touching a disk.
 */
#ifndef CASTALIA_GREP_CORE_H
#define CASTALIA_GREP_CORE_H

#include "castalia/ctypes.h"

/*
 * The 1-based line holding the first case-insensitive occurrence of 'needle',
 * or 0 when there is none.
 *
 * 'n' is a byte count, so an embedded NUL is a byte like any other rather than
 * the end of the file -- which is what lets this be pointed at something that
 * turned out not to be text without stopping at its first zero.
 *
 * An EMPTY needle finds nothing rather than matching everywhere. Matching
 * everywhere is technically right and useless: it would report every file in
 * the tree, which is not a search result, it is the tree.
 */
int grep_find_line(const char *buf, cu32 n, const char *needle);

/* How many lines of 'buf' hold at least one occurrence. Stops counting at
 * 'cap' so a file that matches on every line cannot cost more than a bounded
 * amount of work. */
int grep_count_lines(const char *buf, cu32 n, const char *needle, int cap);

#endif /* CASTALIA_GREP_CORE_H */
