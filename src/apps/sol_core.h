/*
 * sol_core.h - The rules of Klondike, without the table.
 *
 * Which card may go on which is the whole game, and it was decided inside a
 * 1,100-line window file where nothing could reach it. A solitaire that
 * accepts an illegal move, or refuses a legal one, is broken in a way no
 * screenshot shows: the board looks exactly the same either way, and the only
 * symptom is a player quietly finding the game unwinnable.
 *
 * So the predicates live here, taking cards rather than a window:
 *
 *   - a tableau accepts a King on empty, and otherwise the next rank DOWN in
 *     the opposite colour
 *   - a foundation accepts an Ace on empty, and otherwise the next rank UP in
 *     the SAME suit
 *   - a run of tableau cards moves as one only if every card is face up and
 *     they descend in strictly alternating colour
 *
 * The deal is here too, because "every card exactly once" is the property a
 * shuffle is supposed to have and the one a bad shuffle silently loses.
 */
#ifndef CASTALIA_SOL_CORE_H
#define CASTALIA_SOL_CORE_H

#include "castalia/ctypes.h"

/* Suits are ordered so that (suit & 1) is not the colour -- the colour test
 * is a named function, because writing it inline is how a red club appears. */
enum { SUIT_CLUB = 0, SUIT_DIAMOND = 1, SUIT_HEART = 2, SUIT_SPADE = 3 };

#define SOL_DECK 52

typedef struct {
    int   rank;      /* 1..13 (A=1, J=11, Q=12, K=13) */
    int   suit;      /* SUIT_*                        */
    cbool face_up;
} Card;

/* Diamonds and hearts. */
cbool sol_is_red(int suit);

/*
 * May 'c' be placed on a tableau column whose top card is 'top'?
 * Pass NULL for 'top' to ask about an empty column, which takes a King only.
 */
cbool sol_can_stack_tab_rule(const Card *top, const Card *c);

/*
 * May 'c' be placed on a foundation whose top card is 'top'?
 * Pass NULL for 'top' to ask about an empty foundation, which takes an Ace.
 */
cbool sol_can_stack_found_rule(const Card *top, const Card *c);

/*
 * Is 'run' (n cards, in board order from the one being picked up to the top
 * of the pile) a legal run to move as a unit? Every card face up, descending
 * by one, strictly alternating colour. A single face-up card is a valid run.
 */
cbool sol_run_valid_rule(const Card *run, int n);

/* Fill 'deck' with a full 52 and shuffle it, advancing 'seed'. */
void sol_deal(Card *deck, cu32 *seed);

/* The one step of the shuffle's generator, exposed so a test can predict it. */
int sol_rand(cu32 *seed);

/* Every card home: the foundations hold 52 between them. */
cbool sol_is_won(const int *found_count, int piles);

#endif /* CASTALIA_SOL_CORE_H */
