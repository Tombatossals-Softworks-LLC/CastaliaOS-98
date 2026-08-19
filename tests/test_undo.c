/*
 * test_undo.c - Undo and redo over a text buffer (undo_core.c).
 *
 * An undo stack fails silently by construction: the document after a wrong
 * undo is still a document. It has the right shape, it opens, it saves, and
 * the characters are simply not the ones the author had. So the checks here
 * are all against the exact buffer contents, never against "it did something".
 *
 * The cases that actually break implementations, in order of how often:
 *
 *   - undoing a DELETE without having kept the deleted text (the buffer is
 *     restored to the right LENGTH with the wrong characters in it);
 *   - a new edit after undoing, which must discard the abandoned redo branch
 *     rather than leaving it to put back text that has since been replaced;
 *   - the stack filling up, where the oldest edit is dropped and the indices
 *     have to still line up.
 */
#include "ctest.h"
#include "../src/apps/undo_core.h"
#include <string.h>

/* Apply an insert to the buffer the way an editor would, then record it. */
static void do_insert(UndoStack *u, char *b, int *n, int cap, int pos,
                      const char *s)
{
    int len = (int)strlen(s), i;
    for (i = *n - 1; i >= pos; i--) { b[i + len] = b[i]; }
    for (i = 0; i < len; i++) { b[pos + i] = s[i]; }
    *n += len;
    b[*n] = '\0';
    undo_record(u, UNDO_INSERT, pos, s, len);
    CASTALIA_UNUSED(cap);
}

static void do_delete(UndoStack *u, char *b, int *n, int pos, int len)
{
    char gone[UNDO_TEXT_MAX];
    int i;
    for (i = 0; i < len; i++) { gone[i] = b[pos + i]; }
    for (i = pos; i + len < *n; i++) { b[i] = b[i + len]; }
    *n -= len;
    b[*n] = '\0';
    undo_record(u, UNDO_DELETE, pos, gone, len);
}

