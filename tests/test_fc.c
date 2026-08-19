/*
 * test_fc.c - The rules of FreeCell (fc_core.c).
 *
 * Two of these rules are the sort that no screenshot can check, because a
 * board with a wrong rule looks exactly like a board with a right one:
 *
 *   - an empty cascade takes ANY card, where Klondike takes only a King.
 *     Getting that backwards makes the game unwinnable and looks like nothing
 *     at all.
 *   - how many cards may move at once. Too small and the player is refused a
 *     move they can see is legal; too large and the game accepts a move that
 *     cannot actually be performed by hand. Neither draws differently.
 *
 * So both are driven here over every combination that exists, and the
 * every-card-exactly-once property of the deal is checked the way it was for
 * Klondike -- because a bad shuffle is the other thing that looks fine.
 */
#include "ctest.h"
#include "../src/apps/fc_core.h"

static Card mk(int rank, int suit)
{
    Card c;
    c.rank = rank; c.suit = suit; c.face_up = CTRUE;
    return c;
}

void test_fc(void)
{
    Card cascade[FC_CASCADES][FC_MAX_COL];
    int  col_count[FC_CASCADES];
    cu32 seed = 20260818u;
    int  i, j, k;

    printf("- freecell rules\n");

    /* ---- an empty cascade takes anything ------------------------------- *
     *
     * This is THE difference from Klondike, and the one a shared card model
     * makes easy to get wrong by reaching for sol_can_stack_tab_rule.
     */
    {
        Card king = mk(13, SUIT_SPADE);
        Card two  = mk(2,  SUIT_HEART);
        Card ace  = mk(1,  SUIT_CLUB);
        CHECK(fc_can_stack_cascade(NULL, &king));
        CHECK(fc_can_stack_cascade(NULL, &two));    /* Klondike says no */
        CHECK(fc_can_stack_cascade(NULL, &ace));    /* ...and to this too */
        /* Klondike really does disagree, so this is a difference and not a
         * restatement. */
        CHECK(!sol_can_stack_tab_rule(NULL, &two));
        CHECK(sol_can_stack_tab_rule(NULL, &king));
    }

    /* ---- and otherwise: down one, opposite colour ----------------------- */
    {
        Card red7   = mk(7, SUIT_HEART);
        Card black6 = mk(6, SUIT_SPADE);
        Card red6   = mk(6, SUIT_DIAMOND);
        Card black5 = mk(5, SUIT_CLUB);
        CHECK(fc_can_stack_cascade(&red7, &black6));    /* 6 black on 7 red */
        CHECK(!fc_can_stack_cascade(&red7, &red6));     /* same colour      */
        CHECK(!fc_can_stack_cascade(&red7, &black5));   /* two below        */
        CHECK(!fc_can_stack_cascade(&black6, &red7));   /* upwards          */
        CHECK(!fc_can_stack_cascade(&red7, NULL));
    }

    /* ---- how many cards may move --------------------------------------- *
     *
     * (free + 1) * 2^empty, and a destination that is itself an empty cascade
     * costs one of those empties -- you cannot park cards in the hole you are
     * filling.
     */
    /* Nothing free: one card, which is all a player can ever lift by hand. */
    CHECK_EQI(fc_max_move(0, 0, CFALSE), 1);
    /* Cells alone are linear. */
    CHECK_EQI(fc_max_move(1, 0, CFALSE), 2);
    CHECK_EQI(fc_max_move(2, 0, CFALSE), 3);
    CHECK_EQI(fc_max_move(3, 0, CFALSE), 4);
    CHECK_EQI(fc_max_move(4, 0, CFALSE), 5);
    /* Empty columns double. */
    CHECK_EQI(fc_max_move(0, 1, CFALSE), 2);
    CHECK_EQI(fc_max_move(0, 2, CFALSE), 4);
    CHECK_EQI(fc_max_move(4, 1, CFALSE), 10);
    CHECK_EQI(fc_max_move(4, 2, CFALSE), 20);
    /* The opening position of a solved-out board: four cells, four empty
     * columns, moving onto a card. */
    CHECK_EQI(fc_max_move(4, 4, CFALSE), 80 > SOL_DECK ? SOL_DECK : 80);

    /* Moving INTO an empty column is strictly weaker than moving onto a card,
     * at every point where there is an empty column to lose. */
    for (i = 0; i <= FC_CELLS; i++) {
        for (j = 1; j <= FC_CASCADES; j++) {
            int onto_card  = fc_max_move(i, j, CFALSE);
            int into_empty = fc_max_move(i, j, CTRUE);
            CHECK(into_empty < onto_card || onto_card >= SOL_DECK);
            /* ...and it equals the same board with one fewer empty column. */
            CHECK_EQI(into_empty, fc_max_move(i, j - 1, CFALSE));
        }
    }
    /* With no empty columns at all, the destination cannot be an empty one,
     * and asking anyway must not go below one. */
    CHECK_EQI(fc_max_move(0, 0, CTRUE), 1);
    CHECK_EQI(fc_max_move(2, 0, CTRUE), 3);

    /* Never zero, never negative, never more than a deck -- for every input
     * including nonsense ones. */
    for (i = -3; i <= FC_CELLS + 3; i++) {
        for (j = -3; j <= FC_CASCADES + 3; j++) {
            int a = fc_max_move(i, j, CFALSE);
            int b = fc_max_move(i, j, CTRUE);
            CHECK(a >= 1 && a <= SOL_DECK);
            CHECK(b >= 1 && b <= SOL_DECK);
        }
    }

    /* ---- a movable run -------------------------------------------------- */
    {
        Card run[4];
        run[0] = mk(8, SUIT_SPADE);
        run[1] = mk(7, SUIT_HEART);
        run[2] = mk(6, SUIT_CLUB);
        run[3] = mk(5, SUIT_DIAMOND);
        CHECK(fc_run_valid(run, 4));
        CHECK(fc_run_valid(run, 1));
        CHECK(fc_run_valid(run + 2, 2));
        /* Break the colour alternation. */
        run[2] = mk(6, SUIT_DIAMOND);
        CHECK(!fc_run_valid(run, 4));
        CHECK(fc_run_valid(run, 2));        /* the head is still fine */
        /* Break the rank descent. */
        run[2] = mk(4, SUIT_CLUB);
        CHECK(!fc_run_valid(run, 3));
        CHECK(!fc_run_valid(NULL, 3));
        CHECK(!fc_run_valid(run, 0));
        CHECK(!fc_run_valid(run, -1));
    }

    /* ---- the deal -------------------------------------------------------- */
    fc_deal(cascade, col_count, &seed);
    {
        int seen[14][4];
        int total = 0;
        for (i = 0; i < 14; i++) {
            for (j = 0; j < 4; j++) { seen[i][j] = 0; }
        }
        /* Four columns of seven and four of six: 4*7 + 4*6 = 52. */
        for (i = 0; i < FC_CASCADES; i++) {
            CHECK(col_count[i] == 7 || col_count[i] == 6);
            total += col_count[i];
            for (j = 0; j < col_count[i]; j++) {
                Card *c = &cascade[i][j];
                CHECK(c->rank >= 1 && c->rank <= 13);
                CHECK(c->suit >= 0 && c->suit <= 3);
                /* Every card is face up. FreeCell hides nothing, and a deal
                 * that left one face down would be unplayable in a way the
                 * board does not show. */
                CHECK(c->face_up);
                seen[c->rank][c->suit]++;
            }
        }
        CHECK_EQI(total, SOL_DECK);
        /* Every card exactly once -- the property a shuffle is supposed to
         * have and the one a bad shuffle silently loses. */
        for (i = 1; i <= 13; i++) {
            for (j = 0; j < 4; j++) { CHECK_EQI(seen[i][j], 1); }
        }
        /* The first four columns are the long ones. */
        for (i = 0; i < 4; i++) { CHECK_EQI(col_count[i], 7); }
        for (i = 4; i < FC_CASCADES; i++) { CHECK_EQI(col_count[i], 6); }
    }

    /* A second deal from a moved seed is a different board -- otherwise
     * "New Game" deals the same hand forever. */
    {
        Card again[FC_CASCADES][FC_MAX_COL];
        int  again_count[FC_CASCADES];
        int  same = 1;
        fc_deal(again, again_count, &seed);
        for (i = 0; i < FC_CASCADES && same; i++) {
            for (j = 0; j < again_count[i]; j++) {
                if (again[i][j].rank != cascade[i][j].rank ||
                    again[i][j].suit != cascade[i][j].suit) {
                    same = 0; break;
                }
            }
        }
        CHECK(!same);
    }

    /* ---- winning --------------------------------------------------------- */
    {
        int found[FC_FOUND];
        for (k = 0; k < FC_FOUND; k++) { found[k] = 13; }
        CHECK(fc_is_won(found));
        found[0] = 12;
        CHECK(!fc_is_won(found));       /* one card short is not a win */
        for (k = 0; k < FC_FOUND; k++) { found[k] = 0; }
        CHECK(!fc_is_won(found));
        CHECK(!fc_is_won(NULL));
    }

    /* ---- refusals -------------------------------------------------------- */
    fc_deal(NULL, col_count, &seed);        /* no crash */
    fc_deal(cascade, NULL, &seed);
}
