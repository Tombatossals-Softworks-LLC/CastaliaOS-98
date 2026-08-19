/*
 * test_trash.c - Recycle Bin bookkeeping (trash_core.c).
 *
 * The bug this exists to prevent already shipped: deleting was a rename into
 * one flat TRASH folder, and a name that was taken got resolved by DELETING
 * what was there. Two files called NOTES.TXT from two different folders, and
 * the first was gone -- permanently, while the status line said "Moved to
 * Trash" and Help promised deleting "moves to the Recycle Bin rather than
 * destroying anything".
 *
 * So the two things checked here are the two that lose files when wrong:
 * a colliding name must produce a DIFFERENT slot, and the index must give
 * back the exact path an item came from -- folder AND name, because the slot
 * name is not always the original name and NOTES~1.TXT put back as
 * NOTES~1.TXT is not a restore.
 */
#include "ctest.h"
#include "../src/apps/trash_core.h"
#include "castalia/sys.h"

void test_trash(void)
{
    char buf[512];
    char out[128];

    /* ---- candidate names ------------------------------------------------ */
    trash_candidate(out, sizeof out, "NOTES.TXT", 0);
    CHECK_STR(out, "NOTES.TXT");            /* n=0 is the name itself */

    trash_candidate(out, sizeof out, "NOTES.TXT", 1);
    CHECK_STR(out, "NOTES~1.TXT");
    trash_candidate(out, sizeof out, "NOTES.TXT", 2);
    CHECK_STR(out, "NOTES~2.TXT");
    /* ...and they differ, which is the whole point. */
    {
        char a[64], b[64];
        trash_candidate(a, sizeof a, "NOTES.TXT", 1);
        trash_candidate(b, sizeof b, "NOTES.TXT", 2);
        CHECK(sys_stricmp(a, b) != 0);
    }

    /*
     * The BASE is trimmed to fit 8.3, never the extension. A restored
     * NOTES~1.TX would stop being a text file: the extension is what the
     * shell opens it with, so it is the one part that may not be sacrificed.
     */
    trash_candidate(out, sizeof out, "LONGNAME.TXT", 1);
    CHECK_STR(out, "LONGNA~1.TXT");     /* 6 + "~1" = the full 8 */
    trash_candidate(out, sizeof out, "LONGNAME.TXT", 12);
    CHECK_STR(out, "LONGN~12.TXT");     /* 5 + "~12", still 8 */

    /*
     * ...and the same thing as a PROPERTY, over every tag from 1 to 30,
     * because the two strings above are only two points on a curve and I got
     * both of them wrong on the first attempt -- by five characters and by
     * four -- while the code was right. An invariant does not need me to do
     * the arithmetic correctly.
     */
    {
        int t;
        for (t = 1; t <= 30; t++) {
            int i, dot = -1, base;
            trash_candidate(out, sizeof out, "LONGNAME.TXT", t);
            for (i = 0; out[i] != '\0'; i++) {
                if (out[i] == '.' && i > 0) { dot = i; }
            }
            base = (dot >= 0) ? dot : (int)sys_strnlen(out, sizeof out);
            CHECK(base <= 8);                       /* fits 8.3          */
            CHECK(dot > 0);                         /* kept an extension */
            CHECK_STR(out + dot, ".TXT");           /* ...and the right one */
        }
    }

    /* Short names have room and keep all of it. */
    trash_candidate(out, sizeof out, "A.TXT", 1);
    CHECK_STR(out, "A~1.TXT");

    /* No extension: the tag still lands. */
    trash_candidate(out, sizeof out, "README", 1);
    CHECK_STR(out, "README~1");

    /* A leading dot is a NAME, not an empty base with an extension --
     * ".INI" must not become "~1.INI" and lose its identity. */
    trash_candidate(out, sizeof out, ".INI", 1);
    CHECK_STR(out, ".INI~1");

    /* NULL and a zero buffer are survivable. */
    trash_candidate(out, sizeof out, NULL, 1);
    CHECK_STR(out, "");
    trash_candidate(NULL, 0, "X", 1);

    /* ---- the index ------------------------------------------------------ */
    buf[0] = '\0';
    CHECK_EQI(trash_idx_count(buf), 0);
    CHECK(trash_idx_find(buf, "NOTES.TXT", out, sizeof out) == CFALSE);

    CHECK(trash_idx_add(buf, sizeof buf, "NOTES.TXT",
                        "C:/CASTALIA/DOCS/NOTES.TXT"));
    CHECK_EQI(trash_idx_count(buf), 1);
    CHECK(trash_idx_find(buf, "NOTES.TXT", out, sizeof out));
    CHECK_STR(out, "C:/CASTALIA/DOCS/NOTES.TXT");

    /*
     * The colliding case, end to end: the same NAME from a different folder is
     * stored under a different slot and remembers its own home -- including
     * the name it had, which the slot no longer carries. Restoring the second
     * one has to produce SYS/NOTES.TXT, not SYS/NOTES~1.TXT.
     */
    CHECK(trash_idx_add(buf, sizeof buf, "NOTES~1.TXT",
                        "C:/CASTALIA/SYS/NOTES.TXT"));
    CHECK_EQI(trash_idx_count(buf), 2);
    CHECK(trash_idx_find(buf, "NOTES.TXT", out, sizeof out));
    CHECK_STR(out, "C:/CASTALIA/DOCS/NOTES.TXT");   /* the first is UNCHANGED */
    CHECK(trash_idx_find(buf, "NOTES~1.TXT", out, sizeof out));
    CHECK_STR(out, "C:/CASTALIA/SYS/NOTES.TXT");

    /* A name that is a PREFIX of another must not match it: looking up
     * "NOTES.TXT" must never answer with "NOTES.TXT.BAK"'s home. */
    CHECK(trash_idx_add(buf, sizeof buf, "NOTES.TXT.BAK", "C:/BACKUP/N.BAK"));
    CHECK(trash_idx_find(buf, "NOTES.TXT", out, sizeof out));
    CHECK_STR(out, "C:/CASTALIA/DOCS/NOTES.TXT");
    CHECK(trash_idx_find(buf, "NOTES.TXT.BAK", out, sizeof out));
    CHECK_STR(out, "C:/BACKUP/N.BAK");

    /* Re-adding replaces rather than duplicating -- otherwise a slot reused
     * after an empty would have two homes and the older one might win. */
    CHECK(trash_idx_add(buf, sizeof buf, "NOTES.TXT", "C:/ELSEWHERE/N.TXT"));
    CHECK_EQI(trash_idx_count(buf), 3);
    CHECK(trash_idx_find(buf, "NOTES.TXT", out, sizeof out));
    CHECK_STR(out, "C:/ELSEWHERE/N.TXT");

    /* Removing one leaves the others intact. */
    trash_idx_remove(buf, sizeof buf, "NOTES~1.TXT");
    CHECK_EQI(trash_idx_count(buf), 2);
    CHECK(trash_idx_find(buf, "NOTES~1.TXT", out, sizeof out) == CFALSE);
    CHECK(trash_idx_find(buf, "NOTES.TXT", out, sizeof out));
    CHECK_STR(out, "C:/ELSEWHERE/N.TXT");
    CHECK(trash_idx_find(buf, "NOTES.TXT.BAK", out, sizeof out));
    CHECK_STR(out, "C:/BACKUP/N.BAK");

    /* Removing something that is not there changes nothing. */
    trash_idx_remove(buf, sizeof buf, "GHOST.TXT");
    CHECK_EQI(trash_idx_count(buf), 2);

    /* An entry with no path is not an answer: restoring to "" would drop the
     * file at the root of nowhere, so it must read as unknown. */
    {
        char b2[64];
        b2[0] = '\0';
        sys_strlcpy(b2, "ORPHAN.TXT|\n", sizeof b2);
        CHECK(trash_idx_find(b2, "ORPHAN.TXT", out, sizeof out) == CFALSE);
    }

    /* A full buffer refuses rather than truncating a path: half a path is a
     * restore to the wrong place, which is worse than no restore. */
    {
        char small[24];
        small[0] = '\0';
        CHECK(trash_idx_add(small, sizeof small, "A.TXT", "C:/A") == CTRUE);
        CHECK(trash_idx_add(small, sizeof small, "B.TXT",
                            "C:/A/VERY/LONG/PATH") == CFALSE);
        /* ...and the first entry survived the refusal. */
        CHECK(trash_idx_find(small, "A.TXT", out, sizeof out));
        CHECK_STR(out, "C:/A");
    }

    /* NULL is survivable everywhere. */
    CHECK(trash_idx_add(NULL, 10, "A", "B") == CFALSE);
    CHECK(trash_idx_find(NULL, "A", out, sizeof out) == CFALSE);
    CHECK_EQI(trash_idx_count(NULL), 0);
    trash_idx_remove(NULL, 0, "A");

    /*
     * ---- the index file is not an item in the bin -----------------------
     *
     * TRASH.IDX sits in TRASH next to the deleted files, so every "what is in
     * the bin" question has to know to skip it. Miss it and the desktop's bin
     * icon stays full after the last file is restored, Empty Recycle Bin
     * offers to delete one item from a bin you can see is empty, and Restore
     * is offered on the bookkeeping itself.
     */
    CHECK(trash_is_index("TRASH.IDX"));
    CHECK(trash_is_index("trash.idx"));      /* DOS names are case-blind */
    CHECK(trash_is_index("Trash.Idx"));
    CHECK(trash_is_index("TRASH.TXT") == CFALSE);
    CHECK(trash_is_index("TRASH") == CFALSE);
    CHECK(trash_is_index("TRASH.IDX.BAK") == CFALSE);
    CHECK(trash_is_index("NOTES.TXT") == CFALSE);
    CHECK(trash_is_index("") == CFALSE);
    CHECK(trash_is_index(NULL) == CFALSE);
}
