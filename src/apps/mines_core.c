/*
 * mines_core.c - Pure Mines game logic (see mines_core.h).
 *
 * Deliberately free of gfx/wm/platform includes so the unit tests link it
 * hermetically. Randomness is a self-contained LCG (the project convention:
 * deterministic, integer-only, C89-clean) seeded by the caller.
 */
#include "mines_core.h"

#include <string.h>

/* ---- RNG (LCG, top bits) ---------------------------------------------- */
static unsigned mines_rand(MinesGame *g)
{
    g->rng = g->rng * 1103515245u + 12345u;
    return (g->rng >> 16) & 0x7FFFu;
}

static int cell_index(const MinesGame *g, int x, int y)
{
    if (x < 0 || y < 0 || x >= g->w || y >= g->h) { return -1; }
    return y * g->w + x;
}

/* ---- setup ------------------------------------------------------------ */
void mines_init(MinesGame *g, int w, int h, int mines, unsigned seed)
{
    if (g == NULL) { return; }
    memset(g, 0, sizeof(*g));
    if (w < 2) { w = 2; }
    if (h < 2) { h = 2; }
    if (w > MINES_MAX_W) { w = MINES_MAX_W; }
    if (h > MINES_MAX_H) { h = MINES_MAX_H; }
    g->w = w;
    g->h = h;
    if (mines < 1) { mines = 1; }
    if (mines > w * h - 1) { mines = w * h - 1; }
    g->mines = mines;
    g->rng = (seed == 0u) ? 0xC0FFEEu : seed;
    g->status = MINES_PLAYING;
    g->burst = -1;
}

static void compute_adjacency(MinesGame *g)
{
    int x, y;
    for (y = 0; y < g->h; y++) {
        for (x = 0; x < g->w; x++) {
            int n = 0, dx, dy;
            for (dy = -1; dy <= 1; dy++) {
                for (dx = -1; dx <= 1; dx++) {
                    int i = cell_index(g, x + dx, y + dy);
                    if (i >= 0 && !(dx == 0 && dy == 0) && g->mine[i]) { n++; }
                }
            }
            g->adj[y * g->w + x] = (unsigned char)n;
        }
    }
}

void mines_set_layout(MinesGame *g, const unsigned char *mine_map)
{
    int i, n;
    if (g == NULL || mine_map == NULL) { return; }
    n = g->w * g->h;
    g->mines = 0;
    for (i = 0; i < n; i++) {
        g->mine[i] = (unsigned char)(mine_map[i] ? 1 : 0);
        if (g->mine[i]) { g->mines++; }
    }
    compute_adjacency(g);
    g->placed = CTRUE;
}

/* Scatter the mines, never under the first-clicked cell. */
static void place_mines(MinesGame *g, int avoid)
{
    int placed = 0, guard = 0;
    while (placed < g->mines && guard < 100000) {
        int i = (int)(mines_rand(g) % (unsigned)(g->w * g->h));
        guard++;
        if (i == avoid || g->mine[i]) { continue; }
        g->mine[i] = 1;
        placed++;
    }
    compute_adjacency(g);
    g->placed = CTRUE;
}

/* ---- reveal ------------------------------------------------------------ */
/* Reveal every mine (end of a lost game, so the player sees the field). */
static void expose_mines(MinesGame *g)
{
    int i, n = g->w * g->h;
    for (i = 0; i < n; i++) {
        if (g->mine[i]) { g->open[i] = 1; }
    }
}

/* Flag whatever is still covered (end of a won game: all covered = mines). */
static void flag_remaining(MinesGame *g)
{
    int i, n = g->w * g->h;
    g->flags = 0;
    for (i = 0; i < n; i++) {
        if (!g->open[i]) { g->flag[i] = 1; g->flags++; }
    }
}

/* Iterative flood reveal from a zero cell (fixed stack; no recursion on a
 * DOS-sized stack). Opens the zero region plus its numbered boundary. Cells
 * are marked open when PUSHED, so each enters the stack at most once and the
 * MINES_MAX_CELLS stack can never overflow or drop work. */
static void flood_open(MinesGame *g, int start)
{
    int stack[MINES_MAX_CELLS];
    int top = 0;
    g->open[start] = 1;
    g->opened++;
    if (g->adj[start] != 0) { return; }   /* a number: no flood from here */
    stack[top++] = start;
    while (top > 0) {
        int i = stack[--top];             /* i is an open zero cell */
        int x = i % g->w, y = i / g->w;
        int dx, dy;
        for (dy = -1; dy <= 1; dy++) {
            for (dx = -1; dx <= 1; dx++) {
                int j = cell_index(g, x + dx, y + dy);
                if (j < 0 || g->open[j] || g->flag[j] || g->mine[j]) { continue; }
                g->open[j] = 1;
                g->opened++;
                if (g->adj[j] == 0) { stack[top++] = j; }
            }
        }
    }
}

cbool mines_reveal(MinesGame *g, int x, int y)
{
    int i;
    if (g == NULL || g->status != MINES_PLAYING) { return CTRUE; }
    i = cell_index(g, x, y);
    if (i < 0 || g->open[i] || g->flag[i]) { return CTRUE; }

    if (!g->placed) { place_mines(g, i); }

    if (g->mine[i]) {
        g->open[i] = 1;
        g->burst = i;
        g->status = MINES_LOST;
        expose_mines(g);
        return CFALSE;
    }
    flood_open(g, i);
    if (g->opened >= g->w * g->h - g->mines) {
        g->status = MINES_WON;
        flag_remaining(g);
    }
    return CTRUE;
}

void mines_toggle_flag(MinesGame *g, int x, int y)
{
    int i;
    if (g == NULL || g->status != MINES_PLAYING) { return; }
    i = cell_index(g, x, y);
    if (i < 0 || g->open[i]) { return; }
    if (g->flag[i]) { g->flag[i] = 0; g->flags--; }
    else            { g->flag[i] = 1; g->flags++; }
}

int mines_flags_left(const MinesGame *g)
{
    return (g != NULL) ? g->mines - g->flags : 0;
}

/* ---- accessors --------------------------------------------------------- */
cbool mines_is_open(const MinesGame *g, int x, int y)
{
    int i = (g != NULL) ? cell_index(g, x, y) : -1;
    return (i >= 0 && g->open[i]) ? CTRUE : CFALSE;
}
cbool mines_is_flag(const MinesGame *g, int x, int y)
{
    int i = (g != NULL) ? cell_index(g, x, y) : -1;
    return (i >= 0 && g->flag[i]) ? CTRUE : CFALSE;
}
cbool mines_is_mine(const MinesGame *g, int x, int y)
{
    int i = (g != NULL) ? cell_index(g, x, y) : -1;
    return (i >= 0 && g->mine[i]) ? CTRUE : CFALSE;
}
int mines_adjacent(const MinesGame *g, int x, int y)
{
    int i = (g != NULL) ? cell_index(g, x, y) : -1;
    return (i >= 0) ? (int)g->adj[i] : 0;
}
