/*
 * test_ren.c - Renaming many files at once (ren_core.c).
 *
 * A batch rename is the one operation where being nearly right is worse than
 * refusing. Rename half a folder and stop, and the folder is in a state nobody
 * asked for; produce the same new name for two different files and one of them
 * is gone. So the cases here are the ones that end that way: patterns that
 * overflow 8.3, patterns that empty a name, and two sources that collide.
 */
#include "ctest.h"
#include "../src/apps/ren_core.h"
#include "castalia/sys.h"

/* Apply and return the result, so the tests read as names rather than as
 * out-parameters. Returns "" when the rename is refused. */
static const char *ap(const char *name, const char *from, const char *to)
{
    static char out[64];
    if (!ren_apply(name, from, to, out, sizeof out)) { out[0] = '\0'; }
    return out;
}

void test_ren(void)
{
    /* ---- matching -------------------------------------------------------- */
    CHECK(ren_match("NOTES.TXT", "*.TXT"));
    CHECK(ren_match("notes.txt", "*.TXT"));        /* case-insensitive */
    CHECK(ren_match("NOTES.TXT", "*.txt"));
    CHECK(!ren_match("NOTES.DOC", "*.TXT"));
    CHECK(ren_match("NOTES.TXT", "NOTES.TXT"));
    CHECK(ren_match("NOTES.TXT", "N*.T*"));

    /*
     * A pattern with no dot says nothing about the extension. If "*" meant
     * "files with no extension" then the commonest pattern anyone types would
     * be the one that matches almost nothing.
     */
    CHECK(ren_match("NOTES.TXT", "*"));
    CHECK(ren_match("README", "*"));
    CHECK(ren_match("NOTES.TXT", "N*"));
    /* ...but a pattern that DOES name an extension means it. */
    CHECK(!ren_match("README", "*.TXT"));
    CHECK(ren_match("README", "*."));

    /* '?' is one character... */
    CHECK(ren_match("A1.TXT", "A?.TXT"));
    CHECK(!ren_match("A12.TXT", "A?.TXT"));
    /* ...or NONE at the end of a component, the way DOS does it. Miss this
     * and `ren REPORT?.DOC` skips REPORT.DOC, which is the file the person
     * was most likely looking at. */
    CHECK(ren_match("A.TXT", "A?.TXT"));
    CHECK(ren_match("REPORT.DOC", "REPORT?.DOC"));

    CHECK(!ren_match(NULL, "*"));
    CHECK(!ren_match("A.TXT", NULL));
    CHECK(!ren_match("", "*"));
    CHECK(!ren_match("A.TXT", ""));

    /* ---- the everyday renames ------------------------------------------- */
    CHECK_STR(ap("NOTES.TXT", "*.TXT", "*.BAK"), "NOTES.BAK");
    CHECK_STR(ap("A.TXT", "*.TXT", "*.BAK"), "A.BAK");
    /* A literal in the TO pattern replaces, position by position against the
     * SOURCE -- ABC.TXT becomes BBC.TXT, not BABC.TXT. */
    CHECK_STR(ap("ABC.TXT", "A*.TXT", "B*.TXT"), "BBC.TXT");
    CHECK_STR(ap("REPORT1.DOC", "*", "OLD*"), "OLDORT1.DOC");
    /* '?' copies the source character at the same position. */
    CHECK_STR(ap("A1.TXT", "A?.TXT", "B?.TXT"), "B1.TXT");

    /*
     * No dot in the TO pattern keeps the extension the file already had, so
     * `ren *.TXT BACKUP` does not quietly strip ".TXT" off everything.
     */
    CHECK_STR(ap("NOTES.TXT", "*.TXT", "BACKUP"), "BACKUP.TXT");
    CHECK_STR(ap("README", "*", "NOTES"), "NOTES");
    /* ...and a TO pattern ending in a bare dot DOES drop it, which is how
     * DOS spelled "remove the extension". */
    CHECK_STR(ap("NOTES.TXT", "*.TXT", "*."), "NOTES");

    /*
     * A '*' ENDS its component: what follows has nowhere to go, because the
     * star has already taken the rest of the source. Without this, `*X*` means
     * "the name, then X, then part of the name again" -- which nobody types on
     * purpose and which quietly changes what a pattern does.
     */
    /* base: the star takes "ABCD" and the "X*" after it is dropped. Without
     * the rule this is "ABCDXCD" -- the name, then X, then part of the name. */
    CHECK_STR(ap("ABCD.TXT", "*.TXT", "*X*.BAK"), "ABCD.BAK");
    /* extension: 'B' is a literal, the star takes "XT" from position 1, and
     * the trailing 'K' is dropped. Without the rule this is "BXTK". */
    CHECK_STR(ap("ABCD.TXT", "*.TXT", "*.B*K"), "ABCD.BXT");

    /* ---- what must be REFUSED ------------------------------------------- */
    /*
     * Over 8.3 is refused, not clipped. Two files clipped to the same name is
     * exactly the accident this whole module exists to prevent, and a clip is
     * the quietest way to arrange it.
     */
    CHECK_STR(ap("REPORT1.DOC", "*", "VERYLONGNAME*"), "");
    CHECK_STR(ap("A.TXT", "*.TXT", "*.TOOLONG"), "");
    /*
     * A SOURCE that is already longer than 8.3 is refused too, not clipped.
     * The host filesystem hands out long names and the File Manager lists
     * them, so `ren * *.BAK` over a folder holding LONGNAME1.TXT and
     * LONGNAME2.TXT would otherwise clip both to LONGNAME.BAK -- one file
     * where there were two, with nothing said.
     */
    CHECK_STR(ap("ABCDEFGHIJ.TXT", "*.TXT", "*.BAK"), "");
    CHECK_STR(ap("LONGNAME1.TXT", "*", "*"), "");
    /* exactly 8 is fine -- the checks above would pass if everything were
     * refused */
    CHECK_STR(ap("ABCDEFGH.TXT", "*", "*"), "ABCDEFGH.TXT");
    CHECK_STR(ap("A.TXT", "*.TXT", "ABCDEFGH.XYZ"), "ABCDEFGH.XYZ");
    CHECK_STR(ap("ABCDEFGH.TXT", "*.TXT", "*.BAK"), "ABCDEFGH.BAK");

    /* A name that would come out empty is not a rename. */
    CHECK_STR(ap("NOTES.TXT", "*.TXT", ".BAK"), "");
    /* Nor is one that could move the file somewhere else. */
    CHECK_STR(ap("NOTES.TXT", "*.TXT", "..\\*.TXT"), "");
    CHECK_STR(ap("NOTES.TXT", "*.TXT", "SUB/*.TXT"), "");
    CHECK_STR(ap("NOTES.TXT", "*.TXT", "C:*.TXT"), "");

    CHECK_STR(ap(NULL, "*", "*"), "");
    CHECK_STR(ap("A.TXT", "*", NULL), "");
    CHECK_STR(ap("A.TXT", "*", ""), "");
    {
        char tiny[4];
        CHECK(!ren_apply("NOTES.TXT", "*.TXT", "*.BAK", tiny, sizeof tiny));
        CHECK(!ren_apply("NOTES.TXT", "*.TXT", "*.BAK", tiny, 0));
    }

    /* ---- the collision, which is the point ------------------------------ */
    {
        /* A flat array, the shape the caller keeps its plan in. */
        static char plan[6][REN_NAME_MAX];
        sys_strlcpy(plan[0], "A.BAK", REN_NAME_MAX);
        sys_strlcpy(plan[1], "B.BAK", REN_NAME_MAX);
        sys_strlcpy(plan[2], "C.BAK", REN_NAME_MAX);
        CHECK_EQI(ren_first_collision(plan[0], 3, (cu32)REN_NAME_MAX), -1);

        /* Two sources that land on one name: the second is reported. */
        sys_strlcpy(plan[2], "A.BAK", REN_NAME_MAX);
        CHECK_EQI(ren_first_collision(plan[0], 3, (cu32)REN_NAME_MAX), 2);

        /* Case does not save you -- on DOS these are the same file. */
        sys_strlcpy(plan[2], "a.bak", REN_NAME_MAX);
        CHECK_EQI(ren_first_collision(plan[0], 3, (cu32)REN_NAME_MAX), 2);

        /* The FIRST collision is reported, not the last. */
        sys_strlcpy(plan[1], "A.BAK", REN_NAME_MAX);
        sys_strlcpy(plan[2], "C.BAK", REN_NAME_MAX);
        sys_strlcpy(plan[3], "C.BAK", REN_NAME_MAX);
        CHECK_EQI(ren_first_collision(plan[0], 4, (cu32)REN_NAME_MAX), 1);

        CHECK_EQI(ren_first_collision(plan[0], 1, (cu32)REN_NAME_MAX), -1);
        CHECK_EQI(ren_first_collision(plan[0], 0, (cu32)REN_NAME_MAX), -1);
        CHECK_EQI(ren_first_collision(NULL, 3, (cu32)REN_NAME_MAX), -1);
        CHECK_EQI(ren_first_collision(plan[0], 3, 0u), -1);
    }

    /*
     * ...and the collision a real batch actually produces, which is why the
     * check is not optional.
     *
     * `ren * ARCHIVED*` is a perfectly legal pattern and every one of its
     * results is a perfectly legal 8.3 name. The star sits at pattern position
     * 8, the sources are seven characters long, so it copies nothing and EVERY
     * file in the folder comes out as ARCHIVED.DOC. Nothing here is truncated
     * and nothing is refused -- the batch is simply an instruction to delete
     * the folder one overwrite at a time, and only ren_first_collision can
     * tell the caller that before it starts.
     */
    {
        static char plan[3][REN_NAME_MAX];
        CHECK(ren_apply("REPORT1.DOC", "*", "ARCHIVED*", plan[0], REN_NAME_MAX));
        CHECK(ren_apply("REPORT2.DOC", "*", "ARCHIVED*", plan[1], REN_NAME_MAX));
        CHECK(ren_apply("REPORT3.DOC", "*", "ARCHIVED*", plan[2], REN_NAME_MAX));
        CHECK_STR(plan[0], "ARCHIVED.DOC");
        CHECK_STR(plan[1], "ARCHIVED.DOC");
        CHECK_EQI(ren_first_collision(plan[0], 3, (cu32)REN_NAME_MAX), 1);
    }

    /*
     * A prefix SHORT enough for the star to still reach the digit keeps every
     * result distinct -- so the check above is about THIS batch, not about the
     * pattern being long. A collision check that fired on both would be no
     * more useful than one that fired on neither.
     *
     * Note how narrow the margin is. "REPORT1" is seven characters, so a star
     * at position 6 copies "1" and a star at position 7 copies nothing: SIX
     * literals rename every file to its own name, SEVEN rename every file to
     * the same name. Nothing about the pattern says which one you typed, and
     * that is the whole argument for planning the batch before running it.
     */
    {
        static char plan[3][REN_NAME_MAX];
        CHECK(ren_apply("REPORT1.DOC", "*", "ARCHIV*", plan[0], REN_NAME_MAX));
        CHECK(ren_apply("REPORT2.DOC", "*", "ARCHIV*", plan[1], REN_NAME_MAX));
        CHECK(ren_apply("REPORT3.DOC", "*", "ARCHIV*", plan[2], REN_NAME_MAX));
        CHECK_STR(plan[0], "ARCHIV1.DOC");
        CHECK_STR(plan[2], "ARCHIV3.DOC");
        CHECK_EQI(ren_first_collision(plan[0], 3, (cu32)REN_NAME_MAX), -1);
        /* one literal more and all three become one file */
        CHECK_STR(ap("REPORT1.DOC", "*", "ARCHIVE*"), "ARCHIVE.DOC");
        CHECK_STR(ap("REPORT2.DOC", "*", "ARCHIVE*"), "ARCHIVE.DOC");
    }

    /*
     * A TO pattern longer than the name it is applied to used to index the
     * SOURCE by the output position, which walks off the end of it: eight
     * literals put the star at output position 8 against a seven-character
     * name, and it read whatever followed on the stack. The results above
     * depend on that being fixed; this is the shortest source that reaches it.
     */
    CHECK_STR(ap("A.DOC", "*", "ARCHIVED*"), "ARCHIVED.DOC");
    CHECK_STR(ap("A.DOC", "*", "ABCDEFGHIJ*"), "");   /* past 8: refused */
}
