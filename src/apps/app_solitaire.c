/*
 * app_solitaire.c - Klondike Solitaire (patience), CastaliaOS style.
 *
 * The classic seven-column patience game, fully playable with the mouse via
 * click-to-move: click a source card (waste top, a foundation top, or a
 * face-up tableau card and the valid run beneath it), then click a legal
 * destination (a tableau column or a foundation). Clicking the stock draws a
 * card to the waste; clicking an empty stock recycles the waste. Double-click
 * a waste or tableau top card to auto-send it to a foundation. N / F2 / the
 * New Game button reshuffle; Esc cancels a selection.
 *
 * Everything (state + rules + rendering) lives in this one file; the board
 * logic mirrors app_mines.c's window/paint/hit-test structure.
 */
#include "apps.h"
#include "sol_core.h"
#include "card_draw.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include <stdlib.h>  /* getenv (host-only SOL_WIN recorder hook) */


/* ---- geometry constants ---------------------------------------------- */
#define CARD_W       58
#define CARD_H       76
#define COL_GAP      10
#define TOOLBAR_H    28
#define TOP_Y        40                 /* stock/waste/foundation row      */
#define TABLEAU_Y    128                /* tableau columns begin here      */
#define FU_OFFSET    18                 /* vertical fan for a face-up card */
#define FD_OFFSET    8                  /* vertical fan for a face-down    */
#define BOARD_W      (7 * CARD_W + 6 * COL_GAP)
#define SOL_DBLCLICK_MS 350u

/* Storage caps (a tableau column never exceeds ~19 in legal play; stock and
 * waste peak at the 24 non-tableau cards; a foundation holds A..K = 13). */
#define TAB_MAX      24
#define PILE_MAX     24
#define FND_MAX      13

/* Win-cascade animation tuning (positions/velocities are in 1/8-px units). */
#define SOL_PROJ_MAX     14   /* cards in flight at once            */
#define SOL_GRAV8        6    /* downward acceleration per frame    */
#define SOL_VYMAX8       128  /* terminal fall speed (16 px/frame)  */
#define SOL_LAUNCH_EVERY 3    /* frames between card launches       */

/* Suits: clubs+spades are BLACK, diamonds+hearts are RED. */

/* Hit / selection kinds. HIT_NONE also means "no selection". */
enum { HIT_NONE = 0, HIT_NEW, HIT_STOCK, HIT_WASTE, HIT_FOUND, HIT_TAB };


typedef struct {
    Card  tableau[7][TAB_MAX];
    int   tab_count[7];
    Card  stock[PILE_MAX];
    int   stock_count;
    Card  waste[PILE_MAX];
    int   waste_count;
    Card  found[4][FND_MAX];
    int   found_count[4];

    cu32  seed;

    int   sel_kind;   /* HIT_NONE / HIT_WASTE / HIT_FOUND / HIT_TAB */
    int   sel_col;    /* foundation index or tableau column         */
    int   sel_card;   /* first card of a tableau run                */

    int   moves;
    cbool won;
    cu32  start_ms;

    int   last_click_kind;
    int   last_click_col;
    int   last_click_card;
    cu32  last_click_ms;

    /* Drag-and-drop: a gesture is armed on button-down over a movable card and
     * resolved on button-up (a plain click falls through to sol_on_click). */
    cbool drag_armed;
    cbool drag_active;
    int   drag_kind, drag_col, drag_card;  /* the picked-up source          */
    int   down_x, down_y;                  /* button-down position (client) */
    int   drag_x, drag_y;                  /* current cursor (client)        */

    /* Win cascade: the classic bouncing-cards waterfall. Cards fly off the
     * foundations under gravity, bounce off the floor, and leave a trail
     * accumulated into an offscreen buffer that fills the felt. */
    cbool cascading;      /* still launching cards (window is animated)      */
    GfxSurface *trail;    /* client-sized accumulation buffer (NULL = none)  */
    int   cascade_col;    /* next foundation to draw a card from             */
    int   cascade_frame;  /* tick counter (paces launches)                   */
    struct {
        Card  card;
        int   x8, y8, vx8, vy8;   /* position/velocity in 1/8-px units       */
        cbool active;
    } proj[SOL_PROJ_MAX];
    UiHot hot;            /* the New Game button, under the pointer         */
} Sol;

typedef struct {
    int   ox;        /* board left edge, client coords */
    CRect toolbar;
    CRect newbtn;
} SolLayout;

/* ---- tiny helpers ----------------------------------------------------- */


static void sol_draw_slot(GfxSurface *s, const CRect *r)
{
    CColor ring = GFX_RGB(0x06, 0x45, 0x1F);
    CColor face = GFX_RGB(0x0A, 0x5E, 0x2A);
    CRect inner;
    gfx_fill_round_rect(s, r, 5, ring);
    inner = crect_inset(r, 2);
    gfx_fill_round_rect(s, &inner, 4, face);
}

