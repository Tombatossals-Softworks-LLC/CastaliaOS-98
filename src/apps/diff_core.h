/*
 * diff_core.h - Comparing two text files, line by line.
 *
 * "Are these two files the same, and if not, where?" is a question this system
 * could not answer. It can archive a folder, compress a file, dump one as hex
 * and edit it in four different editors -- but given a backup and the original
 * there was nothing that would tell you what changed. DOS itself shipped FC
 * for exactly this, and a machine with no network and no second computer is
 * precisely where it matters: the backup you took last week is the only other
 * copy in existence.
 *
 * The algorithm is RESYNCHRONISATION, which is what FC does, and not a minimal
 * edit script. Walk both files together while the lines agree; when they stop
 * agreeing, look ahead a bounded distance on both sides for a place where they
 * start agreeing again, and report everything between as one difference. It is
 * not always the smallest possible answer -- a true minimal diff needs a table
 * the size of one file times the other, which on the machine this targets is
 * megabytes for a pair of modest files, and the honest tradeoff for a 386 is
 * a bounded window.
 *
 * Two consecutive matching lines are required to call it resynchronised. One
 * is not enough: a single "}" or a blank line matches almost anywhere, and
 * a diff that resyncs on it reports two small differences where there is one
 * real one, over and over down the file.
 *
 * Everything here is pure: the caller reads the files and hands over two
 * buffers, so tests/test_diff.c drives the cases that go wrong -- an inserted
 * block, a deleted block, a change at the very first line, a change at the
 * very last, one file a prefix of the other, files that differ only in line
 * endings -- without touching a disk.
 */
#ifndef CASTALIA_DIFF_CORE_H
#define CASTALIA_DIFF_CORE_H

#include "castalia/ctypes.h"

/*
 * Bounds. A difference report longer than this is not a report anybody reads,
 * and the point at which it stops is stated rather than silently dropped --
 * 'truncated' below. Lines beyond DIFF_MAX_LINES are not compared at all, so
 * the two numbers mean different things and both are reported.
 */
#define DIFF_MAX_LINES 2000
#define DIFF_MAX_HUNKS 200
#define DIFF_WINDOW    60     /* how far ahead resynchronisation looks    */
#define DIFF_SYNC      2      /* matching lines that count as "in step"   */

typedef struct {
    int a_start, a_count;   /* 1-based first line and count in file A */
    int b_start, b_count;   /* ...and in file B; either count may be 0 */
} DiffHunk;

typedef struct {
    DiffHunk hunk[DIFF_MAX_HUNKS];
    int   count;
    int   a_lines, b_lines;   /* lines actually compared on each side     */
    cbool identical;          /* no differences at all                    */
    cbool line_limit;         /* a file was longer than DIFF_MAX_LINES    */
    cbool hunk_limit;         /* more differences than the report holds   */
} DiffResult;

/*
 * Compare two text buffers. 'na' / 'nb' are byte counts, so an embedded NUL is
 * data like any other byte rather than the end of the file.
 *
 * Lines are split on '\n' and a trailing '\r' is dropped, so a DOS file and a
 * Unix file with the same text compare EQUAL. That is a deliberate choice and
 * the opposite one is defensible -- but on a system whose own editors write
 * one convention and whose archives carry the other, a comparison that reports
 * every single line as different is a comparison nobody can use.
 */
void diff_compare(const char *a, cu32 na, const char *b, cu32 nb,
                  DiffResult *out);

/*
 * One line of file A / B by 1-based number, written into 'dst'. Returns CFALSE
 * when there is no such line. The buffer must be the same one passed to
 * diff_compare -- nothing is copied or kept between calls.
 */
cbool diff_line(const char *buf, cu32 n, int lineno, char *dst, cu32 dstsz);

/* How many lines the buffer holds, counted the same way diff_compare counts
 * them (so a report's line numbers and this always agree). */
int diff_count_lines(const char *buf, cu32 n);

/* A one-line summary: "identical", or how many differences and where it had
 * to stop. */
void diff_summary(const DiffResult *r, char *dst, cu32 dstsz);

#endif /* CASTALIA_DIFF_CORE_H */
