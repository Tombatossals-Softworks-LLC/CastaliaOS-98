/*
 * cmap_core.h - The Character Map's grid, without the window.
 *
 * A character map is a grid and nothing else: a range of codes laid out in
 * rows, a highlight that moves over it, and the reverse question of which
 * code a click landed on. All of it is arithmetic, and every one of its bugs
 * is invisible -- the grid still draws, the highlight still moves, it just
 * lands one cell off, or on a cell that is not there, or refuses to reach the
 * last character in the font. Nobody notices until they go looking for a
 * character they cannot select.
 *
 * The range is a PARAMETER here, never a constant. That is the point of the
 * file. gfx_font_range() exists precisely so the map follows the font, and
 * its own comment warned what would happen otherwise:
 *
 *     "the day a face arrives with more in it is the day a hard-coded 126
 *      somewhere else becomes a bug nobody is looking for"
 *
 * That hard-coded 126 was one file away, in the Character Map's key handler,
 * where a typed character jumped the highlight to `ch` if `ch <= 126` no
 * matter what the font actually held. It agrees with the font today by
 * coincidence -- both are 32..126 -- so nothing is visibly wrong and no test
 * that used the real font could ever have caught it. tests/test_cmap.c drives
 * ranges that are NOT 32..126, which is the only way that class of bug shows.
 */
#ifndef CASTALIA_CMAP_CORE_H
#define CASTALIA_CMAP_CORE_H

#include "castalia/ctypes.h"

typedef struct {
    int first;   /* lowest code in the map, inclusive                      */
    int last;    /* highest code in the map, inclusive                     */
    int cols;    /* cells per row                                          */
    int rows;    /* rows needed to hold them all, rounded up               */
} CmapGrid;

/*
 * Set up a grid over codes 'first'..'last' laid out 'cols' wide. A reversed
 * or empty range collapses to the single code 'first', and a non-positive
 * 'cols' becomes 1, because a grid with no columns has no cells and every
 * caller below would then divide by zero.
 */
void cmap_grid_init(CmapGrid *g, int first, int last, int cols);

/* How many codes the grid holds. */
int cmap_count(const CmapGrid *g);

/* The code in a cell, or -1 for a cell past the end of a partial last row. */
int cmap_code_at(const CmapGrid *g, int row, int col);

/* Where a code sits. CFALSE (and untouched outputs) if it is not in range. */
cbool cmap_cell_of(const CmapGrid *g, int code, int *row, int *col);

/*
 * Move the highlight by (dcol, drow) and return where it lands. Movement is
 * REFUSED rather than clamped when it would leave the map -- the code comes
 * back unchanged -- so holding an arrow at the edge does nothing instead of
 * quietly jumping somewhere else.
 */
int cmap_move(const CmapGrid *g, int code, int dcol, int drow);

/* The first and last codes, for Home and End. */
int cmap_home(const CmapGrid *g);
int cmap_end(const CmapGrid *g);

/*
 * Where a typed character sends the highlight: 'ch' itself when the font
 * holds it, and -1 when it does not. Returning -1 rather than 'ch' is the fix
 * for the bug this file's header describes -- a character the font cannot
 * draw must not become the selection.
 */
int cmap_jump(const CmapGrid *g, int ch);

/*
 * The code under a point measured from the grid's top-left interior pixel,
 * given cell dimensions; -1 for a miss, including the empty cells of a
 * partial last row.
 */
int cmap_hit(const CmapGrid *g, int x, int y, int cell_w, int cell_h);

#endif /* CASTALIA_CMAP_CORE_H */