/* ---- layout / geometry ------------------------------------------------ */
static void sol_layout(WmWindow *win, SolLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c);
    L->ox = (cw - BOARD_W) / 2;
    if (L->ox < 6) { L->ox = 6; }
    L->toolbar = crect_make(0, 0, cw, TOOLBAR_H);
    L->newbtn = crect_make(8, 5, 90, TOOLBAR_H - 10);
}

static int sol_col_x(const SolLayout *L, int col)
{
    return L->ox + col * (CARD_W + COL_GAP);
}
static CRect sol_stock_rect(const SolLayout *L)
{
    return crect_make(sol_col_x(L, 0), TOP_Y, CARD_W, CARD_H);
}
static CRect sol_waste_rect(const SolLayout *L)
{
    return crect_make(sol_col_x(L, 1), TOP_Y, CARD_W, CARD_H);
}
static CRect sol_found_rect(const SolLayout *L, int f)
{
    return crect_make(sol_col_x(L, 3 + f), TOP_Y, CARD_W, CARD_H);
}

/* Client-coord y of the top edge of tableau card k in column col. */
static int sol_tab_card_top(const Sol *g, int col, int k)
{
    int j, y = TABLEAU_Y;
    for (j = 0; j < k; j++) {
        y += g->tableau[col][j].face_up ? FU_OFFSET : FD_OFFSET;
    }
    return y;
}

/* ---- deal / new game -------------------------------------------------- */
static void sol_new_game(Sol *g)
{
    Card deck[52];
    int i, col, k, pos;

    g->seed = g->seed * 1103515245u + 12345u;   /* advance for a fresh deal */

    sol_deal(deck, &g->seed);

    for (col = 0; col < 7; col++) { g->tab_count[col] = 0; }
    for (i = 0; i < 4; i++) { g->found_count[i] = 0; }
    g->stock_count = 0;
    g->waste_count = 0;

    pos = 0;
    for (col = 0; col < 7; col++) {
        for (k = 0; k <= col; k++) {
            Card c = deck[pos++];
            c.face_up = (k == col) ? CTRUE : CFALSE;
            g->tableau[col][g->tab_count[col]++] = c;
        }
    }
    while (pos < 52) {
        Card c = deck[pos++];
        c.face_up = CFALSE;
        g->stock[g->stock_count++] = c;
    }

    g->sel_kind = HIT_NONE;
    g->sel_col = -1;
    g->sel_card = -1;
    g->moves = 0;
    g->won = CFALSE;
    g->start_ms = sys_now_ms();
    g->last_click_kind = HIT_NONE;
    g->last_click_col = -1;
    g->last_click_card = -1;
    g->last_click_ms = 0;
}

/* ---- rules ------------------------------------------------------------ */
/*
 * The rules themselves are sol_core.c, where tests/test_sol.c can reach them.
 * These three are the board's way of asking: they find the card in question
 * and hand it over. Which card may go on which is not decided here.
 */
static cbool sol_can_stack_tab(const Sol *g, int col, const Card *c)
{
    const Card *top = (g->tab_count[col] == 0)
                    ? (const Card *)0
                    : &g->tableau[col][g->tab_count[col] - 1];
    return sol_can_stack_tab_rule(top, c);
}

static cbool sol_can_stack_found(const Sol *g, int f, const Card *c)
{
    const Card *top = (g->found_count[f] == 0)
                    ? (const Card *)0
                    : &g->found[f][g->found_count[f] - 1];
    return sol_can_stack_found_rule(top, c);
}

static cbool sol_run_valid(const Sol *g, int col, int start)
{
    int n = g->tab_count[col];
    if (start < 0 || start >= n) { return CFALSE; }
    return sol_run_valid_rule(&g->tableau[col][start], n - start);
}

/* Flip the new top of a tableau column face-up after cards leave it. */
static void sol_flip_top(Sol *g, int col)
{
    int n = g->tab_count[col];
    if (n > 0 && !g->tableau[col][n - 1].face_up) {
        g->tableau[col][n - 1].face_up = CTRUE;
    }
}

/* First foundation that would accept card c (matching suit, else an empty
 * one for an Ace), or -1. */
static int sol_foundation_for(const Sol *g, const Card *c)
{
    int f, empty = -1;
    for (f = 0; f < 4; f++) {
        if (g->found_count[f] > 0) {
            if (sol_can_stack_found(g, f, c)) { return f; }
        } else if (empty < 0) {
            empty = f;
        }
    }
    if (c->rank == 1 && empty >= 0) { return empty; }
    return -1;
}

static void sol_start_cascade(WmWindow *win, Sol *g);   /* defined below */

static void sol_check_win(WmWindow *win, Sol *g)
{
    g->won = sol_is_won(g->found_count, 4);
    if (g->won && g->trail == NULL) { sol_start_cascade(win, g); }
}

