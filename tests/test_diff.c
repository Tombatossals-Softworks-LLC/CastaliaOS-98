/*
 * test_diff.c - Comparing two text files (diff_core.c).
 *
 * A compare tool that is wrong is worse than none: it is read as evidence.
 * "The files are identical" when they are not loses the change somebody was
 * looking for, and a difference reported at the wrong line sends them to edit
 * the wrong place. So the cases here are the ones where a resynchronising
 * compare gets it wrong -- a change at the very first line, at the very last,
 * an insert, a delete, one file a prefix of the other, and a file that differs
 * from the other only in its line endings.
 */
#include "ctest.h"
#include "../src/apps/diff_core.h"
#include "castalia/sys.h"

/* Compare two C strings and hand back the result. */
static void cmp(const char *a, const char *b, DiffResult *r)
{
    diff_compare(a, sys_strnlen(a, 65535u), b, sys_strnlen(b, 65535u), r);
}

void test_diff(void)
{
    DiffResult r;
    char line[128];

    /* ---- identical ------------------------------------------------------ */
    cmp("one\ntwo\nthree\n", "one\ntwo\nthree\n", &r);
    CHECK(r.identical);
    CHECK_EQI(r.count, 0);
    CHECK_EQI(r.a_lines, 3);
    CHECK_EQI(r.b_lines, 3);

    /* Two empty files are identical, and are not a difference of size zero. */
    cmp("", "", &r);
    CHECK(r.identical);
    CHECK_EQI(r.a_lines, 0);

    /*
     * Line endings. A file this system's editor wrote and the same text out of
     * an archive differ in every byte of every line ending -- and a compare
     * that calls all of it different is one nobody can use.
     */
    cmp("one\r\ntwo\r\n", "one\ntwo\n", &r);
    CHECK(r.identical);

    /* ...but a real change is still a change, CRLF or not. */
    cmp("one\r\ntwo\r\n", "one\ntWo\n", &r);
    CHECK(!r.identical);

    /* ---- a changed line ------------------------------------------------- */
    cmp("one\ntwo\nthree\nfour\nfive\n",
        "one\nTWO\nthree\nfour\nfive\n", &r);
    CHECK(!r.identical);
    CHECK_EQI(r.count, 1);
    CHECK_EQI(r.hunk[0].a_start, 2);   /* 1-based, the line a person sees */
    CHECK_EQI(r.hunk[0].a_count, 1);
    CHECK_EQI(r.hunk[0].b_start, 2);
    CHECK_EQI(r.hunk[0].b_count, 1);

    /*
     * The FIRST line changed. Walking in step has not started yet here, so a
     * compare that only resynchronises after a match reports this at the wrong
     * place or not at all.
     */
    cmp("one\ntwo\nthree\nfour\n", "ONE\ntwo\nthree\nfour\n", &r);
    CHECK_EQI(r.count, 1);
    CHECK_EQI(r.hunk[0].a_start, 1);
    CHECK_EQI(r.hunk[0].a_count, 1);

    /*
     * The LAST line changed. There is nothing after it to resynchronise ON,
     * so this is the case a window-based compare drops: it looks ahead, finds
     * no agreement, and must fall back to "the rest differs".
     */
    cmp("one\ntwo\nthree\n", "one\ntwo\nTHREE\n", &r);
    CHECK(!r.identical);
    CHECK_EQI(r.count, 1);
    CHECK_EQI(r.hunk[0].a_start, 3);
    CHECK_EQI(r.hunk[0].a_count, 1);
    CHECK_EQI(r.hunk[0].b_count, 1);

    /* ---- an inserted block ---------------------------------------------- */
    cmp("one\ntwo\nthree\nfour\n",
        "one\ntwo\nNEW A\nNEW B\nthree\nfour\n", &r);
    CHECK_EQI(r.count, 1);
    CHECK_EQI(r.hunk[0].a_count, 0);   /* nothing on the A side...        */
    CHECK_EQI(r.hunk[0].a_start, 3);   /* ...inserted before A's line 3   */
    CHECK_EQI(r.hunk[0].b_start, 3);
    CHECK_EQI(r.hunk[0].b_count, 2);

    /* ---- a deleted block ------------------------------------------------ */
    cmp("one\ntwo\nGONE A\nGONE B\nthree\nfour\n",
        "one\ntwo\nthree\nfour\n", &r);
    CHECK_EQI(r.count, 1);
    CHECK_EQI(r.hunk[0].a_start, 3);
    CHECK_EQI(r.hunk[0].a_count, 2);
    CHECK_EQI(r.hunk[0].b_count, 0);

    /* ---- one file is a prefix of the other ------------------------------ */
    cmp("one\ntwo\n", "one\ntwo\nthree\nfour\n", &r);
    CHECK(!r.identical);
    CHECK_EQI(r.count, 1);
    CHECK_EQI(r.hunk[0].a_count, 0);
    CHECK_EQI(r.hunk[0].b_start, 3);
    CHECK_EQI(r.hunk[0].b_count, 2);

    cmp("one\ntwo\nthree\n", "one\n", &r);
    CHECK_EQI(r.count, 1);
    CHECK_EQI(r.hunk[0].a_start, 2);
    CHECK_EQI(r.hunk[0].a_count, 2);
    CHECK_EQI(r.hunk[0].b_count, 0);

    /* An empty file against a full one is entirely one difference. */
    cmp("", "one\ntwo\n", &r);
    CHECK(!r.identical);
    CHECK_EQI(r.count, 1);
    CHECK_EQI(r.hunk[0].b_count, 2);

    /* ---- two separate changes stay separate ------------------------------ */
    cmp("a\nb\nc\nd\ne\nf\ng\nh\n",
        "a\nX\nc\nd\ne\nf\nY\nh\n", &r);
    CHECK_EQI(r.count, 2);
    CHECK_EQI(r.hunk[0].a_start, 2);
    CHECK_EQI(r.hunk[1].a_start, 7);

    /*
     * A single repeated line must not be treated as resynchronisation. With a
     * sync length of one, the blank line after "alpha" matches the blank line
     * after "one", and the report comes apart into several small differences
     * pointing at the wrong lines. Requiring two consecutive matches is what
     * holds this together.
     */
    cmp("head\n\nalpha\n\ntail\n", "head\n\nbeta\n\ntail\n", &r);
    CHECK_EQI(r.count, 1);
    CHECK_EQI(r.hunk[0].a_start, 3);
    CHECK_EQI(r.hunk[0].a_count, 1);
    CHECK_EQI(r.hunk[0].b_count, 1);

    /* ---- line counting agrees with the report ---------------------------- */
    /* A final newline does not add a phantom empty line... */
    CHECK_EQI(diff_count_lines("a\nb\n", 4u), 2);
    /* ...and a file that ends mid-line still has that line. */
    CHECK_EQI(diff_count_lines("a\nb", 3u), 2);
    /* A deliberate blank line IS a line. */
    CHECK_EQI(diff_count_lines("a\n\nb\n", 5u), 3);
    CHECK_EQI(diff_count_lines("", 0u), 0);
    CHECK_EQI(diff_count_lines(NULL, 0u), 0);

    /* ---- reading a line back --------------------------------------------- */
    CHECK(diff_line("one\ntwo\nthree\n", 14u, 2, line, sizeof line));
    CHECK_STR(line, "two");
    CHECK(diff_line("one\r\ntwo\r\n", 10u, 1, line, sizeof line));
    CHECK_STR(line, "one");            /* the CR is not part of the text */
    CHECK(!diff_line("one\n", 4u, 2, line, sizeof line));
    CHECK_STR(line, "");               /* ...and says so by writing nothing */
    CHECK(!diff_line("one\n", 4u, 0, line, sizeof line));
    CHECK(!diff_line(NULL, 0u, 1, line, sizeof line));
    /* A control byte is shown, not sent to the glyph renderer. */
    CHECK(diff_line("a\001b\n", 4u, 1, line, sizeof line));
    CHECK_STR(line, "a.b");
    /* A line longer than the buffer is cut, not overrun. */
    {
        char small[5];
        CHECK(diff_line("abcdefgh\n", 9u, 1, small, sizeof small));
        CHECK_STR(small, "abcd");
    }

    /* ---- the summary says what happened ---------------------------------- */
    cmp("a\n", "a\n", &r);
    diff_summary(&r, line, sizeof line);
    CHECK_STR(line, "The files are identical");
    cmp("a\n", "b\n", &r);
    diff_summary(&r, line, sizeof line);
    CHECK_STR(line, "1 difference");
    cmp("a\nb\nc\nd\ne\nf\ng\nh\n", "a\nX\nc\nd\ne\nf\nY\nh\n", &r);
    diff_summary(&r, line, sizeof line);
    CHECK_STR(line, "2 differences");

    /*
     * ---- the limits are reported, not hidden -----------------------------
     *
     * A truncated report that looks complete is the worst outcome here:
     * somebody reads "3 differences" and concludes the rest of the file
     * matches. Both ceilings have to show up in the summary.
     */
    {
        static char big_a[DIFF_MAX_LINES * 2 + 64];
        static char big_b[DIFF_MAX_LINES * 2 + 64];
        int k, n = 0;
        for (k = 0; k < DIFF_MAX_LINES + 20; k++) {
            big_a[n] = 'x'; big_a[n + 1] = '\n';
            big_b[n] = 'x'; big_b[n + 1] = '\n';
            n += 2;
        }
        big_a[n] = '\0'; big_b[n] = '\0';
        diff_compare(big_a, (cu32)n, big_b, (cu32)n, &r);
        CHECK(r.line_limit);
        CHECK_EQI(r.a_lines, DIFF_MAX_LINES);
        diff_summary(&r, line, sizeof line);
        CHECK(sys_strnlen(line, sizeof line) > 20u);
    }

    /* ---- NULL is survivable ---------------------------------------------- */
    diff_compare(NULL, 0u, NULL, 0u, &r);
    CHECK(r.identical);
    diff_compare("a\n", 2u, NULL, 0u, &r);
    CHECK(!r.identical);
    diff_compare(NULL, 0u, NULL, 0u, NULL);
    diff_summary(NULL, line, sizeof line);
    diff_summary(&r, NULL, 0u);
}
