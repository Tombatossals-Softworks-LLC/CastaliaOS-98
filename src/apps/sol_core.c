/*
 * sol_core.c - The rules of Klondike, without the table (see sol_core.h).
 */
#include "sol_core.h"

cbool sol_is_red(int suit)
{
    return (suit == SUIT_DIAMOND || suit == SUIT_HEART) ? CTRUE : CFALSE;
}

cbool sol_can_stack_tab_rule(const Card *top, const Card *c)
{
    if (c == NULL) { return CFALSE; }
    /* An empty column takes a King and nothing else -- the rule that stops a
     * player parking any card in a gap and unwinding the whole game. */
    if (top == NULL) { return (c->rank == 13) ? CTRUE : CFALSE; }
    /* A face-down card is not a landing place; it is the back of a card. */
    if (!top->face_up) { return CFALSE; }
    if (top->rank != c->rank + 1) { return CFALSE; }
    return (sol_is_red(top->suit) != sol_is_red(c->suit)) ? CTRUE : CFALSE;
}

cbool sol_can_stack_found_rule(const Card *top, const Card *c)
{
    if (c == NULL) { return CFALSE; }
    if (top == NULL) { return (c->rank == 1) ? CTRUE : CFALSE; }
    if (c->suit != top->suit) { return CFALSE; }
    return (c->rank == top->rank + 1) ? CTRUE : CFALSE;
}

cbool sol_run_valid_rule(const Card *run, int n)
{
    int k;
    if (run == NULL || n < 1) { return CFALSE; }
    for (k = 0; k < n; k++) {
        if (!run[k].face_up) { return CFALSE; }
    }
    for (k = 0; k < n - 1; k++) {
        if (run[k + 1].rank != run[k].rank - 1) { return CFALSE; }
        if (sol_is_red(run[k].suit) == sol_is_red(run[k + 1].suit)) {
            return CFALSE;
        }
    }
    return CTRUE;
}

int sol_rand(cu32 *seed)
{
    if (seed == NULL) { return 0; }
    *seed = *seed * 1103515245u + 12345u;
    return (int)((*seed >> 16) & 0x7FFF);
}

void sol_deal(Card *deck, cu32 *seed)
{
    int i;
    if (deck == NULL || seed == NULL) { return; }
    for (i = 0; i < SOL_DECK; i++) {
        deck[i].suit = i % 4;
        deck[i].rank = i / 4 + 1;
        deck[i].face_up = CFALSE;
    }
    /* Fisher-Yates, downwards. The property that matters is that every card
     * appears exactly once afterwards -- a shuffle that loses one, or deals
     * one twice, produces a game that cannot be won and looks entirely
     * normal until the very end. */
    for (i = SOL_DECK - 1; i > 0; i--) {
        int j = sol_rand(seed) % (i + 1);
        Card tmp = deck[i];
        deck[i] = deck[j];
        deck[j] = tmp;
    }
}

cbool sol_is_won(const int *found_count, int piles)
{
    int f, total = 0;
    if (found_count == NULL || piles < 1) { return CFALSE; }
    for (f = 0; f < piles; f++) { total += found_count[f]; }
    return (total >= SOL_DECK) ? CTRUE : CFALSE;
}