/* ---- move execution (driven by the current selection) ----------------- */
static cbool sol_move_to_tab(Sol *g, int dc)
{
    if (g->sel_kind == HIT_WASTE) {
        Card *c;
        if (g->waste_count <= 0) { return CFALSE; }
        c = &g->waste[g->waste_count - 1];
        if (!sol_can_stack_tab(g, dc, c)) { return CFALSE; }
        g->tableau[dc][g->tab_count[dc]] = *c;
        g->tableau[dc][g->tab_count[dc]].face_up = CTRUE;
        g->tab_count[dc]++;
        g->waste_count--;
        return CTRUE;
    } else if (g->sel_kind == HIT_FOUND) {
        int f = g->sel_col;
        Card *c;
        if (f < 0 || g->found_count[f] <= 0) { return CFALSE; }
        c = &g->found[f][g->found_count[f] - 1];
        if (!sol_can_stack_tab(g, dc, c)) { return CFALSE; }
        g->tableau[dc][g->tab_count[dc]] = *c;
        g->tableau[dc][g->tab_count[dc]].face_up = CTRUE;
        g->tab_count[dc]++;
        g->found_count[f]--;
        return CTRUE;
    } else if (g->sel_kind == HIT_TAB) {
        int sc = g->sel_col, start = g->sel_card, k, n;
        if (sc < 0 || sc == dc) { return CFALSE; }
        if (!sol_run_valid(g, sc, start)) { return CFALSE; }
        if (!sol_can_stack_tab(g, dc, &g->tableau[sc][start])) { return CFALSE; }
        n = g->tab_count[sc];
        for (k = start; k < n; k++) {
            g->tableau[dc][g->tab_count[dc]++] = g->tableau[sc][k];
        }
        g->tab_count[sc] = start;
        sol_flip_top(g, sc);
        return CTRUE;
    }
    return CFALSE;
}

static cbool sol_move_to_found(Sol *g, int f)
{
    if (f < 0) { return CFALSE; }
    if (g->sel_kind == HIT_WASTE) {
        Card *c;
        if (g->waste_count <= 0) { return CFALSE; }
        c = &g->waste[g->waste_count - 1];
        if (!sol_can_stack_found(g, f, c)) { return CFALSE; }
        g->found[f][g->found_count[f]++] = *c;
        g->waste_count--;
        return CTRUE;
    } else if (g->sel_kind == HIT_TAB) {
        int sc = g->sel_col, start = g->sel_card;
        Card *c;
        if (sc < 0 || start != g->tab_count[sc] - 1) { return CFALSE; }
        c = &g->tableau[sc][start];
        if (!sol_can_stack_found(g, f, c)) { return CFALSE; }
        g->found[f][g->found_count[f]++] = *c;
        g->tab_count[sc]--;
        sol_flip_top(g, sc);
        return CTRUE;
    }
    return CFALSE;
}

/* Draw a card from the stock to the waste, or recycle the waste. */
static void sol_draw_stock(Sol *g)
{
    if (g->stock_count > 0) {
        Card c = g->stock[g->stock_count - 1];
        g->stock_count--;
        c.face_up = CTRUE;
        g->waste[g->waste_count++] = c;
    } else {
        int i, n = g->waste_count;
        for (i = 0; i < n; i++) {           /* reversed onto the stock      */
            Card c = g->waste[n - 1 - i];
            c.face_up = CFALSE;
            g->stock[i] = c;
        }
        g->stock_count = n;
        g->waste_count = 0;
    }
    g->moves++;
}

/* Try to make (kind,col,card) the current selection source. */
static void sol_try_select(Sol *g, int kind, int col, int card)
{
    if (kind == HIT_WASTE && g->waste_count > 0) {
        g->sel_kind = HIT_WASTE; g->sel_col = -1; g->sel_card = -1;
    } else if (kind == HIT_FOUND && col >= 0 && g->found_count[col] > 0) {
        g->sel_kind = HIT_FOUND; g->sel_col = col; g->sel_card = -1;
    } else if (kind == HIT_TAB && col >= 0 && card >= 0 &&
               g->tableau[col][card].face_up) {
        g->sel_kind = HIT_TAB; g->sel_col = col; g->sel_card = card;
    } else {
        g->sel_kind = HIT_NONE; g->sel_col = -1; g->sel_card = -1;
    }
}

