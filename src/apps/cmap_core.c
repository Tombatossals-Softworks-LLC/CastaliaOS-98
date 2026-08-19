/*
 * cmap_core.c - The Character Map's grid (see cmap_core.h).
 */
#include "cmap_core.h"

void cmap_grid_init(CmapGrid *g, int first, int last, int cols)
{
    if (g == NULL) { return; }
    if (cols < 1) { cols = 1; }
    if (last < first) { last = first; }
    g->first = first;
    g->last  = last;
    g->cols  = cols;
    /* Round up: a partial last row is still a row, and dropping it is how the
     * last few characters in a font become unreachable. */
    g->rows  = (last - first + cols) / cols;
    if (g->rows < 1) { g->rows = 1; }
}

int cmap_count(const CmapGrid *g)
{
    return (g != NULL) ? (g->last - g->first + 1) : 0;
}

int cmap_code_at(const CmapGrid *g, int row, int col)
{
    int code;
    if (g == NULL || row < 0 || col < 0 || row >= g->rows || col >= g->cols) {
        return -1;
    }
    code = g->first + row * g->cols + col;
    return (code <= g->last) ? code : -1;
}

cbool cmap_cell_of(const CmapGrid *g, int code, int *row, int *col)
{
    if (g == NULL || code < g->first || code > g->last) { return CFALSE; }
    if (row != NULL) { *row = (code - g->first) / g->cols; }
    if (col != NULL) { *col = (code - g->first) % g->cols; }
    return CTRUE;
}

int cmap_move(const CmapGrid *g, int code, int dcol, int drow)
{
    int to;
    if (g == NULL) { return code; }
    if (code < g->first || code > g->last) { return code; }
    to = code + dcol + drow * g->cols;
    /*
     * Refused, not clamped. A vertical move off the bottom of a PARTIAL last
     * row lands past 'last' and is rejected here, which is what keeps the
     * highlight on a cell that exists -- there is no cell under the tail of
     * the second-to-last row.
     */
    if (to < g->first || to > g->last) { return code; }
    return to;
}

int cmap_home(const CmapGrid *g) { return (g != NULL) ? g->first : 0; }
int cmap_end(const CmapGrid *g)  { return (g != NULL) ? g->last  : 0; }

int cmap_jump(const CmapGrid *g, int ch)
{
    if (g == NULL || ch < g->first || ch > g->last) { return -1; }
    return ch;
}

int cmap_hit(const CmapGrid *g, int x, int y, int cell_w, int cell_h)
{
    int row, col;
    if (g == NULL || cell_w < 1 || cell_h < 1) { return -1; }
    if (x < 0 || y < 0) { return -1; }
    col = x / cell_w;
    row = y / cell_h;
    return cmap_code_at(g, row, col);
}
