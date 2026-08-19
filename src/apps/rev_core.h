/*
 * rev_core.h - The rules of Reversi (Othello).
 *
 * A pure board model with no drawing in it, so tests/test_rev.c can reach the
 * parts that are easy to get wrong and impossible to see: this is a game in
 * which an illegal move looks exactly like a legal one, and a mis-flipped run
 * leaves a board that is still a perfectly plausible board.
 *
 * Three rules carry almost all the risk, and each has its own test:
 *
 *   1. A move must FLIP something. Dropping a stone on an empty square next
 *      to an enemy is not a move unless the run of enemies is closed by one
 *      of your own; a run that runs off the edge closes nothing.
 *   2. Flipping runs in all EIGHT directions from the placed stone, and each
 *      direction is independent -- one that closes flips, one that does not
 *      leaves its stones alone.
 *   3. PASSING. A player with no legal move passes, and the turn goes back.
 *      If NEITHER player can move the game ends -- even with empty squares
 *      left on the board. Implementations that end the game only when the
 *      board is full will hang on a position that is over, which is the
 *      classic Reversi bug and the reason rev_game_over exists separately
 *      from "sixty-four stones are down".
 */
#ifndef CASTALIA_REV_CORE_H
#define CASTALIA_REV_CORE_H

#include "castalia/ctypes.h"

#define REV_N 8                     /* the board is 8x8, always            */
#define REV_CELLS (REV_N * REV_N)

enum { REV_EMPTY = 0, REV_BLACK, REV_WHITE };

typedef struct {
    unsigned char cell[REV_N][REV_N];
    int turn;                       /* REV_BLACK or REV_WHITE              */
    int passes;                     /* consecutive passes; 2 ends the game */
} RevBoard;

/* The opening: four stones crossed in the middle, Black to move. */
void rev_init(RevBoard *b);

/* The opposing colour. REV_EMPTY answers REV_EMPTY -- a caller that has lost
 * track of whose turn it is gets a harmless answer rather than a plausible
 * wrong one. */
int rev_other(int who);

/*
 * How many stones a move at (r,c) would turn over for 'who'. Zero means the
 * move is illegal, which is the same answer for "off the board", "square
 * occupied" and "closes nothing" -- the three are indistinguishable to a
 * player and should be indistinguishable here.
 */
int rev_would_flip(const RevBoard *b, int r, int c, int who);

cbool rev_legal(const RevBoard *b, int r, int c, int who);

/*
 * Play for the side to move. Returns the number of stones flipped, or 0 if
 * the move was illegal -- in which case the board is UNTOUCHED, including
 * whose turn it is. A rejected move that quietly advanced the turn would hand
 * the game to the other player for a mis-click.
 *
 * On success the turn passes to the opponent, or stays put if the opponent
 * has no reply (see rev_pass_needed).
 */
int rev_play(RevBoard *b, int r, int c);

/* Does the side to move have to pass? */
cbool rev_pass_needed(const RevBoard *b);

/* How many legal moves 'who' has. */
int rev_move_count(const RevBoard *b, int who);

/* Stones on the board, by colour. */
void rev_score(const RevBoard *b, int *black, int *white);

/* Neither side can move (or the board is full). */
cbool rev_game_over(const RevBoard *b);

/*
 * Choose a move for 'who'. Returns CTRUE and fills *out_r / *out_c, or CFALSE
 * when there is no legal move.
 *
 * Positional, not greedy: the most-stones-now move is a famously bad Reversi
 * strategy, because stones flip back. The weights price corners highly (they
 * can never be flipped), penalise the squares diagonally inside them (playing
 * there hands the corner over), and count the flip total only as a tie-break.
 */
cbool rev_ai_move(const RevBoard *b, int who, int *out_r, int *out_c);

#endif /* CASTALIA_REV_CORE_H */