/* ---- hit testing ------------------------------------------------------ */
static int sol_hit(WmWindow *win, int px, int py, int *out_col, int *out_card)
{
    Sol *g = (Sol *)wm_user(win);
    SolLayout L;
    CRect r;
    int col, k, cx;

    *out_col = -1;
    *out_card = -1;
    sol_layout(win, &L);

    if (crect_contains(&L.newbtn, px, py)) { return HIT_NEW; }

    r = sol_stock_rect(&L);
    if (crect_contains(&r, px, py)) { return HIT_STOCK; }
    r = sol_waste_rect(&L);
    if (crect_contains(&r, px, py)) { return HIT_WASTE; }
    for (col = 0; col < 4; col++) {
        r = sol_found_rect(&L, col);
        if (crect_contains(&r, px, py)) { *out_col = col; return HIT_FOUND; }
    }

    for (col = 0; col < 7; col++) {
        cx = sol_col_x(&L, col);
        if (px < cx || px >= cx + CARD_W) { continue; }
        if (g->tab_count[col] == 0) {
            r = crect_make(cx, TABLEAU_Y, CARD_W, CARD_H);
            if (crect_contains(&r, px, py)) { *out_col = col; return HIT_TAB; }
            continue;
        }
        for (k = g->tab_count[col] - 1; k >= 0; k--) {
            int ty = sol_tab_card_top(g, col, k);
            r = crect_make(cx, ty, CARD_W, CARD_H);
            if (crect_contains(&r, px, py)) {
                *out_col = col; *out_card = k; return HIT_TAB;
            }
        }
    }
    return HIT_NONE;
}

/* ---- painting --------------------------------------------------------- */
static cbool sol_sel_at(const Sol *g, int kind, int col, int k)
{
    if (g->sel_kind != kind) { return CFALSE; }
    if (kind == HIT_WASTE) { return CTRUE; }
    if (kind == HIT_FOUND) { return (g->sel_col == col) ? CTRUE : CFALSE; }
    if (kind == HIT_TAB) {
        return (g->sel_col == col && k >= g->sel_card) ? CTRUE : CFALSE;
    }
    return CFALSE;
}

/* Defined below with the drag helpers; used at the end of sol_paint. */
static void sol_draw_drag(WmWindow *win, GfxSurface *s, CPoint o);

/* Draw the whole board (felt, toolbar, piles) onto 's' with client-local
 * origin 'o'. Shared by the live paint and the cascade's trail snapshot. */
