/*
 * fc_core.h - The rules of FreeCell, without the table.
 *
 * FreeCell is the other patience game the era shipped, and it is a better one
 * to have than a second Klondike: every card is face up from the deal, so
 * there is no luck left in it -- only whether you can see the move.
 *
 * The card, the colours and the foundation rule come from sol_core.h rather
 * than being written again here. What is genuinely different is worth stating,
 * because two of the three are easy to get subtly wrong:
 *
 *   - a cascade takes ANY card when empty, not a King. Klondike's
 *     King-on-empty rule is what makes that game hard; FreeCell's freedom is
 *     what makes it solvable.
 *   - a free cell holds exactly one card, of any kind.
 *   - and the one that is really a rule rather than a convention: how many
 *     cards you may move at once. A player can only ever move ONE card by
 *     hand; a "supermove" of several is shorthand for shuffling them through
 *     the free cells and empty cascades and back. So the limit is
 *
 *         (free cells + 1) * 2^(empty cascades)
 *
 *     ...with the caveat that an empty cascade you are moving INTO cannot
 *     also be used as scratch space, which is the part implementations get
 *     wrong. fc_max_move takes the destination for exactly that reason.
 *
 * That formula is invisible when wrong: the board looks identical, and the
 * only symptom is a legal move the game refuses, or an illegal one it allows
 * and which no amount of staring at the screen will explain. It is arithmetic
 * over two small integers, so tests/test_fc.c can drive every combination
 * there is.
 */
#ifndef CASTALIA_FC_CORE_H
#define CASTALIA_FC_CORE_H

#include "castalia/ctypes.h"
#include "sol_core.h"

#define FC_CASCADES 8
#define FC_CELLS    4
#define FC_FOUND    4
#define FC_MAX_COL  24        /* a cascade never grows past this in practice */

/*
 * May 'c' go on a cascade whose top card is 'top'? NULL 'top' means the
 * cascade is empty, and an empty cascade in FreeCell takes ANY card -- which
 * is the single rule that separates this game from Klondike, where the same
 * question answers "a King, or nothing".
 */
cbool fc_can_stack_cascade(const Card *top, const Card *c);

/*
 * How many cards may be moved as one unit, given the free cells and empty
 * cascades available.
 *
 * 'free_cells'   how many cells are empty (0..FC_CELLS)
 * 'empty_cols'   how many cascades are empty (0..FC_CASCADES)
 * 'into_empty'   CTRUE when the destination is itself an empty cascade
 *
 * The last argument is the whole reason this is a function and not a macro.
 * A cascade you are moving into is not available as working space, so moving
 * onto an empty column is strictly weaker than moving onto a card -- and an
 * implementation that forgets it will happily accept a move the player cannot
 * actually perform.
 */
int fc_max_move(int free_cells, int empty_cols, cbool into_empty);

/*
 * Is 'run' (n cards, from the one being picked up down to the top of the
 * pile) a legal sequence to move as a unit? Descending by one and strictly
 * alternating colour. Unlike Klondike there is no face-down card to worry
 * about: everything is face up in FreeCell.
 */
cbool fc_run_valid(const Card *run, int n);

/* Deal 52 cards into 8 cascades: the first four get seven cards, the last
 * four get six. Advances 'seed'. 'col_count' receives the height of each. */
void fc_deal(Card cascade[FC_CASCADES][FC_MAX_COL], int *col_count,
             cu32 *seed);

/* Every card home. */
cbool fc_is_won(const int *found_count);

#endif /* CASTALIA_FC_CORE_H */