void test_undo(void)
{
    UndoStack u;
    char b[512];
    int n;

    printf("- undo / redo\n");

    /* ---- nothing to undo ------------------------------------------------ */
    undo_init(&u);
    CHECK(!undo_can_undo(&u));
    CHECK(!undo_can_redo(&u));
    n = 0; b[0] = '\0';
    CHECK_EQI(undo_undo(&u, b, &n, (int)sizeof b), -1);
    CHECK_EQI(undo_redo(&u, b, &n, (int)sizeof b), -1);

    /* ---- an insert, undone and redone ----------------------------------- */
    undo_init(&u);
    n = 0; b[0] = '\0';
    do_insert(&u, b, &n, (int)sizeof b, 0, "Hello");
    CHECK_STR(b, "Hello");
    CHECK(undo_can_undo(&u));
    CHECK(!undo_can_redo(&u));
    CHECK_EQI(undo_undo(&u, b, &n, (int)sizeof b), 0);
    b[n] = '\0';
    CHECK_STR(b, "");
    CHECK(!undo_can_undo(&u));
    CHECK(undo_can_redo(&u));
    CHECK(undo_redo(&u, b, &n, (int)sizeof b) >= 0);
    b[n] = '\0';
    CHECK_STR(b, "Hello");

    /* ---- a DELETE, undone: the characters must come BACK ---------------- *
     *
     * This is the case an implementation that only records positions and
     * lengths gets wrong. The length is restored either way; only the text
     * tells you whether the right characters came back.
     */
    undo_init(&u);
    n = 0; b[0] = '\0';
    do_insert(&u, b, &n, (int)sizeof b, 0, "The quick brown fox");
    do_delete(&u, b, &n, 4, 6);                  /* remove "quick " */
    CHECK_STR(b, "The brown fox");
    CHECK_EQI(undo_undo(&u, b, &n, (int)sizeof b), 4);
    b[n] = '\0';
    CHECK_STR(b, "The quick brown fox");         /* not "The       brown fox" */
    CHECK(undo_redo(&u, b, &n, (int)sizeof b) >= 0);
    b[n] = '\0';
    CHECK_STR(b, "The brown fox");

    /* ---- several edits, unwound in order -------------------------------- */
    undo_init(&u);
    n = 0; b[0] = '\0';
    do_insert(&u, b, &n, (int)sizeof b, 0, "one");
    do_insert(&u, b, &n, (int)sizeof b, 3, " two");
    do_insert(&u, b, &n, (int)sizeof b, 7, " three");
    CHECK_STR(b, "one two three");
    undo_undo(&u, b, &n, (int)sizeof b); b[n] = '\0';
    CHECK_STR(b, "one two");
    undo_undo(&u, b, &n, (int)sizeof b); b[n] = '\0';
    CHECK_STR(b, "one");
    undo_undo(&u, b, &n, (int)sizeof b); b[n] = '\0';
    CHECK_STR(b, "");
    CHECK(!undo_can_undo(&u));
    /* ...and wound back up again. */
    undo_redo(&u, b, &n, (int)sizeof b);
    undo_redo(&u, b, &n, (int)sizeof b);
    undo_redo(&u, b, &n, (int)sizeof b); b[n] = '\0';
    CHECK_STR(b, "one two three");
    CHECK(!undo_can_redo(&u));

    /* ---- a new edit after undoing forks the timeline -------------------- *
     *
     * The abandoned branch must go. Keeping it lets a later redo put back
     * text the author has already replaced, which is the most alarming way an
     * undo stack can misbehave: characters appear that were never typed at
     * that point in the document's life.
     */
    undo_init(&u);
    n = 0; b[0] = '\0';
    do_insert(&u, b, &n, (int)sizeof b, 0, "cat");
    do_insert(&u, b, &n, (int)sizeof b, 3, " and dog");
    CHECK_STR(b, "cat and dog");
    undo_undo(&u, b, &n, (int)sizeof b); b[n] = '\0';
    CHECK_STR(b, "cat");
    CHECK(undo_can_redo(&u));
    do_insert(&u, b, &n, (int)sizeof b, 3, " and bird");
    CHECK_STR(b, "cat and bird");
    CHECK(!undo_can_redo(&u));      /* " and dog" is gone for good */
    undo_undo(&u, b, &n, (int)sizeof b); b[n] = '\0';
    CHECK_STR(b, "cat");
    CHECK(undo_can_redo(&u));
    undo_redo(&u, b, &n, (int)sizeof b); b[n] = '\0';
    CHECK_STR(b, "cat and bird");   /* and NOT "cat and dog" */
    /*
     * ...and all the way back to empty. This is the check that catches a
     * stack which does not actually DROP the abandoned branch but merely
     * hides it: undoing once lands on "cat" either way, and only the SECOND
     * undo reaches the record that should no longer exist. Left in place, it
     * is an insert of " and dog" against a three-character buffer, so the
     * undo fails and the document stops moving -- with every earlier check
     * still green. Measured: without the discard this section passes and this
     * pair does not.
     */
    undo_undo(&u, b, &n, (int)sizeof b); b[n] = '\0';
    CHECK_STR(b, "cat");
    CHECK(undo_can_undo(&u));
    undo_undo(&u, b, &n, (int)sizeof b); b[n] = '\0';
    CHECK_STR(b, "");
    CHECK(!undo_can_undo(&u));

    /* ---- the stack fills up --------------------------------------------- */
    {
        int i;
        undo_init(&u);
        n = 0; b[0] = '\0';
        /* One more edit than the stack holds. */
        for (i = 0; i < UNDO_LEVELS + 8; i++) {
            do_insert(&u, b, &n, (int)sizeof b, n, "x");
        }
        CHECK_EQI(n, UNDO_LEVELS + 8);
        /* Only UNDO_LEVELS of them can be taken back. */
        i = 0;
        while (undo_can_undo(&u)) {
            CHECK(undo_undo(&u, b, &n, (int)sizeof b) >= 0);
            i++;
            CHECK(i <= UNDO_LEVELS);      /* never more than it holds */
        }
        CHECK_EQI(i, UNDO_LEVELS);
        /* The eight oldest 'x' remain, because their history was dropped. */
        b[n] = '\0';
        CHECK_EQI(n, 8);
        CHECK_STR(b, "xxxxxxxx");
        /* And redo walks all the way back up. */
        i = 0;
        while (undo_can_redo(&u)) {
            CHECK(undo_redo(&u, b, &n, (int)sizeof b) >= 0);
            i++;
            CHECK(i <= UNDO_LEVELS);
        }
        CHECK_EQI(n, UNDO_LEVELS + 8);
    }

    /* ---- an edit too big to remember clears the history ------------------ *
     *
     * Half a record is worse than none: undoing it would take back the wrong
     * number of characters and the document would still look like a document.
     */
    {
        char big[UNDO_TEXT_MAX + 40];
        int i;
        for (i = 0; i < (int)sizeof big - 1; i++) { big[i] = 'z'; }
        big[sizeof big - 1] = '\0';
        undo_init(&u);
        n = 0; b[0] = '\0';
        do_insert(&u, b, &n, (int)sizeof b, 0, "keep");
        CHECK(undo_can_undo(&u));
        undo_record(&u, UNDO_INSERT, 0, big, (int)sizeof big - 1);
        CHECK(!undo_can_undo(&u));      /* refused, and the rest went too */
        CHECK(!undo_can_redo(&u));
    }

    /* ---- cell undo, for the spreadsheet --------------------------------- *
     *
     * Same two properties as above, checked the same way: the OLD text comes
     * back, and a new edit after undoing discards the branch. A sheet is the
     * worst place for a silent undo bug -- every cell still recalculates, so
     * a wrong restore propagates through the formulas and the result is a
     * page of plausible numbers.
     */
    {
        CellUndo cu;
        const char *t;
        int sh = -1, r = -1, c = -1;

        cundo_init(&cu);
        CHECK(!cundo_can_undo(&cu));
        CHECK(!cundo_can_redo(&cu));
        CHECK(cundo_undo(&cu, &sh, &r, &c) == NULL);

        /* B2 held nothing, now holds a formula. */
        cundo_record(&cu, 0, 1, 1, "", "=SUM(A1:A5)");
        CHECK(cundo_can_undo(&cu));
        t = cundo_undo(&cu, &sh, &r, &c);
        CHECK(t != NULL);
        CHECK_STR(t, "");
        CHECK_EQI(sh, 0); CHECK_EQI(r, 1); CHECK_EQI(c, 1);
        /* ...and redo puts the formula back, not something else. */
        t = cundo_redo(&cu, &sh, &r, &c);
        CHECK(t != NULL);
        CHECK_STR(t, "=SUM(A1:A5)");

        /* Overwriting a cell that HELD something: the old text must return,
         * which is the case a location-only record gets wrong. */
        cundo_init(&cu);
        cundo_record(&cu, 1, 3, 2, "12.5", "99");
        t = cundo_undo(&cu, &sh, &r, &c);
        CHECK_STR(t, "12.5");        /* not "" */
        CHECK_EQI(sh, 1); CHECK_EQI(r, 3); CHECK_EQI(c, 2);

        /* Setting a cell to what it already held is not an edit at all --
         * otherwise every re-entered value costs a wasted Ctrl+Z. */
        cundo_init(&cu);
        cundo_record(&cu, 0, 0, 0, "same", "same");
        CHECK(!cundo_can_undo(&cu));

        /* The branch is discarded on a new edit. */
        cundo_init(&cu);
        cundo_record(&cu, 0, 0, 0, "", "one");
        cundo_record(&cu, 0, 0, 1, "", "two");
        cundo_undo(&cu, &sh, &r, &c);
        CHECK(cundo_can_redo(&cu));
        cundo_record(&cu, 0, 0, 2, "", "three");
        CHECK(!cundo_can_redo(&cu));
        /* Undoing twice reaches the first edit, not the abandoned one. */
        t = cundo_undo(&cu, &sh, &r, &c);
        CHECK_STR(t, "");
        CHECK_EQI(c, 2);
        t = cundo_undo(&cu, &sh, &r, &c);
        CHECK_STR(t, "");
        CHECK_EQI(c, 0);
        CHECK(!cundo_can_undo(&cu));

        /* Overfilling drops the oldest and keeps the rest usable. */
        {
            int i, k = 0;
            cundo_init(&cu);
            for (i = 0; i < CUNDO_LEVELS + 5; i++) {
                cundo_record(&cu, 0, i, 0, "old", "new");
            }
            while (cundo_can_undo(&cu)) {
                CHECK(cundo_undo(&cu, &sh, &r, &c) != NULL);
                k++;
                CHECK(k <= CUNDO_LEVELS);
            }
            CHECK_EQI(k, CUNDO_LEVELS);
        }

        /* Refusals. */
        cundo_init(NULL);
        cundo_record(NULL, 0, 0, 0, "a", "b");
        CHECK(!cundo_can_undo(NULL));
        CHECK(!cundo_can_redo(NULL));
        CHECK(cundo_undo(NULL, &sh, &r, &c) == NULL);
        CHECK(cundo_redo(NULL, &sh, &r, &c) == NULL);
        /* NULL text is an empty cell, not a crash. */
        cundo_init(&cu);
        cundo_record(&cu, 0, 0, 0, NULL, "x");
        t = cundo_undo(&cu, &sh, &r, &c);
        CHECK(t != NULL);
        CHECK_STR(t, "");
        /* And the out-parameters are optional. */
        cundo_init(&cu);
        cundo_record(&cu, 0, 2, 2, "a", "b");
        CHECK(cundo_undo(&cu, NULL, NULL, NULL) != NULL);
    }

    /* ---- attributed text, for CastaliaWrite ----------------------------- *
     *
     * The whole reason this third stack exists: the attribute bytes must come
     * back with the characters. An undo that restores "important" without the
     * bold is a partial undo wearing the costume of a complete one -- the
     * words are right, the emphasis is gone, and the document still reads.
     */
    {
        WUndoStack wu;
        const WUndoRec *w;
        char t[8];
        unsigned char at[8];
        int i;

        for (i = 0; i < 5; i++) { t[i] = "bold!"[i]; at[i] = 0x01; }

        wundo_init(&wu);
        CHECK(!wundo_can_undo(&wu));
        CHECK(wundo_undo_step(&wu) == NULL);
        CHECK(wundo_redo_step(&wu) == NULL);

        wundo_record(&wu, UNDO_DELETE, 12, t, at, 5);
        CHECK(wundo_can_undo(&wu));
        w = wundo_undo_step(&wu);
        CHECK(w != NULL);
        CHECK_EQI(w->kind, UNDO_DELETE);
        CHECK_EQI(w->pos, 12);
        CHECK_EQI(w->len, 5);
        CHECK_EQI(w->text[0], 'b');
        CHECK_EQI(w->text[4], '!');
        /* The bold survives the round trip. This is the check the flat stack
         * cannot make, because it has nowhere to put this byte. */
        for (i = 0; i < 5; i++) { CHECK_EQI(w->attr[i], 0x01); }
        CHECK(!wundo_can_undo(&wu));
        CHECK(wundo_can_redo(&wu));
        w = wundo_redo_step(&wu);
        CHECK(w != NULL);
        CHECK_EQI(w->attr[2], 0x01);

        /* No attributes supplied means PLAIN, not whatever was on the stack:
         * a half-filled record would restore random emphasis. */
        wundo_init(&wu);
        wundo_record(&wu, UNDO_INSERT, 0, "abc", NULL, 3);
        w = wundo_undo_step(&wu);
        CHECK(w != NULL);
        for (i = 0; i < 3; i++) { CHECK_EQI(w->attr[i], 0); }

        /* Fork, overflow and refusals, as for the other two. */
        wundo_init(&wu);
        wundo_record(&wu, UNDO_INSERT, 0, "a", at, 1);
        wundo_record(&wu, UNDO_INSERT, 1, "b", at, 1);
        wundo_undo_step(&wu);
        CHECK(wundo_can_redo(&wu));
        wundo_record(&wu, UNDO_INSERT, 1, "c", at, 1);
        CHECK(!wundo_can_redo(&wu));
        w = wundo_undo_step(&wu);
        CHECK_EQI(w->text[0], 'c');
        w = wundo_undo_step(&wu);
        CHECK_EQI(w->text[0], 'a');     /* not the abandoned 'b' */
        CHECK(!wundo_can_undo(&wu));

        {
            int k = 0;
            wundo_init(&wu);
            for (i = 0; i < WUNDO_LEVELS + 6; i++) {
                wundo_record(&wu, UNDO_INSERT, i, "x", at, 1);
            }
            while (wundo_can_undo(&wu)) {
                CHECK(wundo_undo_step(&wu) != NULL);
                k++;
                CHECK(k <= WUNDO_LEVELS);
            }
            CHECK_EQI(k, WUNDO_LEVELS);
        }

        wundo_init(NULL);
        wundo_record(NULL, UNDO_INSERT, 0, "a", at, 1);
        CHECK(!wundo_can_undo(NULL));
        CHECK(wundo_undo_step(NULL) == NULL);
        CHECK(wundo_redo_step(NULL) == NULL);
        wundo_init(&wu);
        wundo_record(&wu, UNDO_INSERT, 0, NULL, at, 1);
        CHECK(!wundo_can_undo(&wu));
        wundo_record(&wu, 99, 0, "a", at, 1);
        CHECK(!wundo_can_undo(&wu));
        wundo_record(&wu, UNDO_INSERT, 0, "a", at, UNDO_TEXT_MAX + 1);
        CHECK(!wundo_can_undo(&wu));
    }

    /* ---- refusals -------------------------------------------------------- */
    undo_init(NULL);
    undo_record(NULL, UNDO_INSERT, 0, "a", 1);
    CHECK(!undo_can_undo(NULL));
    CHECK(!undo_can_redo(NULL));
    CHECK_EQI(undo_undo(NULL, b, &n, 10), -1);
    CHECK_EQI(undo_redo(NULL, b, &n, 10), -1);
    undo_init(&u);
    undo_record(&u, UNDO_INSERT, 0, NULL, 4);
    CHECK(!undo_can_undo(&u));
    undo_record(&u, UNDO_INSERT, -1, "a", 1);
    CHECK(!undo_can_undo(&u));
    undo_record(&u, UNDO_INSERT, 0, "a", 0);
    CHECK(!undo_can_undo(&u));
    undo_record(&u, 99, 0, "a", 1);        /* not a kind this knows */
    CHECK(!undo_can_undo(&u));
    /* An undo that will not fit the buffer refuses rather than overrunning. */
    undo_init(&u);
    n = 0; b[0] = '\0';
    do_insert(&u, b, &n, (int)sizeof b, 0, "abcd");
    do_delete(&u, b, &n, 0, 4);
    CHECK_EQI(undo_undo(&u, b, &n, 2), -1);   /* capacity 2, needs 4 */
}
