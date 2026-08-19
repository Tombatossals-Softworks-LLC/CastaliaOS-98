/*
 * test_rev.c - The rules of Reversi (rev_core.c).
 *
 * Reversi is unusually hostile to being checked by looking at it. Every
 * position is a grid of black and white discs, so a board produced by a
 * broken rule is indistinguishable from a board produced by a correct one --
 * there is no crash, no gap, nothing out of place. The game simply becomes a
 * different game, and the player assumes they misunderstood it.
 *
 * So the rules are driven here directly, and the three that carry the risk
 * get a test each:
 *
 *   - a move must FLIP something, and a run that reaches the edge closes
 *     nothing;
 *   - each of the eight directions is independent;
 *   - passing, and the end condition that is NOT "the board is full".
 */
#include "ctest.h"
#include "../src/apps/rev_core.h"

/* Empty the board so a position can be built by hand. */
static void clear(RevBoard *b)
{
    int r, c;
    for (r = 0; r < REV_N; r++) {
        for (c = 0; c < REV_N; c++) { b->cell[r][c] = (unsigned char)REV_EMPTY; }
    }
    b->turn = REV_BLACK;
    b->passes = 0;
}

void test_rev(void)
{
    RevBoard b;

    /* ---- the opening ---------------------------------------------------- */
    rev_init(&b);
    {
        int bl = 0, wh = 0;
        rev_score(&b, &bl, &wh);
        CHECK_EQI(bl, 2);
        CHECK_EQI(wh, 2);
        CHECK_EQI(b.turn, REV_BLACK);
        /* Black has exactly four opening moves, and by symmetry they are the
         * same move. A board set up with the diagonals swapped gives four
         * different ones, still looks correct, and is a different game. */
        CHECK_EQI(rev_move_count(&b, REV_BLACK), 4);
        CHECK_EQI(rev_move_count(&b, REV_WHITE), 4);
        CHECK(rev_legal(&b, 2, 3, REV_BLACK));
        CHECK(rev_legal(&b, 3, 2, REV_BLACK));
        CHECK(rev_legal(&b, 4, 5, REV_BLACK));
        CHECK(rev_legal(&b, 5, 4, REV_BLACK));
        /* ...and the diagonal neighbours of the centre are not moves. */
        CHECK(rev_legal(&b, 2, 2, REV_BLACK) == CFALSE);
        CHECK(rev_legal(&b, 5, 5, REV_BLACK) == CFALSE);
    }

    /* ---- a move must flip something ------------------------------------- */
    /*
     * The single most common way to get Reversi wrong is to accept any empty
     * square next to an enemy stone. The board that produces is still a board.
     */
    clear(&b);
    b.cell[4][4] = (unsigned char)REV_WHITE;   /* one lone white stone */
    b.turn = REV_BLACK;
    CHECK_EQI(rev_would_flip(&b, 4, 3, REV_BLACK), 0);  /* nothing behind it */
    CHECK(rev_legal(&b, 4, 3, REV_BLACK) == CFALSE);
    CHECK_EQI(rev_play(&b, 4, 3), 0);
    /* ...and the board is untouched, including whose turn it is. A rejected
     * move that advanced the turn hands the game over for a mis-click. */
    CHECK_EQI((int)b.cell[4][3], REV_EMPTY);
    CHECK_EQI(b.turn, REV_BLACK);

    /* An occupied square is never a move, even a useful-looking one. */
    CHECK_EQI(rev_would_flip(&b, 4, 4, REV_BLACK), 0);
    /* Nor is anything off the board -- and asking must not read past it. */
    CHECK_EQI(rev_would_flip(&b, -1, 4, REV_BLACK), 0);
    CHECK_EQI(rev_would_flip(&b, 8, 4, REV_BLACK), 0);
    CHECK_EQI(rev_would_flip(&b, 4, -1, REV_BLACK), 0);
    CHECK_EQI(rev_would_flip(&b, 4, 8, REV_BLACK), 0);

    /* ---- a run that reaches the edge closes nothing ---------------------- */
    /*
     * Black at (0,0), whites filling the rest of row 0. There is no black
     * stone to close the run, so playing along it flips nothing -- an
     * implementation that walks to the edge and flips what it passed turns
     * the whole row over and looks entirely reasonable doing it.
     */
    clear(&b);
    {
        int c;
        for (c = 1; c < REV_N; c++) { b.cell[0][c] = (unsigned char)REV_WHITE; }
        b.cell[0][0] = (unsigned char)REV_EMPTY;
        b.turn = REV_BLACK;
        /* (0,0) is empty, the run to its right is all white, and it ends at
         * the edge with no black stone: nothing closes it. */
        CHECK_EQI(rev_would_flip(&b, 0, 0, REV_BLACK), 0);
        /* Put a black stone at the far end and the same square becomes a
         * move that takes the whole row. */
        b.cell[0][7] = (unsigned char)REV_BLACK;
        CHECK_EQI(rev_would_flip(&b, 0, 0, REV_BLACK), 6);
    }

    /* ---- the eight directions are independent ---------------------------- */
    /*
     * A black stone is placed at the centre of a white cross whose arms are
     * closed by black in FOUR directions and open in the other four. Exactly
     * the closed arms may flip. A direction table with a transposed sign
     * flips the wrong arms and leaves a board that is still symmetrical
     * enough to look deliberate.
     */
    clear(&b);
    {
        int i;
        b.turn = REV_BLACK;
        /* Closed arms: right, down, and the two diagonals going down-right
         * and up-right. Each is white, white, black. */
        b.cell[4][5] = (unsigned char)REV_WHITE;
        b.cell[4][6] = (unsigned char)REV_BLACK;
        b.cell[5][4] = (unsigned char)REV_WHITE;
        b.cell[6][4] = (unsigned char)REV_BLACK;
        b.cell[5][5] = (unsigned char)REV_WHITE;
        b.cell[6][6] = (unsigned char)REV_BLACK;
        b.cell[3][5] = (unsigned char)REV_WHITE;
        b.cell[2][6] = (unsigned char)REV_BLACK;
        /* Open arms: left and up are white but never closed. */
        b.cell[4][3] = (unsigned char)REV_WHITE;
        b.cell[4][2] = (unsigned char)REV_WHITE;
        b.cell[3][4] = (unsigned char)REV_WHITE;
        b.cell[2][4] = (unsigned char)REV_WHITE;
        CHECK_EQI(rev_would_flip(&b, 4, 4, REV_BLACK), 4);
        CHECK_EQI(rev_play(&b, 4, 4), 4);
        /* the four closed arms turned... */
        CHECK_EQI((int)b.cell[4][5], REV_BLACK);
        CHECK_EQI((int)b.cell[5][4], REV_BLACK);
        CHECK_EQI((int)b.cell[5][5], REV_BLACK);
        CHECK_EQI((int)b.cell[3][5], REV_BLACK);
        /* ...and the open ones did not. */
        CHECK_EQI((int)b.cell[4][3], REV_WHITE);
        CHECK_EQI((int)b.cell[4][2], REV_WHITE);
        CHECK_EQI((int)b.cell[3][4], REV_WHITE);
        CHECK_EQI((int)b.cell[2][4], REV_WHITE);
        for (i = 0; i < 1; i++) { (void)i; }
    }

    /* ---- passing, and an end that is not a full board -------------------- */
    /*
     * The rule implementations get wrong: a position where neither side can
     * move is OVER, with empty squares still on it. Waiting for sixty-four
     * stones leaves the game sitting there, and the player -- who can see
     * that nobody has a move -- assumes the program has hung. It has.
     */
    clear(&b);
    b.cell[0][0] = (unsigned char)REV_BLACK;   /* two lone stones, far apart */
    b.cell[7][7] = (unsigned char)REV_WHITE;
    b.turn = REV_BLACK;
    CHECK_EQI(rev_move_count(&b, REV_BLACK), 0);
    CHECK_EQI(rev_move_count(&b, REV_WHITE), 0);
    CHECK(rev_pass_needed(&b));
    CHECK(rev_game_over(&b));                  /* 62 squares empty, still over */
    {
        int bl = 0, wh = 0;
        rev_score(&b, &bl, &wh);
        CHECK_EQI(bl + wh, 2);                 /* and the board really is empty */
    }

    /* One side able to move is NOT game over, however lopsided. */
    rev_init(&b);
    CHECK(rev_game_over(&b) == CFALSE);

    /*
     * A pass: after Black plays, if White has no reply the turn must come
     * BACK to Black rather than being handed over to a player who cannot use
     * it. A turn handed to a player with no move is how a game deadlocks.
     */
    clear(&b);
    b.cell[3][3] = (unsigned char)REV_WHITE;
    b.cell[3][4] = (unsigned char)REV_BLACK;
    b.turn = REV_BLACK;
    CHECK(rev_play(&b, 3, 2) > 0);
    /* White now has one stone... none, in fact: it was flipped. With no white
     * stone on the board White can never move, so the turn stays with Black. */
    {
        int bl = 0, wh = 0;
        rev_score(&b, &bl, &wh);
        CHECK_EQI(wh, 0);
        CHECK_EQI(rev_move_count(&b, REV_WHITE), 0);
        CHECK_EQI(b.turn, REV_BLACK);
    }

    /* ---- rev_other ------------------------------------------------------ */
    CHECK_EQI(rev_other(REV_BLACK), REV_WHITE);
    CHECK_EQI(rev_other(REV_WHITE), REV_BLACK);
    /* An unknown colour answers EMPTY rather than guessing a side. */
    CHECK_EQI(rev_other(REV_EMPTY), REV_EMPTY);
    CHECK_EQI(rev_other(99), REV_EMPTY);

    /* ---- the computer player -------------------------------------------- */
    /*
     * Two properties, both of which a greedy player fails.
     */
    rev_init(&b);
    {
        int r = -1, c = -1;
        CHECK(rev_ai_move(&b, REV_BLACK, &r, &c));
        CHECK(rev_legal(&b, r, c, REV_BLACK));     /* never an illegal move */
    }
    /*
     * Offered a corner and a bigger flip elsewhere, it takes the corner. A
     * corner can never be flipped back, so trading it for stones now is the
     * losing move a most-stones-now player makes every time.
     */
    clear(&b);
    {
        int r = -1, c = -1, i;
        b.turn = REV_BLACK;
        /* Corner (0,0): one white at (0,1), black at (0,2) -- flips 1. */
        b.cell[0][1] = (unsigned char)REV_WHITE;
        b.cell[0][2] = (unsigned char)REV_BLACK;
        /* Middle (4,1): a run of five whites closed by black -- flips 5. */
        for (i = 2; i <= 6; i++) { b.cell[4][i] = (unsigned char)REV_WHITE; }
        b.cell[4][7] = (unsigned char)REV_BLACK;
        CHECK_EQI(rev_would_flip(&b, 0, 0, REV_BLACK), 1);
        CHECK_EQI(rev_would_flip(&b, 4, 1, REV_BLACK), 5);
        CHECK(rev_ai_move(&b, REV_BLACK, &r, &c));
        CHECK_EQI(r, 0);
        CHECK_EQI(c, 0);
    }
    /* With no move at all it says so, rather than returning a square. */
    clear(&b);
    {
        int r = 5, c = 5;
        CHECK(rev_ai_move(&b, REV_BLACK, &r, &c) == CFALSE);
    }

    /* ---- NULL is survivable --------------------------------------------- */
    CHECK_EQI(rev_would_flip(NULL, 0, 0, REV_BLACK), 0);
    CHECK_EQI(rev_move_count(NULL, REV_BLACK), 0);
    CHECK_EQI(rev_play(NULL, 0, 0), 0);
    CHECK(rev_game_over(NULL));
    CHECK(rev_ai_move(NULL, REV_BLACK, NULL, NULL) == CFALSE);
    rev_init(NULL);
    rev_score(NULL, NULL, NULL);
}
