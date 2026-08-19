/*
 * test_wrap.c - Where the lines break (wrap_core.c).
 *
 * Word wrap is easy to write, easy to get subtly wrong, and impossible to
 * notice when it is: the text is all still there, it just breaks in the wrong
 * place, or a row is skipped, or the caret lands one row off. It lived inside
 * the Notepad window where nothing could reach it.
 */
#include "ctest.h"
#include "../src/apps/wrap_core.h"
#include <string.h>

#define R 64
static int rows[R];

/* Build and return the count, for a NUL-terminated string. */
static int build(const char *s, int cols)
{
    return wrap_build(s, (int)strlen(s), cols, rows, R);
}

/* The text of visual row r, into dst. */
static void row_text(const char *s, int count, int r, char *dst, int dstsz)
{
    int len = (int)strlen(s);
    int a = rows[r];
    int b = wrap_row_end(s, len, rows, count, r);
    int n = b - a;
    if (n < 0) { n = 0; }
    if (n > dstsz - 1) { n = dstsz - 1; }
    memcpy(dst, s + a, (size_t)n);
    dst[n] = '\0';
}

void test_wrap(void)
{
    char t[80];
    int n;

    printf("- word wrap\n");

    /* ---- nothing to wrap ------------------------------------------------ */
    n = build("", 10);
    CHECK_EQI(n, 1);              /* an empty document is one empty row */
    CHECK_EQI(rows[0], 0);
    n = build("hello", 10);
    CHECK_EQI(n, 1);
    row_text("hello", n, 0, t, sizeof t); CHECK_STR(t, "hello");

    /* Exactly the width is one row, not two. The off-by-one here is the
     * classic one: a row that fits must not spill. */
    n = build("abcde", 5);
    CHECK_EQI(n, 1);
    row_text("abcde", n, 0, t, sizeof t); CHECK_STR(t, "abcde");
    n = build("abcdef", 5);
    CHECK_EQI(n, 2);
    /* A line that fits exactly AND contains a space must still be one row.
     * Every exact-fit case above happens to have no space in it, so a
     * too-eager wrap test (">= cols" where "> cols" belongs) passed all of
     * them: it only breaks early when there is a space to break at. This is
     * the case that separates the two. */
    n = build("ab cd", 5);
    CHECK_EQI(n, 1);
    row_text("ab cd", n, 0, t, sizeof t); CHECK_STR(t, "ab cd");
    n = build("a b c", 5);
    CHECK_EQI(n, 1);
    row_text("a b c", n, 0, t, sizeof t); CHECK_STR(t, "a b c");
    /* One character over, and it does break -- so the check above is not
     * simply asserting that wrapping never happens. */
    n = build("ab cde", 5);
    CHECK_EQI(n, 2);

    /* ---- breaking at a space -------------------------------------------- */
    /* The break goes AFTER the space, so the space stays on the row it
     * ended and the next row starts with a word. */
    n = build("hello world", 8);
    CHECK_EQI(n, 2);
    row_text("hello world", n, 0, t, sizeof t); CHECK_STR(t, "hello ");
    row_text("hello world", n, 1, t, sizeof t); CHECK_STR(t, "world");

    /* The LAST space that fits, not the first. */
    n = build("a b c d e f", 6);
    row_text("a b c d e f", n, 0, t, sizeof t); CHECK_STR(t, "a b c ");

    /* ---- a word longer than the row -------------------------------------- */
    /* There is nowhere to break, so it is cut hard rather than running off
     * the edge. A long path must still be readable. */
    n = build("abcdefghij", 4);
    CHECK_EQI(n, 3);
    row_text("abcdefghij", n, 0, t, sizeof t); CHECK_STR(t, "abcd");
    row_text("abcdefghij", n, 1, t, sizeof t); CHECK_STR(t, "efgh");
    row_text("abcdefghij", n, 2, t, sizeof t); CHECK_STR(t, "ij");

    /* A long word AFTER a space still breaks at the space first. */
    n = build("hi abcdefghij", 6);
    row_text("hi abcdefghij", n, 0, t, sizeof t); CHECK_STR(t, "hi ");

    /* ---- explicit newlines ----------------------------------------------- */
    n = build("one\ntwo", 20);
    CHECK_EQI(n, 2);
    row_text("one\ntwo", n, 0, t, sizeof t); CHECK_STR(t, "one");
    row_text("one\ntwo", n, 1, t, sizeof t); CHECK_STR(t, "two");
    /* The '\n' is never part of a row, so it is never drawn. */

    /* An EMPTY logical line is one visual row, not zero. An editor that
     * swallowed it would put every following line one row too high. */
    n = build("a\n\nb", 20);
    CHECK_EQI(n, 3);
    row_text("a\n\nb", n, 0, t, sizeof t); CHECK_STR(t, "a");
    row_text("a\n\nb", n, 1, t, sizeof t); CHECK_STR(t, "");
    row_text("a\n\nb", n, 2, t, sizeof t); CHECK_STR(t, "b");

    /* Several blank lines in a row all survive. */
    n = build("a\n\n\n\nb", 20);
    CHECK_EQI(n, 5);

    /* A trailing newline leaves an empty last row -- that is where the caret
     * goes when you press Enter at the end, so it must exist. */
    n = build("a\n", 20);
    CHECK_EQI(n, 2);
    row_text("a\n", n, 1, t, sizeof t); CHECK_STR(t, "");

    /* Wrapping and newlines together. */
    n = build("hello world\nbye", 8);
    CHECK_EQI(n, 3);
    row_text("hello world\nbye", n, 0, t, sizeof t); CHECK_STR(t, "hello ");
    row_text("hello world\nbye", n, 1, t, sizeof t); CHECK_STR(t, "world");
    row_text("hello world\nbye", n, 2, t, sizeof t); CHECK_STR(t, "bye");

    /* ---- which row holds the caret ---------------------------------------- */
    {
        const char *s = "hello world";
        int len = (int)strlen(s);
        n = build(s, 8);
        CHECK_EQI(wrap_row_of(s, len, rows, n, 0), 0);
        CHECK_EQI(wrap_row_of(s, len, rows, n, 5), 0);   /* on the space  */
        CHECK_EQI(wrap_row_of(s, len, rows, n, 6), 1);   /* start of row 1 */
        CHECK_EQI(wrap_row_of(s, len, rows, n, len), 1); /* the very end   */
    }
    /* After a newline the caret is on the NEXT row, not the end of the last. */
    {
        const char *s = "one\ntwo";
        int len = (int)strlen(s);
        n = build(s, 20);
        CHECK_EQI(wrap_row_of(s, len, rows, n, 3), 0);   /* before the \n */
        CHECK_EQI(wrap_row_of(s, len, rows, n, 4), 1);   /* after it      */
    }

    /* ---- line boundaries --------------------------------------------------- */
    {
        const char *s = "one\ntwo\nthree";
        int len = (int)strlen(s);
        CHECK_EQI(wrap_line_end(s, len, 0), 3);
        CHECK_EQI(wrap_line_end(s, len, 4), 7);
        CHECK_EQI(wrap_line_end(s, len, 8), len);   /* no trailing newline */
        CHECK_EQI(wrap_line_start(s, 0), 0);
        CHECK_EQI(wrap_line_start(s, 5), 4);
        CHECK_EQI(wrap_line_start(s, 3), 0);        /* on the newline      */
        CHECK_EQI(wrap_line_start(s, 4), 4);        /* just after it       */
    }

    /* ---- the row cap ------------------------------------------------------- */
    /* A document with more rows than the table holds must fill it and stop,
     * not run past the end. That table is a fixed array on the DOS side. */
    {
        static char big[600];
        int i;
        for (i = 0; i < 500; i++) { big[i] = (char)((i % 2) ? '\n' : 'x'); }
        big[500] = '\0';
        n = wrap_build(big, 500, 10, rows, 8);
        CHECK_EQI(n, 8);
    }

    /* ---- refusals ----------------------------------------------------------- */
    /* A width of zero would divide the text into nothing; it is treated as 1. */
    n = build("abc", 0);
    CHECK_EQI(n, 3);
    n = build("abc", -5);
    CHECK_EQI(n, 3);
    CHECK_EQI(wrap_build(NULL, 5, 10, rows, R), 0);
    CHECK_EQI(wrap_build("abc", 3, 10, NULL, R), 0);
    CHECK_EQI(wrap_build("abc", 3, 10, rows, 0), 0);
    CHECK_EQI(wrap_row_end("abc", 3, rows, 1, -1), 0);
    CHECK_EQI(wrap_row_end("abc", 3, rows, 1, 5), 0);
    CHECK_EQI(wrap_line_end(NULL, 3, 0), 0);
    CHECK_EQI(wrap_line_start(NULL, 3), 0);
}
