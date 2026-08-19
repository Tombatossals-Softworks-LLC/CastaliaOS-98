/*
 * app_reversi.c - Reversi: the eight-by-eight disc-flipping game, against the
 * machine. Black is the player, White is the program.
 *
 * The rules live in rev_core.c (pure, host-tested); this file is only the
 * window -- the board, the discs, hit-testing and input. Fully keyboard-
 * playable like every Castalia app: the arrows move a cell cursor, Enter or
 * Space plays, N starts again.
 *
 * Two things the window does that the rules cannot:
 *
 *   - it SHOWS the legal moves, as small hollow marks. Reversi is much harder
 *     to read than chess or draughts -- every square is a disc of one of two
 *     colours and legality depends on runs you have to trace by eye -- so a
 *     board that does not show them is a board most people give up on.
 *   - it handles the PASS out loud. A turn silently coming back to you looks
 *     exactly like the program ignoring your move, so the status line says
 *     which side had nothing to play.
 */
#include "apps.h"
#include "rev_core.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/snd.h"
#include "castalia/sys.h"

#define RV_CELL    26
#define RV_MARGIN  8
#define RV_HEAD_H  26
#define RV_STATUS  18

/* The felt, and the discs. Original colours, in the era's palette. */
#define RV_FELT     GFX_RGB(0x1E, 0x6B, 0x38)
#define RV_FELT_HI  GFX_RGB(0x27, 0x82, 0x45)
#define RV_GRID     GFX_RGB(0x12, 0x45, 0x24)
#define RV_BLACKD   GFX_RGB(0x18, 0x18, 0x1C)
#define RV_WHITED   GFX_RGB(0xF2, 0xF2, 0xEC)

typedef struct {
    RevBoard b;
    int   kx, ky;         /* keyboard cell cursor                          */
    /*
      * Frames left before White plays. A countdown rather than a flag
      * because the shell handles input and then ticks animations in the
      * SAME frame: with a flag, White's reply landed in the very frame the
      * player clicked, so the board jumped two moves at once and the
      * player never saw their own disc go down. Three frames is enough to
      * read as an answer rather than a flicker.
      */
     int   think;
    char  status[72];
} Reversi;

typedef struct {
    CRect head, board, status;
} RvLayout;

static void rv_layout(WmWindow *win, RvLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    L->head   = crect_make(0, 0, cw, RV_HEAD_H);
    L->board  = crect_make(RV_MARGIN, RV_HEAD_H,
                           REV_N * RV_CELL, REV_N * RV_CELL);
    L->status = crect_make(0, ch - RV_STATUS, cw, RV_STATUS);
}

static CRect rv_cell_rect(const RvLayout *L, int r, int c)
{
    return crect_make(L->board.x0 + c * RV_CELL, L->board.y0 + r * RV_CELL,
                      RV_CELL, RV_CELL);
}

/* A filled disc with a highlight, drawn as a circle rather than a square
 * because a square disc reads as a tile and the whole game is about discs
 * turning over. */
static void rv_disc(GfxSurface *s, const CRect *cell, int who)
{
    int cx = (cell->x0 + cell->x1) / 2;
    int cy = (cell->y0 + cell->y1) / 2;
    int rad = RV_CELL / 2 - 3;
    CColor face = (who == REV_BLACK) ? RV_BLACKD : RV_WHITED;
    CColor edge = (who == REV_BLACK) ? GFX_RGB(0x00, 0x00, 0x00)
                                     : GFX_RGB(0xA8, 0xA8, 0xA4);
    gfx_fill_circle(s, cx, cy + 1, rad, edge);      /* a seated shadow */
    gfx_fill_circle(s, cx, cy, rad, face);
    /* A small light off-centre, so a white disc on green still reads as a
     * disc and not a hole. */
    gfx_fill_circle(s, cx - rad / 3, cy - rad / 3, rad / 4,
                    (who == REV_BLACK) ? GFX_RGB(0x4A, 0x4A, 0x52)
                                       : GFX_RGB(0xFF, 0xFF, 0xFF));
}

static void rv_set_status(Reversi *g, const char *msg)
{
    sys_strlcpy(g->status, msg, sizeof g->status);
}

static void rv_new_game(Reversi *g)
{
    rev_init(&g->b);
    g->kx = 3; g->ky = 2;
    g->think = 0;
    rv_set_status(g, "Your move -- black");
}

