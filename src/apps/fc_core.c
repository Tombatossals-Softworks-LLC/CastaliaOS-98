/*
 * fc_core.c - The rules of FreeCell (see fc_core.h).
 */
#include "fc_core.h"

cbool fc_can_stack_cascade(const Card *top, const Card *c)
{
    if (c == NULL) { return CFALSE; }
    /* Any card at all on an empty cascade. Klondike answers "a King only" to
     * the same question, and that one difference is most of the game. */
    if (top == NULL) { return CTRUE; }
    if (top->rank != c->rank + 1) { return CFALSE; }
    return (sol_is_red(top->suit) != sol_is_red(c->suit)) ? CTRUE : CFALSE;
}

int fc_max_move(int free_cells, int empty_cols, cbool into_empty)
{
    long n;
    int i;
    if (free_cells < 0) { free_cells = 0; }
    if (empty_cols  < 0) { empty_cols  = 0; }
    if (free_cells > FC_CELLS)    { free_cells = FC_CELLS; }
    if (empty_cols > FC_CASCADES) { empty_cols = FC_CASCADES; }
    /*
     * A cascade you are moving INTO is not scratch space -- you cannot park
     * cards in the hole you are trying to fill. Implementations that forget
     * this offer moves the player cannot actually make.
     */
    if (into_empty && empty_cols > 0) { empty_cols--; }

    /* (free + 1) * 2^empty, computed by doubling so there is no pow() and no
     * floating point anywhere near a rule. */
    n = (long)free_cells + 1L;
    for (i = 0; i < empty_cols; i++) {
        n *= 2L;
        /* Nothing can move more cards than exist, and the doubling would
         * otherwise run away on a board with many empty columns. */
        if (n >= (long)SOL_DECK) { return SOL_DECK; }
    }
    return (int)n;
}

cbool fc_run_valid(const Card *run, int n)
{
    int i;
    if (run == NULL || n <= 0) { return CFALSE; }
    if (n == 1) { return CTRUE; }
    for (i = 0; i + 1 < n; i++) {
        if (run[i].rank != run[i + 1].rank + 1) { return CFALSE; }
        if (sol_is_red(run[i].suit) == sol_is_red(run[i + 1].suit)) {
            return CFALSE;
        }
    }
    return CTRUE;
}

void fc_deal(Card cascade[FC_CASCADES][FC_MAX_COL], int *col_count, cu32 *seed)
{
    Card deck[SOL_DECK];
    int i, col;
    if (cascade == NULL || col_count == NULL) { return; }
    sol_deal(deck, seed);
    for (i = 0; i < FC_CASCADES; i++) { col_count[i] = 0; }
    /*
     * Dealt round-robin, so the first four columns get seven cards and the
     * last four get six. Every card face up: FreeCell hides nothing, which is
     * why it is a puzzle rather than a gamble.
     */
    for (i = 0; i < SOL_DECK; i++) {
        col = i % FC_CASCADES;
        deck[i].face_up = CTRUE;
        cascade[col][col_count[col]] = deck[i];
        col_count[col]++;
    }
}

cbool fc_is_won(const int *found_count)
{
    int i, total = 0;
    if (found_count == NULL) { return CFALSE; }
    for (i = 0; i < FC_FOUND; i++) { total += found_count[i]; }
    return (total >= SOL_DECK) ? CTRUE : CFALSE;
}
