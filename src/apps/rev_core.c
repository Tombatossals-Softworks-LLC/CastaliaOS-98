/*
 * rev_core.c - The rules of Reversi (see rev_core.h).
 */
#include "rev_core.h"

/* The eight directions, as (dr,dc). Kept in one table because eight hand-
 * written direction cases is eight chances to transpose a sign, and a single
 * wrong direction produces a board that still looks like a game. */
static const int REV_DR[8] = { -1, -1, -1,  0, 0,  1, 1, 1 };
static const int REV_DC[8] = { -1,  0,  1, -1, 1, -1, 0, 1 };

void rev_init(RevBoard *b)
{
    int r, c;
    if (b == NULL) { return; }
    for (r = 0; r < REV_N; r++) {
        for (c = 0; c < REV_N; c++) { b->cell[r][c] = (unsigned char)REV_EMPTY; }
    }
    /* White on the top-left of the centre pair, which is the standard opening
     * and the reason Black's four opening moves are all equivalent. */
    b->cell[3][3] = (unsigned char)REV_WHITE;
    b->cell[4][4] = (unsigned char)REV_WHITE;
    b->cell[3][4] = (unsigned char)REV_BLACK;
    b->cell[4][3] = (unsigned char)REV_BLACK;
    b->turn = REV_BLACK;
    b->passes = 0;
}

int rev_other(int who)
{
    if (who == REV_BLACK) { return REV_WHITE; }
    if (who == REV_WHITE) { return REV_BLACK; }
    return REV_EMPTY;
}

static cbool rev_on(int r, int c)
{
    return (r >= 0 && r < REV_N && c >= 0 && c < REV_N) ? CTRUE : CFALSE;
}

/* Stones that would turn in ONE direction. Zero unless the run of enemy
 * stones is non-empty and closed by one of 'who's own. */
static int rev_run(const RevBoard *b, int r, int c, int dr, int dc, int who)
{
    int them = rev_other(who);
    int n = 0;
    int rr = r + dr, cc = c + dc;
    while (rev_on(rr, cc) && (int)b->cell[rr][cc] == them) {
        n++;
        rr += dr; cc += dc;
    }
    /* Ran off the board, or onto an empty square: nothing is closed, so
     * nothing flips. This is the case that makes an edge run harmless. */
    if (n == 0 || !rev_on(rr, cc) || (int)b->cell[rr][cc] != who) { return 0; }
    return n;
}

int rev_would_flip(const RevBoard *b, int r, int c, int who)
{
    int d, total = 0;
    if (b == NULL || !rev_on(r, c)) { return 0; }
    if (who != REV_BLACK && who != REV_WHITE) { return 0; }
    if ((int)b->cell[r][c] != REV_EMPTY) { return 0; }
    for (d = 0; d < 8; d++) {
        total += rev_run(b, r, c, REV_DR[d], REV_DC[d], who);
    }
    return total;
}

cbool rev_legal(const RevBoard *b, int r, int c, int who)
{
    return (rev_would_flip(b, r, c, who) > 0) ? CTRUE : CFALSE;
}

int rev_move_count(const RevBoard *b, int who)
{
    int r, c, n = 0;
    if (b == NULL) { return 0; }
    for (r = 0; r < REV_N; r++) {
        for (c = 0; c < REV_N; c++) {
            if (rev_legal(b, r, c, who)) { n++; }
        }
    }
    return n;
}