/* Announce the score in the way a player actually wants it. */
static void rv_score_line(const Reversi *g, char *out, cu32 sz)
{
    int bl = 0, wh = 0;
    rev_score(&g->b, &bl, &wh);
    if (rev_game_over(&g->b)) {
        const char *verdict = (bl > wh) ? "You win" :
                              (wh > bl) ? "White wins" : "A draw";
        sys_snprintf(out, sz, "%s -- %d to %d.  N for a new game", verdict,
                     bl, wh);
    } else {
        sys_snprintf(out, sz, "Black %d   White %d", bl, wh);
    }
}

static void rv_paint(WmWindow *win, GfxSurface *s)
{
    Reversi *g = (Reversi *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    RvLayout L;
    CRect r;
    int rr, cc;
    char line[80];

    if (g == NULL) { return; }
    rv_layout(win, &L);

    /* Header: the running score, in the window's own face. */
    r = crect_offset(&L.head, o.x, o.y);
    gfx_fill_rect(s, &r, p->face);
    rv_score_line(g, line, sizeof line);
    {
        CRect tr = r; tr.x0 += 8;
        gfx_draw_text_rect(s, GFX_FONT_BOLD, &tr, line, p->text,
                           GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
    }

    /* The felt, with a faint vertical wash so it is not a flat slab. */
    r = crect_offset(&L.board, o.x, o.y);
    gfx_vgradient(s, &r, RV_FELT_HI, RV_FELT);
    for (rr = 0; rr <= REV_N; rr++) {
        gfx_hline(s, r.x0, r.y0 + rr * RV_CELL, REV_N * RV_CELL, RV_GRID);
        gfx_vline(s, r.x0 + rr * RV_CELL, r.y0, REV_N * RV_CELL, RV_GRID);
    }

    for (rr = 0; rr < REV_N; rr++) {
        for (cc = 0; cc < REV_N; cc++) {
            CRect cell = rv_cell_rect(&L, rr, cc);
            cell = crect_offset(&cell, o.x, o.y);
            if ((int)g->b.cell[rr][cc] != REV_EMPTY) {
                rv_disc(s, &cell, (int)g->b.cell[rr][cc]);
            } else if (g->b.turn == REV_BLACK &&
                       rev_legal(&g->b, rr, cc, REV_BLACK)) {
                /* A legal move, marked but not drawn as a disc -- the player
                 * must still be able to tell what is on the board from what
                 * is merely available. */
                int cx = (cell.x0 + cell.x1) / 2;
                int cy = (cell.y0 + cell.y1) / 2;
                gfx_fill_circle(s, cx, cy, 3, RV_FELT_HI);
                gfx_fill_circle(s, cx, cy, 2, GFX_RGB(0x8C, 0xC8, 0xA0));
            }
        }
    }

    /* The cell cursor, so the keyboard has somewhere visible to be. */
    {
        CRect cur = rv_cell_rect(&L, g->ky, g->kx);
        cur = crect_offset(&cur, o.x, o.y);
        cur = crect_inset(&cur, 1);
        gfx_focus_rect(s, &cur, GFX_RGB(0xFF, 0xFF, 0x80));
    }

    r = crect_offset(&L.status, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, p->face);
    {
        CRect tr = r; tr.x0 += 6;
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &tr, g->status, p->text,
                           GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
    }
}

/*
 * After any move: let White reply, announce a pass, and stop at the end.
 *
 * White plays on the next frame rather than inside the click, so the player
 * sees their own disc land before the answer arrives. A reply computed and
 * drawn in the same frame looks like the board changed by itself.
 */
static void rv_after(Reversi *g, WmWindow *win)
{
    if (rev_game_over(&g->b)) {
        char line[80];
        rv_score_line(g, line, sizeof line);
        rv_set_status(g, line);
        g->think = 0;
        wm_set_animated(win, CFALSE);
    } else if (g->b.turn == REV_WHITE) {
        /*
         * Ask for timer ticks only while White actually owes a move, and
         * give them back immediately afterwards. A window marked animated is
         * on the tick list every frame for the rest of its life, and a board
         * that is sitting still waiting for the player has nothing to do
         * with that budget.
         */
        g->think = 3;
        wm_set_animated(win, CTRUE);
        rv_set_status(g, "White is thinking...");
    } else {
        /* Back to the player. If White never got a turn, say so -- silence
         * here is indistinguishable from the program ignoring the move. */
        rv_set_status(g, (g->b.passes > 0)
                      ? "White had no move -- your turn again"
                      : "Your move -- black");
    }
    wm_invalidate(win, NULL);
}

static void rv_player_move(Reversi *g, WmWindow *win, int r, int c)
{
    if (g->b.turn != REV_BLACK || rev_game_over(&g->b)) { return; }
    if (!rev_legal(&g->b, r, c, REV_BLACK)) {
        rv_set_status(g, "Not a legal move -- it must turn something over");
        wm_invalidate(win, NULL);
        return;
    }
    (void)rev_play(&g->b, r, c);
    snd_click();
    rv_after(g, win);
}

static void rv_on_click(WmWindow *win, int px, int py)
{
    Reversi *g = (Reversi *)wm_user(win);
    RvLayout L;
    int r, c;
    if (g == NULL) { return; }
    rv_layout(win, &L);
    if (!crect_contains(&L.board, px, py)) { return; }
    r = (py - L.board.y0) / RV_CELL;
    c = (px - L.board.x0) / RV_CELL;
    if (r < 0 || r >= REV_N || c < 0 || c >= REV_N) { return; }
    g->ky = r; g->kx = c;
    rv_player_move(g, win, r, c);
}

static void rv_on_key(WmWindow *win, int key, int ch)
{
    Reversi *g = (Reversi *)wm_user(win);
    if (g == NULL) { return; }
    if (key == PLAT_KEY_LEFT)       { if (g->kx > 0)          { g->kx--; } }
    else if (key == PLAT_KEY_RIGHT) { if (g->kx < REV_N - 1)  { g->kx++; } }
    else if (key == PLAT_KEY_UP)    { if (g->ky > 0)          { g->ky--; } }
    else if (key == PLAT_KEY_DOWN)  { if (g->ky < REV_N - 1)  { g->ky++; } }
    else if (key == PLAT_KEY_ENTER || ch == ' ') {
        rv_player_move(g, win, g->ky, g->kx);
        return;
    }
    else if (ch == 'h' || ch == 'H') {
        /*
         * A hint: the move the program would play in your place. Reversi is
         * hard to read even with the legal moves marked -- knowing WHICH of
         * six squares is worth having is the whole skill -- so the same
         * positional judgement White uses is offered to the player rather
         * than kept for the opponent.
         */
        int hr = 0, hc = 0;
        if (g->b.turn == REV_BLACK && !rev_game_over(&g->b) &&
            rev_ai_move(&g->b, REV_BLACK, &hr, &hc)) {
            g->ky = hr; g->kx = hc;
            sys_snprintf(g->status, sizeof g->status,
                         "Hint: row %d, column %d -- turns %d over",
                         hr + 1, hc + 1,
                         rev_would_flip(&g->b, hr, hc, REV_BLACK));
        } else {
            rv_set_status(g, "No hint -- nothing to play");
        }
    }
    else if (ch == 'n' || ch == 'N' || key == PLAT_KEY_F2) {
        rv_new_game(g);
    }
    else { return; }
    wm_invalidate(win, NULL);
}

/* White's reply, one move per timer tick. */
static void rv_tick(WmWindow *win)
{
    Reversi *g = (Reversi *)wm_user(win);
    int r = 0, c = 0;
    if (g == NULL || g->think <= 0) { return; }
    if (--g->think > 0) { return; }        /* still thinking */
    wm_set_animated(win, CFALSE);
    if (rev_game_over(&g->b) || g->b.turn != REV_WHITE) {
        rv_after(g, win);
        return;
    }
    if (rev_ai_move(&g->b, REV_WHITE, &r, &c)) {
        (void)rev_play(&g->b, r, c);
        snd_click();
    }
    rv_after(g, win);
}

static cbool rv_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    switch (msg) {
    case WM_MSG_PAINT:       rv_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: rv_on_click(win, (int)a, (int)b);   return CTRUE;
    case WM_MSG_KEYDOWN:     rv_on_key(win, (int)a, (int)b);     return CTRUE;
    case WM_MSG_TIMER:       rv_tick(win);                       return CTRUE;
    case WM_MSG_DESTROY: {
        Reversi *g = (Reversi *)wm_user(win);
        if (g != NULL) { sys_free(g, (cu32)sizeof(Reversi)); }
        return CTRUE;
    }
    default: return CFALSE;
    }
}

void app_reversi_open(void)
{
    Reversi *g;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int fw = REV_N * RV_CELL + RV_MARGIN * 2 + 8;
    int fh = REV_N * RV_CELL + RV_HEAD_H + RV_STATUS + 30;
    int fx, fy;

    g = (Reversi *)sys_calloc(1, (cu32)sizeof(Reversi));
    if (g == NULL) { SYS_LOGE("app", "reversi: OOM"); return; }
    rv_new_game(g);

    plat_video_info(&vi);
    fx = (vi.width - fw) / 2 + 30;
    fy = (vi.height - fh) / 2 - 20;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, fw, fh);
    w = wm_create("Reversi", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_BORDER,
                  rv_proc, g);
    if (w == NULL) { sys_free(g, (cu32)sizeof(Reversi)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
}

/* ---- accessors for --reversi-demo -------------------------------------- */
/*
 * Conservation and legality are the two properties a screenshot cannot check:
 * a board that has lost a disc, or accepted an illegal move, is still a
 * perfectly ordinary-looking board of black and white discs.
 */
int app_reversi_discs(struct WmWindow *win, int who)
{
    Reversi *g = (win != NULL) ? (Reversi *)wm_user(win) : NULL;
    int bl = 0, wh = 0;
    if (g == NULL) { return -1; }
    rev_score(&g->b, &bl, &wh);
    if (who == REV_BLACK) { return bl; }
    if (who == REV_WHITE) { return wh; }
    return bl + wh;
}

cbool app_reversi_cell_rect(struct WmWindow *win, int r, int c, CRect *out)
{
    RvLayout L;
    if (win == NULL || out == NULL || wm_user(win) == NULL) { return CFALSE; }
    if (r < 0 || r >= REV_N || c < 0 || c >= REV_N) { return CFALSE; }
    rv_layout(win, &L);
    *out = rv_cell_rect(&L, r, c);
    return CTRUE;
}

int app_reversi_turn(struct WmWindow *win)
{
    Reversi *g = (win != NULL) ? (Reversi *)wm_user(win) : NULL;
    return (g != NULL) ? g->b.turn : -1;
}

/*
 * The move the program would play for whoever is to move. This is the Hint
 * key's answer, exposed so --reversi-demo can play a WHOLE GAME through the
 * real window rather than against the core directly.
 *
 * A full game is the only way to check the thing rev_core.h warns about: a
 * position where neither side can move is over with empty squares left, and a
 * program that waits for a full board sits on a finished game forever. That
 * is a hang, and a hang is exactly what a scene that only plays two moves
 * cannot see.
 */
cbool app_reversi_hint(struct WmWindow *win, int *out_r, int *out_c)
{
    Reversi *g = (win != NULL) ? (Reversi *)wm_user(win) : NULL;
    if (g == NULL || rev_game_over(&g->b)) { return CFALSE; }
    return rev_ai_move(&g->b, g->b.turn, out_r, out_c);
}

cbool app_reversi_over(struct WmWindow *win)
{
    Reversi *g = (win != NULL) ? (Reversi *)wm_user(win) : NULL;
    return (g == NULL) ? CTRUE : rev_game_over(&g->b);
}

/*
 * The cell cursor, and the status line -- so --reversi-demo can press H and
 * see whether the KEY did anything.
 *
 * The accessor above answers what the hint would be; that is not the same
 * question as whether the keystroke works, and confusing the two is how a
 * documented key comes to do nothing. Help now tells the reader H offers a
 * move, so H has to be driven the way a reader will drive it.
 */
void app_reversi_cursor(struct WmWindow *win, int *out_r, int *out_c)
{
    Reversi *g = (win != NULL) ? (Reversi *)wm_user(win) : NULL;
    if (out_r) { *out_r = (g != NULL) ? g->ky : -1; }
    if (out_c) { *out_c = (g != NULL) ? g->kx : -1; }
}

const char *app_reversi_status(struct WmWindow *win)
{
    Reversi *g = (win != NULL) ? (Reversi *)wm_user(win) : NULL;
    return (g != NULL) ? g->status : "";
}