static void sol_render_board(WmWindow *win, GfxSurface *s, CPoint o)
{
    Sol *g = (Sol *)wm_user(win);
    const UiPalette *p = ui_palette();
    CRect cc = wm_client_rect(win);
    int cw = crect_w(&cc), ch = crect_h(&cc);
    SolLayout L;
    CRect r, br;
    char buf[64];
    cu32 elapsed;
    int col, k, f;

    if (g == NULL) { return; }
    sol_layout(win, &L);

    /* felt background */
    r = crect_make(o.x, o.y, cw, ch);
    gfx_vgradient(s, &r, GFX_RGB(0x0E, 0x7A, 0x38), GFX_RGB(0x06, 0x4E, 0x22));

    /* toolbar */
    r = crect_offset(&L.toolbar, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    br = crect_offset(&L.newbtn, o.x, o.y);
    ui_draw_button(s, &br, "New Game",
                   ui_hot_state(&g->hot, 0, UI_BTN_NORMAL));

    elapsed = (g->start_ms != 0) ? (sys_now_ms() - g->start_ms) / 1000u : 0u;
    sys_snprintf(buf, sizeof(buf), "Moves: %d    Time: %lus",
                 g->moves, (unsigned long)elapsed);
    gfx_draw_text(s, GFX_FONT_SYSTEM, br.x1 + 12, br.y0 + 5, buf, p->text);
    if (g->won) {
        gfx_draw_text(s, GFX_FONT_BOLD, r.x1 - 60, br.y0 + 5,
                      "You Win!", p->accent);
    }

    /* stock */
    r = sol_stock_rect(&L);
    r = crect_offset(&r, o.x, o.y);
    if (g->stock_count > 0) {
        card_draw_back(s, &r);
    } else {
        int cx = (r.x0 + r.x1) / 2, cy = (r.y0 + r.y1) / 2;
        sol_draw_slot(s, &r);
        gfx_fill_circle(s, cx, cy, 11, GFX_RGB(0x2A, 0x86, 0x4E));
        gfx_fill_circle(s, cx, cy, 7, GFX_RGB(0x0A, 0x5E, 0x2A));
    }

    /* waste (top card only) */
    r = sol_waste_rect(&L);
    r = crect_offset(&r, o.x, o.y);
    if (g->waste_count > 0) {
        card_draw_face(s, &r, &g->waste[g->waste_count - 1],
                      sol_sel_at(g, HIT_WASTE, 0, 0));
    } else {
        sol_draw_slot(s, &r);
    }

    /* foundations */
    for (f = 0; f < 4; f++) {
        r = sol_found_rect(&L, f);
        r = crect_offset(&r, o.x, o.y);
        if (g->found_count[f] > 0) {
            card_draw_face(s, &r, &g->found[f][g->found_count[f] - 1],
                          sol_sel_at(g, HIT_FOUND, f, 0));
        } else {
            sol_draw_slot(s, &r);
        }
    }

    /* tableau */
    for (col = 0; col < 7; col++) {
        int cx = sol_col_x(&L, col);
        if (g->tab_count[col] == 0) {
            r = crect_make(cx + o.x, TABLEAU_Y + o.y, CARD_W, CARD_H);
            sol_draw_slot(s, &r);
            continue;
        }
        for (k = 0; k < g->tab_count[col]; k++) {
            int ty = sol_tab_card_top(g, col, k);
            r = crect_make(cx + o.x, ty + o.y, CARD_W, CARD_H);
            if (g->tableau[col][k].face_up) {
                card_draw_face(s, &r, &g->tableau[col][k],
                              sol_sel_at(g, HIT_TAB, col, k));
            } else {
                card_draw_back(s, &r);
            }
        }
    }
}

static void sol_paint(WmWindow *win, GfxSurface *s)
{
    Sol *g = (Sol *)wm_user(win);
    CPoint o = wm_client_origin(win);
    CRect cc = wm_client_rect(win);
    int cw = crect_w(&cc), ch = crect_h(&cc);

    if (g == NULL) { return; }

    if (g->trail != NULL) {
        /* Win cascade: blit the accumulated trail, then the cards in flight. */
        CRect src = crect_make(0, 0, g->trail->w, g->trail->h);
        int i;
        gfx_blit(s, o.x, o.y, g->trail, &src, GFX_BLIT_COPY);
        for (i = 0; i < SOL_PROJ_MAX; i++) {
            if (g->proj[i].active) {
                CRect cr = crect_make(o.x + g->proj[i].x8 / 8,
                                      o.y + g->proj[i].y8 / 8, CARD_W, CARD_H);
                card_draw_face(s, &cr, &g->proj[i].card, CFALSE);
            }
        }
    } else {
        sol_render_board(win, s, o);
    }

    /* win banner */
    if (g->won) {
        int bw = 168, bh = 46;
        CRect bb = crect_make(o.x + (cw - bw) / 2, o.y + (ch - bh) / 2, bw, bh);
        CRect ib;
        gfx_fill_round_rect(s, &bb, 8, GFX_RGB(0xC8, 0x9A, 0x22));
        ib = crect_inset(&bb, 3);
        gfx_fill_round_rect(s, &ib, 6, GFX_RGB(0xFF, 0xF6, 0xD8));
        gfx_draw_text_rect(s, GFX_FONT_BOLD, &bb, "You Win!",
                           GFX_RGB(0xB0, 0x30, 0x10),
                           GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
    }

    if (g->drag_active) { sol_draw_drag(win, s, o); }
}

/* ---- win cascade ------------------------------------------------------ */
/* Free the trail buffer and reset cascade state (new game / close). */
static void sol_cascade_free(WmWindow *win, Sol *g)
{
    int i;
    if (g->trail != NULL) { gfx_surface_free(g->trail); g->trail = NULL; }
    g->cascading = CFALSE;
    for (i = 0; i < SOL_PROJ_MAX; i++) { g->proj[i].active = CFALSE; }
    if (win != NULL) { wm_set_animated(win, CFALSE); }
}

/* Begin the bouncing-cards waterfall: snapshot the won board into an offscreen
 * trail buffer and start ticking. Falls back to the static banner on OOM. */
static void sol_start_cascade(WmWindow *win, Sol *g)
{
    CRect cc = wm_client_rect(win);
    CPoint zero; zero.x = 0; zero.y = 0;
    if (g->trail != NULL) { return; }        /* already cascading */
    g->trail = gfx_surface_new(crect_w(&cc), crect_h(&cc));
    if (g->trail == NULL) { return; }         /* low memory: keep the banner */
    sol_render_board(win, g->trail, zero);    /* snapshot the full board */
    g->cascading = CTRUE;
    g->cascade_col = 0;
    g->cascade_frame = 0;
    wm_set_animated(win, CTRUE);
    wm_invalidate(win, NULL);
}

/* Launch one card from the next non-empty foundation into a free slot. */
static void sol_cascade_launch(WmWindow *win, Sol *g)
{
    SolLayout L;
    CRect fr;
    int slot = -1, tries, i, rr, dir;
    for (i = 0; i < SOL_PROJ_MAX; i++) { if (!g->proj[i].active) { slot = i; break; } }
    if (slot < 0) { return; }
    for (tries = 0; tries < 4 && g->found_count[g->cascade_col] == 0; tries++) {
        g->cascade_col = (g->cascade_col + 1) % 4;
    }
    if (g->found_count[g->cascade_col] == 0) { return; }  /* all empty */
    sol_layout(win, &L);
    fr = sol_found_rect(&L, g->cascade_col);
    g->proj[slot].card = g->found[g->cascade_col][--g->found_count[g->cascade_col]];
    g->proj[slot].x8 = fr.x0 * 8;
    g->proj[slot].y8 = fr.y0 * 8;
    rr = sol_rand(&g->seed);
    dir = (rr & 1) ? 1 : -1;
    g->proj[slot].vx8 = dir * (2 + (rr % 4)) * 8;   /* 2..5 px/frame sideways */
    g->proj[slot].vy8 = -((rr >> 4) % 7) * 8;       /* small upward kick      */
    g->proj[slot].active = CTRUE;
    g->cascade_col = (g->cascade_col + 1) % 4;       /* spread across suits    */
}

static void sol_cascade_tick(WmWindow *win, Sol *g)
{
    CRect cc = wm_client_rect(win);
    int cw = crect_w(&cc), bottom8 = (crect_h(&cc) - CARD_H) * 8;
    int i, any = CFALSE, remaining = 0, f;
    if (g->trail == NULL) { return; }
    /* Pace the launches so several cards are aloft at once. */
    if ((g->cascade_frame++ % SOL_LAUNCH_EVERY) == 0) { sol_cascade_launch(win, g); }
    for (i = 0; i < SOL_PROJ_MAX; i++) {
        CRect cr;
        if (!g->proj[i].active) { continue; }
        any = CTRUE;
        g->proj[i].vy8 += SOL_GRAV8;
        if (g->proj[i].vy8 > SOL_VYMAX8) { g->proj[i].vy8 = SOL_VYMAX8; }
        g->proj[i].x8 += g->proj[i].vx8;
        g->proj[i].y8 += g->proj[i].vy8;
        if (g->proj[i].y8 >= bottom8) {
            g->proj[i].y8 = bottom8;
            g->proj[i].vy8 = -(g->proj[i].vy8 * 3) / 4;   /* bounce ~75%      */
        }
        cr = crect_make(g->proj[i].x8 / 8, g->proj[i].y8 / 8, CARD_W, CARD_H);
        card_draw_face(g->trail, &cr, &g->proj[i].card, CFALSE);
        if (g->proj[i].x8 / 8 <= -CARD_W || g->proj[i].x8 / 8 >= cw) {
            g->proj[i].active = CFALSE;
        }
    }
    for (f = 0; f < 4; f++) { remaining += g->found_count[f]; }
    if (!any && remaining == 0) {
        /* All cards launched and gone: stop ticking, keep the filled felt. */
        g->cascading = CFALSE;
        wm_set_animated(win, CFALSE);
    }
    wm_invalidate(win, NULL);
}

/* ---- input ------------------------------------------------------------ */
static void sol_on_click(WmWindow *win, int px, int py)
{
    Sol *g = (Sol *)wm_user(win);
    int kind, col = -1, card = -1;
    cu32 now;
    cbool dbl;

    if (g == NULL) { return; }
    kind = sol_hit(win, px, py, &col, &card);
    now = sys_now_ms();
    dbl = (kind != HIT_NONE && kind == g->last_click_kind &&
           col == g->last_click_col && card == g->last_click_card &&
           (now - g->last_click_ms) < SOL_DBLCLICK_MS) ? CTRUE : CFALSE;
    g->last_click_kind = kind;
    g->last_click_col = col;
    g->last_click_card = card;
    g->last_click_ms = now;

    if (kind == HIT_NEW) {
        sol_cascade_free(win, g);
        sol_new_game(g);
        wm_invalidate(win, NULL);
        return;
    }
    if (kind == HIT_STOCK) {
        sol_draw_stock(g);
        g->sel_kind = HIT_NONE;
        wm_invalidate(win, NULL);
        return;
    }

    /* double-click: auto-send a waste/tableau top card to a foundation */
    if (dbl) {
        Card *c = NULL;
        if (kind == HIT_WASTE && g->waste_count > 0) {
            c = &g->waste[g->waste_count - 1];
        } else if (kind == HIT_TAB && col >= 0 && card >= 0 &&
                   card == g->tab_count[col] - 1 &&
                   g->tableau[col][card].face_up) {
            c = &g->tableau[col][card];
        }
        if (c != NULL) {
            int fnd = sol_foundation_for(g, c);
            if (fnd >= 0) {
                if (kind == HIT_WASTE) {
                    g->sel_kind = HIT_WASTE; g->sel_col = -1; g->sel_card = -1;
                } else {
                    g->sel_kind = HIT_TAB; g->sel_col = col; g->sel_card = card;
                }
                if (sol_move_to_found(g, fnd)) {
                    g->moves++;
                    sol_check_win(win, g);
                }
                g->sel_kind = HIT_NONE;
                g->last_click_kind = HIT_NONE;
                wm_invalidate(win, NULL);
                return;
            }
        }
    }

    if (g->sel_kind != HIT_NONE) {
        cbool moved = CFALSE;
        /* clicking the same source again cancels the selection */
        if (kind == g->sel_kind &&
            ((kind == HIT_WASTE) ||
             (kind == HIT_FOUND && col == g->sel_col) ||
             (kind == HIT_TAB && col == g->sel_col && card == g->sel_card))) {
            g->sel_kind = HIT_NONE;
            wm_invalidate(win, NULL);
            return;
        }
        if (kind == HIT_TAB && col >= 0) {
            moved = sol_move_to_tab(g, col);
        } else if (kind == HIT_FOUND && col >= 0) {
            moved = sol_move_to_found(g, col);
        }
        if (moved) {
            g->moves++;
            g->sel_kind = HIT_NONE;
            sol_check_win(win, g);
            wm_invalidate(win, NULL);
            return;
        }
        /* illegal destination: drop selection, then retarget if it's a source */
        g->sel_kind = HIT_NONE;
        sol_try_select(g, kind, col, card);
        wm_invalidate(win, NULL);
        return;
    }

    sol_try_select(g, kind, col, card);
    wm_invalidate(win, NULL);
}

/* ---- drag and drop ---------------------------------------------------- */
/* Is the source at (kind,col,card) a card that can be picked up and dragged? */
static cbool sol_source_movable(Sol *g, int kind, int col, int card)
{
    if (kind == HIT_WASTE) { return (g->waste_count > 0) ? CTRUE : CFALSE; }
    if (kind == HIT_FOUND) { return (col >= 0 && g->found_count[col] > 0) ? CTRUE : CFALSE; }
    if (kind == HIT_TAB && col >= 0 && card >= 0 &&
        card < g->tab_count[col] && g->tableau[col][card].face_up) { return CTRUE; }
    return CFALSE;
}

/* Button-down: arm a drag if a movable card is under the point (the click
 * semantics themselves run on button-up so both styles coexist). */
static void sol_on_down(WmWindow *win, int px, int py)
{
    Sol *g = (Sol *)wm_user(win);
    int kind, col = -1, card = -1;
    if (g == NULL) { return; }
    g->drag_armed = CFALSE;
    g->drag_active = CFALSE;
    kind = sol_hit(win, px, py, &col, &card);
    if (sol_source_movable(g, kind, col, card)) {
        g->drag_armed = CTRUE;
        g->drag_kind = kind; g->drag_col = col; g->drag_card = card;
        g->down_x = px; g->down_y = py; g->drag_x = px; g->drag_y = py;
    }
}

/* Button-up: a real drag drops onto the pile under the cursor; a plain click
 * (no drag) falls through to the existing click-to-move handler. */
static void sol_on_up(WmWindow *win, int px, int py)
{
    Sol *g = (Sol *)wm_user(win);
    if (g == NULL) { return; }
    if (g->drag_active) {
        int dk, dcol = -1, dcard = -1;
        cbool moved = CFALSE;
        dk = sol_hit(win, px, py, &dcol, &dcard);
        /* Restore the picked-up card as the selection, then try the drop. */
        g->sel_kind = g->drag_kind; g->sel_col = g->drag_col; g->sel_card = g->drag_card;
        if (dk == HIT_TAB && dcol >= 0 && !(g->drag_kind == HIT_TAB && dcol == g->drag_col)) {
            moved = sol_move_to_tab(g, dcol);
        } else if (dk == HIT_FOUND && dcol >= 0 &&
                   !(g->drag_kind == HIT_FOUND && dcol == g->drag_col)) {
            moved = sol_move_to_found(g, dcol);
        }
        if (moved) { g->moves++; sol_check_win(win, g); }
        g->sel_kind = HIT_NONE;
        g->drag_armed = CFALSE; g->drag_active = CFALSE;
        wm_invalidate(win, NULL);
        return;
    }
    g->drag_armed = CFALSE;
    sol_on_click(win, px, py);   /* a plain click: select / move / stock / new */
}

static void sol_on_motion(WmWindow *win, int px, int py)
{
    Sol *g = (Sol *)wm_user(win);
    if (g == NULL) { return; }
    if (!g->drag_armed && !g->drag_active) {
        SolLayout L;
        sol_layout(win, &L);
        if (ui_hot_move(&g->hot,
                        crect_contains(&L.newbtn, px, py) ? 0 : -1)) {
            ui_hot_repaint(win, &g->hot, &L.newbtn, 1);
        }
        return;
    }
    if (!g->drag_active) {
        int dx = px - g->down_x, dy = py - g->down_y;
        if (dx < 0) { dx = -dx; }
        if (dy < 0) { dy = -dy; }
        if (dx > 4 || dy > 4) { g->drag_active = CTRUE; }
    }
    if (g->drag_active) { g->drag_x = px; g->drag_y = py; wm_invalidate(win, NULL); }
}

/* Draw the card(s) being dragged, following the cursor (client coords + o). */
static void sol_draw_drag(WmWindow *win, GfxSurface *s, CPoint o)
{
    Sol *g = (Sol *)wm_user(win);
    int bx = o.x + g->drag_x - CARD_W / 2;
    int by = o.y + g->drag_y - 12;
    if (g->drag_kind == HIT_TAB) {
        int k;
        for (k = g->drag_card; k < g->tab_count[g->drag_col]; k++) {
            CRect r = crect_make(bx, by + (k - g->drag_card) * FU_OFFSET, CARD_W, CARD_H);
            card_draw_face(s, &r, &g->tableau[g->drag_col][k], CFALSE);
        }
    } else if (g->drag_kind == HIT_WASTE && g->waste_count > 0) {
        CRect r = crect_make(bx, by, CARD_W, CARD_H);
        card_draw_face(s, &r, &g->waste[g->waste_count - 1], CFALSE);
    } else if (g->drag_kind == HIT_FOUND && g->drag_col >= 0 &&
               g->found_count[g->drag_col] > 0) {
        CRect r = crect_make(bx, by, CARD_W, CARD_H);
        card_draw_face(s, &r, &g->found[g->drag_col][g->found_count[g->drag_col] - 1], CFALSE);
    }
}

static void sol_on_key(WmWindow *win, int key, int ch)
{
    Sol *g = (Sol *)wm_user(win);
    if (g == NULL) { return; }
    if (ch == 'n' || ch == 'N' || key == PLAT_KEY_F2) {
        sol_cascade_free(win, g);
        sol_new_game(g);
        wm_invalidate(win, NULL);
    } else if (key == PLAT_KEY_ESC) {
        if (g->sel_kind != HIT_NONE) {
            g->sel_kind = HIT_NONE;
            wm_invalidate(win, NULL);
        }
    }
}

static cbool sol_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    switch (msg) {
    case WM_MSG_PAINT:       sol_paint(win, (GfxSurface *)param);      return CTRUE;
    case WM_MSG_LBUTTONDOWN: sol_on_down(win, (int)a, (int)b);         return CTRUE;
    case WM_MSG_LBUTTONUP:   sol_on_up(win, (int)a, (int)b);           return CTRUE;
    case WM_MSG_MOUSELEAVE: {
        Sol *g = (Sol *)wm_user(win);
        cbool redraw;
        if (g == NULL) { return CFALSE; }
        redraw = ui_hot_release(&g->hot);
        if (ui_hot_move(&g->hot, -1)) { redraw = CTRUE; }
        if (redraw) {
            SolLayout L;
            sol_layout(win, &L);
            ui_hot_repaint(win, &g->hot, &L.newbtn, 1);
        }
        return CTRUE;
    }
    case WM_MSG_MOUSEMOVE:   sol_on_motion(win, (int)a, (int)b);       return CTRUE;
    case WM_MSG_KEYDOWN:     sol_on_key(win, (int)a, (int)b);          return CTRUE;
    case WM_MSG_TIMER: {
        Sol *g = (Sol *)wm_user(win);
        if (g != NULL && g->cascading) { sol_cascade_tick(win, g); }
        return CTRUE;
    }
    case WM_MSG_DESTROY: {
        Sol *g = (Sol *)wm_user(win);
        if (g != NULL) { sol_cascade_free(win, g); sys_free(g, (cu32)sizeof(Sol)); }
        return CTRUE;
    }
    default: return CFALSE;
    }
}

void app_solitaire_open(void)
{
    Sol *g;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw = 516, ch = 556;

    g = (Sol *)sys_calloc(1, (cu32)sizeof(Sol));
    if (g == NULL) { SYS_LOGE("app", "solitaire: OOM"); return; }
    g->seed = sys_now_ms() * 2654435761u + 1u;
    sol_new_game(g);

    plat_video_info(&vi);
    /* The work area, not the screen. At 640x480 a hundred and two pixels of
     * the table were under the taskbar, which is where the bottom of a
     * tableau pile lives. */
    frame = wm_place_centered(cw, ch);

    w = wm_create("Solitaire", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_MAXIMIZE | WM_STYLE_BORDER,
                  sol_proc, g);
    if (w == NULL) { sys_free(g, (cu32)sizeof(Sol)); return; }
#ifdef CASTALIA_HOST
    /* Host-only marketing/QA hook: SOL_WIN=1 forces a won board so the win
     * cascade can be recorded (tools/make_clips.sh). Never in the DOS build. */
    if (getenv("SOL_WIN") != NULL) {
        int ff, kk;
        for (ff = 0; ff < 7; ff++) { g->tab_count[ff] = 0; }
        g->stock_count = 0; g->waste_count = 0;
        for (ff = 0; ff < 4; ff++) {
            g->found_count[ff] = 13;
            for (kk = 0; kk < 13; kk++) {
                g->found[ff][kk].rank = kk + 1; g->found[ff][kk].suit = ff;
                g->found[ff][kk].face_up = CTRUE;
            }
        }
        sol_check_win(w, g);
    }
#endif
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened Solitaire");
}
