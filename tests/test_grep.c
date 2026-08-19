/*
 * test_grep.c - Finding text inside a file (grep_core.c).
 *
 * A search that reports the wrong LINE sends somebody to edit the wrong place,
 * and a search that misses sends them to open every file by hand -- which is
 * the job it exists to save. So the cases here are the ones a line counter
 * gets wrong: a match on the very first line, on the very last, on a line with
 * no newline after it, a match ON a newline, and a file that is one long line.
 */
#include "ctest.h"
#include "../src/apps/grep_core.h"
#include "castalia/sys.h"

/* Search a C string, so the tests read as text rather than as byte counts. */
static int find(const char *hay, const char *needle)
{
    return grep_find_line(hay, sys_strnlen(hay, 65535u), needle);
}

void test_grep(void)
{
    /* ---- which line ------------------------------------------------------ */
    CHECK_EQI(find("alpha\nbeta\ngamma\n", "alpha"), 1);   /* the first  */
    CHECK_EQI(find("alpha\nbeta\ngamma\n", "beta"),  2);
    CHECK_EQI(find("alpha\nbeta\ngamma\n", "gamma"), 3);   /* the last   */
    CHECK_EQI(find("alpha\nbeta\ngamma\n", "delta"), 0);   /* not there  */

    /* A file that does not end in a newline still has its last line. */
    CHECK_EQI(find("alpha\nbeta", "beta"), 2);
    /* ...and a file that is one long line is line 1, not line 0. */
    CHECK_EQI(find("no newlines here at all", "at all"), 1);

    /* Blank lines count. Miss this and every line number after the first
     * paragraph break is wrong, which is the failure that looks like the
     * search working. */
    CHECK_EQI(find("one\n\n\nfour\n", "four"), 4);

    /* ---- case ------------------------------------------------------------ */
    CHECK_EQI(find("Hello World\n", "hello"), 1);
    CHECK_EQI(find("Hello World\n", "WORLD"), 1);
    CHECK_EQI(find("hello world\n", "Hello World"), 1);

    /* ---- the edges ------------------------------------------------------- */
    /*
     * A needle that would run off the end must not match on a prefix of
     * itself, and must not read past the buffer to find that out.
     */
    CHECK_EQI(find("abc", "abcd"), 0);
    CHECK_EQI(find("", "a"), 0);

    /*
     * An empty needle finds NOTHING. Matching everywhere is technically right
     * and useless: the File Manager would report every file in the tree, which
     * is not a search result, it is the tree.
     */
    CHECK_EQI(find("anything at all\n", ""), 0);
    CHECK_EQI(grep_find_line(NULL, 0u, "a"), 0);
    CHECK_EQI(grep_find_line("abc", 3u, NULL), 0);

    /*
     * A NUL in the middle is a byte, not the end. This is what lets the search
     * be pointed at something that turned out not to be text and still find
     * the string that is in it.
     */
    CHECK_EQI(grep_find_line("aa\0bb\nxxfoundxx\n", 16u, "found"), 2);

    /* A newline inside the needle matches across the break, and belongs to the
     * line the match STARTS on. */
    CHECK_EQI(find("one\ntwo\nthree\n", "one\ntwo"), 1);
    CHECK_EQI(find("one\ntwo\nthree\n", "two\nthree"), 2);

    /* ---- counting lines --------------------------------------------------- */
    CHECK_EQI(grep_count_lines("a\nb\na\nc\na\n", 10u, "a", 100), 3);
    /* Twice on one line is one line. */
    CHECK_EQI(grep_count_lines("aa\nb\n", 5u, "a", 100), 1);
    /* The cap stops the work, and is what it says. */
    CHECK_EQI(grep_count_lines("a\na\na\na\na\n", 10u, "a", 2), 2);
    CHECK_EQI(grep_count_lines("a\na\n", 4u, "a", 0), 0);
    CHECK_EQI(grep_count_lines("abc", 3u, "", 10), 0);
    CHECK_EQI(grep_count_lines(NULL, 0u, "a", 10), 0);

    /*
     * ...and the two agree with each other: whenever count says there is at
     * least one, find says which line, and the reverse. They are separate
     * walks over the same rule and drifting apart is exactly how a result
     * count and a result list stop matching.
     */
    {
        static const char *const SAMPLES[] = {
            "alpha\nbeta\ngamma\n", "\n\n\nx\n", "one long line only",
            "aXa\nbXb\n", "nothing here", "", "trailing\n"
        };
        static const char *const NEEDLES[] = { "a", "X", "z", "one", "\n" };
        int si, ni;
        for (si = 0; si < 7; si++) {
            for (ni = 0; ni < 5; ni++) {
                cu32 n = sys_strnlen(SAMPLES[si], 4096u);
                int first = grep_find_line(SAMPLES[si], n, NEEDLES[ni]);
                int cnt = grep_count_lines(SAMPLES[si], n, NEEDLES[ni], 100);
                CHECK((first > 0) == (cnt > 0));
                if (first > 0) { CHECK(cnt >= 1); }
            }
        }
    }
}
