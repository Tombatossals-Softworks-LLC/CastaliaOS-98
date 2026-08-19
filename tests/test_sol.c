/*
 * test_sol.c - The rules of Klondike (sol_core.c).
 *
 * Which card may go on which is the whole game, and it lived inside an
 * 1,100-line window file where nothing could reach it. A solitaire that
 * accepts an illegal move, or refuses a legal one, is broken in a way no
 * screenshot shows: the board looks identical either way, and the only
 * symptom is a player quietly finding the game unwinnable.
 */
#include "ctest.h"
#include "../src/apps/sol_core.h"

static Card C(int rank, int suit, cbool up)
{
    Card c; c.rank = rank; c.suit = suit; c.face_up = up; return c;
}

void test_sol(void)
{
    printf("- solitaire rules\n");

    /* ---- colour -------------------------------------------------------- */
    CHECK(sol_is_red(SUIT_DIAMOND));
    CHECK(sol_is_red(SUIT_HEART));
    CHECK(!sol_is_red(SUIT_CLUB));
    CHECK(!sol_is_red(SUIT_SPADE));

    /* ---- the tableau --------------------------------------------------- */
    {
        Card k_sp = C(13, SUIT_SPADE, CTRUE);
        Card q_he = C(12, SUIT_HEART, CTRUE);
        Card q_sp = C(12, SUIT_SPADE, CTRUE);
        Card j_cl = C(11, SUIT_CLUB, CTRUE);
        Card down = C(12, SUIT_HEART, CFALSE);

        /* An empty column takes a King and nothing else. That rule is what
         * stops a player parking any card in a gap and unwinding the game. */
        CHECK(sol_can_stack_tab_rule(NULL, &k_sp));
        CHECK(!sol_can_stack_tab_rule(NULL, &q_he));
        CHECK(!sol_can_stack_tab_rule(NULL, &j_cl));

        /* Next rank down, opposite colour. */
        CHECK(sol_can_stack_tab_rule(&k_sp, &q_he));      /* red on black   */
        CHECK(!sol_can_stack_tab_rule(&k_sp, &q_sp));     /* black on black */
        CHECK(sol_can_stack_tab_rule(&q_he, &j_cl));      /* black on red   */

        /* Same rank, or the wrong direction, or a gap. */
        CHECK(!sol_can_stack_tab_rule(&q_he, &q_sp));
        CHECK(!sol_can_stack_tab_rule(&j_cl, &q_he));
        CHECK(!sol_can_stack_tab_rule(&k_sp, &j_cl));

        /* A face-down card is the back of a card, not a landing place. */
        CHECK(!sol_can_stack_tab_rule(&down, &j_cl));

        /* An Ace goes nowhere in the tableau except onto a 2. */
        {
            Card a_he = C(1, SUIT_HEART, CTRUE);
            Card two_sp = C(2, SUIT_SPADE, CTRUE);
            CHECK(sol_can_stack_tab_rule(&two_sp, &a_he));
            CHECK(!sol_can_stack_tab_rule(NULL, &a_he));
        }
        CHECK(!sol_can_stack_tab_rule(&k_sp, NULL));
    }

    /* ---- the foundations ------------------------------------------------ */
    {
        Card a_he = C(1, SUIT_HEART, CTRUE);
        Card a_sp = C(1, SUIT_SPADE, CTRUE);
        Card two_he = C(2, SUIT_HEART, CTRUE);
        Card two_sp = C(2, SUIT_SPADE, CTRUE);
        Card k_he = C(13, SUIT_HEART, CTRUE);

        /* Empty takes an Ace, and only an Ace. */
        CHECK(sol_can_stack_found_rule(NULL, &a_he));
        CHECK(sol_can_stack_found_rule(NULL, &a_sp));
        CHECK(!sol_can_stack_found_rule(NULL, &two_he));
        CHECK(!sol_can_stack_found_rule(NULL, &k_he));

        /* Up by one, same suit -- the opposite of the tableau on both
         * counts, which is exactly the pair a copy-paste gets wrong. */
        CHECK(sol_can_stack_found_rule(&a_he, &two_he));
        CHECK(!sol_can_stack_found_rule(&a_he, &two_sp));   /* wrong suit      */
        CHECK(!sol_can_stack_found_rule(&a_he, &a_sp));     /* same rank       */
        CHECK(!sol_can_stack_found_rule(&two_he, &a_he));   /* wrong direction */

        /* The last card home. */
        {
            Card q_he = C(12, SUIT_HEART, CTRUE);
            CHECK(sol_can_stack_found_rule(&q_he, &k_he));
        }
        CHECK(!sol_can_stack_found_rule(&a_he, NULL));
    }

    /* ---- moving a run --------------------------------------------------- */
    {
        Card run[4];
        /* A single face-up card is a run of one. */
        run[0] = C(7, SUIT_HEART, CTRUE);
        CHECK(sol_run_valid_rule(run, 1));
        /* Descending, alternating. */
        run[0] = C(7, SUIT_HEART, CTRUE);
        run[1] = C(6, SUIT_SPADE, CTRUE);
        run[2] = C(5, SUIT_DIAMOND, CTRUE);
        CHECK(sol_run_valid_rule(run, 3));
        /* Two of the same colour in a row breaks it. */
        run[2] = C(5, SUIT_CLUB, CTRUE);
        CHECK(!sol_run_valid_rule(run, 3));
        /* A rank out of order breaks it. */
        run[1] = C(4, SUIT_SPADE, CTRUE);
        run[2] = C(3, SUIT_DIAMOND, CTRUE);
        CHECK(!sol_run_valid_rule(run, 3));
        /* A face-down card anywhere in the run breaks it -- you cannot carry
         * a card you have not turned over. */
        run[0] = C(7, SUIT_HEART, CTRUE);
        run[1] = C(6, SUIT_SPADE, CFALSE);
        run[2] = C(5, SUIT_DIAMOND, CTRUE);
        CHECK(!sol_run_valid_rule(run, 3));
        /* ...including the first one. */
        run[0] = C(7, SUIT_HEART, CFALSE);
        run[1] = C(6, SUIT_SPADE, CTRUE);
        CHECK(!sol_run_valid_rule(run, 2));
        /* Degenerate lengths. */
        CHECK(!sol_run_valid_rule(run, 0));
        CHECK(!sol_run_valid_rule(run, -1));
        CHECK(!sol_run_valid_rule(NULL, 3));
    }

    /* ---- the deal -------------------------------------------------------
     *
     * The property a shuffle is supposed to have is that every card is still
     * there exactly once. A shuffle that drops one, or deals one twice,
     * makes a game that cannot be won and looks completely normal until the
     * very end -- so it is checked over many deals, not one.
     */
    {
        cu32 seed = 12345u;
        int deal;
        int bad_counts = 0, bad_ranks = 0, identical = 0;
        Card prev[SOL_DECK];
        for (deal = 0; deal < 200; deal++) {
            Card deck[SOL_DECK];
            int seen[4][14];
            int i, s, r, same;
            for (s = 0; s < 4; s++) {
                for (r = 0; r < 14; r++) { seen[s][r] = 0; }
            }
            sol_deal(deck, &seed);
            for (i = 0; i < SOL_DECK; i++) {
                if (deck[i].rank < 1 || deck[i].rank > 13 ||
                    deck[i].suit < 0 || deck[i].suit > 3) { bad_ranks++; continue; }
                seen[deck[i].suit][deck[i].rank]++;
                if (deck[i].face_up) { bad_ranks++; }
            }
            for (s = 0; s < 4; s++) {
                for (r = 1; r <= 13; r++) {
                    if (seen[s][r] != 1) { bad_counts++; }
                }
            }
            /* Consecutive deals must differ -- a shuffle that returns the
             * same order every time passes every check above. */
            if (deal > 0) {
                same = 1;
                for (i = 0; i < SOL_DECK; i++) {
                    if (deck[i].rank != prev[i].rank ||
                        deck[i].suit != prev[i].suit) { same = 0; break; }
                }
                if (same) { identical++; }
            }
            for (i = 0; i < SOL_DECK; i++) { prev[i] = deck[i]; }
        }
        CHECK_EQI(bad_counts, 0);
        CHECK_EQI(bad_ranks, 0);
        CHECK_EQI(identical, 0);
    }

    /* The same seed deals the same game -- which is what makes a bug in one
     * reproducible at all. */
    {
        cu32 s1 = 999u, s2 = 999u;
        Card d1[SOL_DECK], d2[SOL_DECK];
        int i, diff = 0;
        sol_deal(d1, &s1);
        sol_deal(d2, &s2);
        for (i = 0; i < SOL_DECK; i++) {
            if (d1[i].rank != d2[i].rank || d1[i].suit != d2[i].suit) { diff++; }
        }
        CHECK_EQI(diff, 0);
    }
    sol_deal(NULL, NULL);          /* no crash */

    /* ---- winning --------------------------------------------------------- */
    {
        int f[4];
        f[0] = 13; f[1] = 13; f[2] = 13; f[3] = 13;
        CHECK(sol_is_won(f, 4));
        f[3] = 12;
        CHECK(!sol_is_won(f, 4));   /* one card short is not a win */
        f[0] = 0; f[1] = 0; f[2] = 0; f[3] = 0;
        CHECK(!sol_is_won(f, 4));
        CHECK(!sol_is_won(NULL, 4));
        CHECK(!sol_is_won(f, 0));
    }
}
