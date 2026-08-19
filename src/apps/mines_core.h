/*
 * mines_core.h - Pure game logic for Mines (no gfx/wm/platform dependency).
 *
 * The board model behind app_mines.c, kept free of UI so the host unit tests
 * can exercise it hermetically (tests/test_mines.c). An original take on the
 * classic grid-of-hidden-mines puzzle: reveal every safe cell without
 * stepping on a mine; numbers count mines in the 8 neighbors.
 */
#ifndef CASTALIA_MINES_CORE_H
#define CASTALIA_MINES_CORE_H

#include "castalia/ctypes.h"

#define MINES_MAX_W     16
#define MINES_MAX_H     16
#define MINES_MAX_CELLS (MINES_MAX_W * MINES_MAX_H)

typedef enum {
    MINES_PLAYING = 0,
    MINES_WON,
    MINES_LOST
} MinesStatus;

typedef struct {
    int           w, h;        /* board size (clamped to the MAX above)     */
    int           mines;       /* mines on the board                        */
    unsigned      rng;         /* LCG state (seeded at init, never 0)       */
    cbool         placed;      /* mines are placed on the FIRST reveal, so
                                * the first click is never a mine           */
    MinesStatus   status;
    int           opened;      /* revealed safe cells                       */
    int           flags;       /* planted flags                             */
    int           burst;       /* cell index of the fatal mine, or -1       */
    unsigned char mine[MINES_MAX_CELLS];   /* 1 = mine                      */
    unsigned char adj[MINES_MAX_CELLS];    /* neighbor mine count 0..8      */
    unsigned char open[MINES_MAX_CELLS];   /* 1 = revealed                  */
    unsigned char flag[MINES_MAX_CELLS];   /* 1 = flagged                   */
} MinesGame;

/* Start a new game. w/h/mines are clamped to sane bounds (mines always
 * leaves at least one safe cell). The seed drives mine placement; the same
 * seed and first click reproduce the same board (host-testable). */
void mines_init(MinesGame *g, int w, int h, int mines, unsigned seed);

/* Reveal a cell. The first reveal places the mines (never under it). A zero
 * cell flood-reveals its region. Returns CFALSE when the reveal hit a mine
 * (game -> MINES_LOST); CTRUE otherwise. Ignores flagged/opened cells and
 * does nothing once the game is over. */
cbool mines_reveal(MinesGame *g, int x, int y);

/* Toggle a flag on a covered cell (no-op on revealed cells / finished game). */
void mines_toggle_flag(MinesGame *g, int x, int y);

/* Mines minus planted flags (the counter widget; may go negative). */
int mines_flags_left(const MinesGame *g);

/* Cell accessors (bounds-checked; out of range reads as 0/false). */
cbool mines_is_open(const MinesGame *g, int x, int y);
cbool mines_is_flag(const MinesGame *g, int x, int y);
cbool mines_is_mine(const MinesGame *g, int x, int y);
int   mines_adjacent(const MinesGame *g, int x, int y);

/* Authoring/test seam: install an explicit mine layout (array of w*h cells,
 * nonzero = mine) instead of random placement, recomputing the neighbor
 * counts. Marks the board as placed. */
void mines_set_layout(MinesGame *g, const unsigned char *mine_map);

#endif /* CASTALIA_MINES_CORE_H */