int rev_play(RevBoard *b, int r, int c)
{
    int d, flipped = 0, who, them;
    if (b == NULL) { return 0; }
    who = b->turn;
    if (rev_would_flip(b, r, c, who) <= 0) { return 0; }
    them = rev_other(who);
    b->cell[r][c] = (unsigned char)who;
    for (d = 0; d < 8; d++) {
        int n = rev_run(b, r, c, REV_DR[d], REV_DC[d], who);
        int i, rr = r + REV_DR[d], cc = c + REV_DC[d];
        for (i = 0; i < n; i++) {
            b->cell[rr][cc] = (unsigned char)who;
            rr += REV_DR[d]; cc += REV_DC[d];
        }
        flipped += n;
    }
    (void)them;
    /*
     * The turn goes to the opponent -- unless they have nothing to play, in
     * which case it comes straight back and the pass is recorded. Two passes
     * in a row is the end of the game, which is NOT the same as a full board:
     * a position where neither side can move with empty squares left is over,
     * and a program that waits for sixty-four stones will sit there forever.
     */
    if (rev_move_count(b, rev_other(who)) > 0) {
        b->turn = rev_other(who);
        b->passes = 0;
    } else if (rev_move_count(b, who) > 0) {
        b->passes++;            /* opponent passes; same player moves again */
    } else {
        b->passes = 2;          /* nobody can move: over */
    }
    return flipped;
}

cbool rev_pass_needed(const RevBoard *b)
{
    if (b == NULL) { return CFALSE; }
    return (rev_move_count(b, b->turn) == 0) ? CTRUE : CFALSE;
}

void rev_score(const RevBoard *b, int *black, int *white)
{
    int r, c, bl = 0, wh = 0;
    if (b != NULL) {
        for (r = 0; r < REV_N; r++) {
            for (c = 0; c < REV_N; c++) {
                if ((int)b->cell[r][c] == REV_BLACK) { bl++; }
                else if ((int)b->cell[r][c] == REV_WHITE) { wh++; }
            }
        }
    }
    if (black) { *black = bl; }
    if (white) { *white = wh; }
}

cbool rev_game_over(const RevBoard *b)
{
    if (b == NULL) { return CTRUE; }
    if (b->passes >= 2) { return CTRUE; }
    /* Asked directly rather than trusting the counter, so a board built by
     * hand (a test, a loaded position) answers correctly too. */
    if (rev_move_count(b, REV_BLACK) == 0 &&
        rev_move_count(b, REV_WHITE) == 0) {
        return CTRUE;
    }
    return CFALSE;
}

/*
 * Positional weights. Corners are worth having because nothing can ever flip
 * them; the diagonal neighbours of a corner are worth avoiding because
 * playing one is usually how the opponent gets the corner.
 */
static const int REV_WEIGHT[REV_N][REV_N] = {
    { 100, -20,  10,   5,   5,  10, -20, 100 },
    { -20, -40,  -4,  -3,  -3,  -4, -40, -20 },
    {  10,  -4,   6,   2,   2,   6,  -4,  10 },
    {   5,  -3,   2,   1,   1,   2,  -3,   5 },
    {   5,  -3,   2,   1,   1,   2,  -3,   5 },
    {  10,  -4,   6,   2,   2,   6,  -4,  10 },
    { -20, -40,  -4,  -3,  -3,  -4, -40, -20 },
    { 100, -20,  10,   5,   5,  10, -20, 100 }
};

cbool rev_ai_move(const RevBoard *b, int who, int *out_r, int *out_c)
{
    int r, c, best_r = -1, best_c = -1;
    long best = 0;
    if (b == NULL) { return CFALSE; }
    for (r = 0; r < REV_N; r++) {
        for (c = 0; c < REV_N; c++) {
            int f = rev_would_flip(b, r, c, who);
            long v;
            if (f <= 0) { continue; }
            /* Position dominates; the flip count only separates squares of
             * equal standing. Scaled so a corner always outranks a large
             * flip in the middle -- taking twelve stones next to a corner
             * and handing the corner over is the losing move that a purely
             * greedy player makes every time. */
            v = (long)REV_WEIGHT[r][c] * 10L + (long)f;
            if (best_r < 0 || v > best) {
                best = v; best_r = r; best_c = c;
            }
        }
    }
    if (best_r < 0) { return CFALSE; }
    if (out_r) { *out_r = best_r; }
    if (out_c) { *out_c = best_c; }
    return CTRUE;
}
