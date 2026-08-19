/*
 * app_freecell.c - CastaliaOS FreeCell.
 *
 * The other patience game the era shipped, and a better one to have than a
 * second Klondike: every card is face up from the deal, so nothing is hidden
 * and nothing is luck. Whether you win is whether you saw the move.
 *
 * The rules are in fc_core.c and the card artwork is in card_draw.c, which
 * Solitaire draws from too -- this window is the table and the pointer, and
 * nothing else. That split is deliberate: the interesting rule here is how
 * many cards may move at once, it is arithmetic over two small integers, and
 * a board with it wrong looks exactly like a board with it right.
 *
 * Interaction is click-to-pick, click-to-place, which is what works on a
 * single-button-friendly desktop and needs no drag state:
 *
 *   - click a card in a cascade: picks it and everything below it, if that is
 *     a valid run AND short enough to actually move;
 *   - click a free cell or a foundation: picks its top card;
 *   - click a destination: places, if the rules allow;
 *   - click the same place twice: sends the card home if it can go.
 */
#include "apps.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include "fc_core.h"
#include "card_draw.h"

#define FCW_CARD_W   58
#define FCW_CARD_H   76
#define FCW_GAP      6
#define FCW_MARGIN   10
#define FCW_TOOLBAR_H 26
#define FCW_TOP_Y    (FCW_TOOLBAR_H + 10)
#define FCW_CASC_Y   (FCW_TOP_Y + FCW_CARD_H + 16)
#define FCW_FAN      18          /* vertical step of a fanned cascade      */
#define FCW_BOARD_W  (FC_CASCADES * FCW_CARD_W + (FC_CASCADES - 1) * FCW_GAP)

enum { FH_NONE = 0, FH_CELL, FH_FOUND, FH_CASC };

typedef struct {
    Card  casc[FC_CASCADES][FC_MAX_COL];
    int   casc_count[FC_CASCADES];
    Card  cell[FC_CELLS];
    cbool cell_used[FC_CELLS];
    Card  found[FC_FOUND][13];
    int   found_count[FC_FOUND];

    cu32  seed;
    int   moves;
    cbool won;

    int   sel_kind;          /* FH_* -- what is picked up, if anything    */
    int   sel_col;           /* cell / foundation / cascade index          */
    int   sel_card;          /* first card of a cascade run                */
    char  status[64];
    UiHot hot;               /* New Game / Send Home under the pointer     */
} FreeCell;

/* ---- geometry ---------------------------------------------------------- */
static int fcw_ox(WmWindow *win)
{
    CRect c = wm_client_rect(win);
    int x = (crect_w(&c) - FCW_BOARD_W) / 2;
    return (x > 0) ? x : FCW_MARGIN;
}

static CRect fcw_cell_rect(WmWindow *win, int i)
{
    return crect_make(fcw_ox(win) + i * (FCW_CARD_W + FCW_GAP), FCW_TOP_Y,
                      FCW_CARD_W, FCW_CARD_H);
}

static CRect fcw_found_rect(WmWindow *win, int i)
{
    return crect_make(fcw_ox(win) + (4 + i) * (FCW_CARD_W + FCW_GAP),
                      FCW_TOP_Y, FCW_CARD_W, FCW_CARD_H);
}

static CRect fcw_casc_rect(WmWindow *win, int col, int card)
{
    return crect_make(fcw_ox(win) + col * (FCW_CARD_W + FCW_GAP),
                      FCW_CASC_Y + card * FCW_FAN, FCW_CARD_W, FCW_CARD_H);
}

static CRect fcw_btn_new(WmWindow *win)
{
    CASTALIA_UNUSED(win);
    return crect_make(FCW_MARGIN, 4, 78, 18);
}

static CRect fcw_btn_home(WmWindow *win)
{
    CASTALIA_UNUSED(win);
    return crect_make(FCW_MARGIN + 84, 4, 78, 18);
}

/* ---- board state ------------------------------------------------------- */
static int fcw_free_cells(const FreeCell *g)
{
    int i, n = 0;
    for (i = 0; i < FC_CELLS; i++) { if (!g->cell_used[i]) { n++; } }
    return n;
}

static int fcw_empty_cols(const FreeCell *g)
{
    int i, n = 0;
    for (i = 0; i < FC_CASCADES; i++) { if (g->casc_count[i] == 0) { n++; } }
    return n;
}

static const Card *fcw_casc_top(const FreeCell *g, int col)
{
    if (g->casc_count[col] <= 0) { return NULL; }
    return &g->casc[col][g->casc_count[col] - 1];
}

static const Card *fcw_found_top(const FreeCell *g, int f)
{
    if (g->found_count[f] <= 0) { return NULL; }
    return &g->found[f][g->found_count[f] - 1];
}

/*
 * A fixed deal for --freecell-demo, 0 for the clock-seeded shuffle players
 * get. The scene's "Send Home puts an ace up" check was DEAL-DEPENDENT and
 * therefore flaky: two runs in six failed simply because that shuffle exposed
 * no ace, which looks exactly like a broken Send Home. A check that fails at
 * random is worse than no check -- it teaches the reader to ignore a red
 * result, which is the one habit this harness cannot afford.
 */
static cu32 g_fcw_seed = 0u;

void app_freecell_set_seed(cu32 seed) { g_fcw_seed = seed; }

static void fcw_new_game(FreeCell *g)
{
    int i;
    fc_deal(g->casc, g->casc_count, &g->seed);
    for (i = 0; i < FC_CELLS; i++) { g->cell_used[i] = CFALSE; }
    for (i = 0; i < FC_FOUND; i++) { g->found_count[i] = 0; }
    g->moves = 0;
    g->won = CFALSE;
    g->sel_kind = FH_NONE;
    sys_strlcpy(g->status, "Click a card, then where it should go",
                sizeof g->status);
}

/* Send 'c' home if a foundation will take it. */
static cbool fcw_to_foundation(FreeCell *g, const Card *c)
{
    int f;
    for (f = 0; f < FC_FOUND; f++) {
        if (sol_can_stack_found_rule(fcw_found_top(g, f), c)) {
            g->found[f][g->found_count[f]] = *c;
            g->found_count[f]++;
            return CTRUE;
        }
    }
    return CFALSE;
}

/*
 * Send every card that can go home, over and over until a pass finds none.
 * The loop is bounded by the deck rather than by "until nothing moves" alone,
 * because a rule bug that made a card acceptable to its own foundation twice
 * would otherwise spin forever with the window frozen -- a hang is a worse
 * way to learn about it than a stopped board.
 */
static int fcw_send_home(FreeCell *g)
{
    int moved = 0, pass;
    for (pass = 0; pass < SOL_DECK; pass++) {
        int did = 0;
        int i;
        for (i = 0; i < FC_CELLS; i++) {
            if (g->cell_used[i] && fcw_to_foundation(g, &g->cell[i])) {
                g->cell_used[i] = CFALSE; did++; moved++;
            }
        }
        for (i = 0; i < FC_CASCADES; i++) {
            const Card *top = fcw_casc_top(g, i);
            if (top != NULL && fcw_to_foundation(g, top)) {
                g->casc_count[i]--; did++; moved++;
            }
        }
        if (did == 0) { break; }
    }
    return moved;
}

/* ---- picking up and putting down --------------------------------------- */
static void fcw_clear_sel(FreeCell *g) { g->sel_kind = FH_NONE; }

/* How many cards the selection is carrying. */
static int fcw_sel_len(const FreeCell *g)
{
    if (g->sel_kind == FH_CASC) {
        return g->casc_count[g->sel_col] - g->sel_card;
    }
    return (g->sel_kind == FH_NONE) ? 0 : 1;
}

static void fcw_pick(FreeCell *g, int kind, int col, int card)
{
    int n;
    if (kind == FH_CASC) {
        if (g->casc_count[col] <= 0) { fcw_clear_sel(g); return; }
        n = g->casc_count[col] - card;
        if (!fc_run_valid(&g->casc[col][card], n)) {
            sys_strlcpy(g->status, "Those do not run in sequence",
                        sizeof g->status);
            fcw_clear_sel(g);
            return;
        }
        /*
         * Refused HERE rather than at the destination, so the message names
         * the real reason. A run can be perfectly legal and still be more
         * cards than the free cells and empty columns can shuffle.
         */
        if (n > fc_max_move(fcw_free_cells(g), fcw_empty_cols(g), CFALSE)) {
            sys_snprintf(g->status, sizeof g->status,
                         "Not enough free cells to move %d cards", n);
            fcw_clear_sel(g);
            return;
        }
    } else if (kind == FH_CELL && !g->cell_used[col]) {
        fcw_clear_sel(g); return;
    } else if (kind == FH_FOUND && g->found_count[col] <= 0) {
        fcw_clear_sel(g); return;
    }
    g->sel_kind = kind;
    g->sel_col = col;
    g->sel_card = card;
}

/* The cards the selection is carrying, into 'out'. */
static const Card *fcw_sel_cards(FreeCell *g, int *n)
{
    switch (g->sel_kind) {
    case FH_CASC:  *n = fcw_sel_len(g); return &g->casc[g->sel_col][g->sel_card];
    case FH_CELL:  *n = 1; return &g->cell[g->sel_col];
    case FH_FOUND: *n = 1;
                   return &g->found[g->sel_col][g->found_count[g->sel_col] - 1];
    default:       *n = 0; return NULL;
    }
}

static void fcw_remove_sel(FreeCell *g)
{
    switch (g->sel_kind) {
    case FH_CASC:  g->casc_count[g->sel_col] = g->sel_card; break;
    case FH_CELL:  g->cell_used[g->sel_col] = CFALSE;       break;
    case FH_FOUND: g->found_count[g->sel_col]--;            break;
    default: break;
    }
}

static cbool fcw_place(FreeCell *g, int kind, int col)
{
    int n = 0, i;
    const Card *src = fcw_sel_cards(g, &n);
    if (src == NULL || n <= 0) { return CFALSE; }

    if (kind == FH_CELL) {
        if (n != 1 || g->cell_used[col]) { return CFALSE; }
        g->cell[col] = src[0];
        g->cell_used[col] = CTRUE;
    } else if (kind == FH_FOUND) {
        if (n != 1) { return CFALSE; }
        if (!sol_can_stack_found_rule(fcw_found_top(g, col), &src[0])) {
            return CFALSE;
        }
        g->found[col][g->found_count[col]] = src[0];
        g->found_count[col]++;
    } else if (kind == FH_CASC) {
        const Card *top = fcw_casc_top(g, col);
        if (g->sel_kind == FH_CASC && g->sel_col == col) { return CFALSE; }
        if (!fc_can_stack_cascade(top, &src[0])) { return CFALSE; }
        /* Moving INTO an empty column is weaker: that column cannot also be
         * used as the scratch space the move needs. */
        if (n > fc_max_move(fcw_free_cells(g), fcw_empty_cols(g),
                            (top == NULL) ? CTRUE : CFALSE)) {
            sys_snprintf(g->status, sizeof g->status,
                         "Not enough room to move %d cards there", n);
            return CFALSE;
        }
        if (g->casc_count[col] + n > FC_MAX_COL) { return CFALSE; }
        for (i = 0; i < n; i++) {
            g->casc[col][g->casc_count[col] + i] = src[i];
        }
        g->casc_count[col] += n;
    } else {
        return CFALSE;
    }
    fcw_remove_sel(g);
    g->moves++;
    fcw_clear_sel(g);
    g->won = fc_is_won(g->found_count);
    if (g->won) {
        sys_snprintf(g->status, sizeof g->status,
                     "Solved in %d moves", g->moves);
    } else {
        sys_snprintf(g->status, sizeof g->status, "%d moves", g->moves);
    }
    return CTRUE;
}

/* ---- hit testing -------------------------------------------------------- */
static int fcw_hit(WmWindow *win, FreeCell *g, int x, int y,
                   int *out_col, int *out_card)
{
    int i, j;
    for (i = 0; i < FC_CELLS; i++) {
        CRect r = fcw_cell_rect(win, i);
        if (crect_contains(&r, x, y)) { *out_col = i; *out_card = 0; return FH_CELL; }
    }
    for (i = 0; i < FC_FOUND; i++) {
        CRect r = fcw_found_rect(win, i);
        if (crect_contains(&r, x, y)) { *out_col = i; *out_card = 0; return FH_FOUND; }
    }
    for (i = 0; i < FC_CASCADES; i++) {
        int n = g->casc_count[i];
        if (n == 0) {
            CRect r = fcw_casc_rect(win, i, 0);
            if (crect_contains(&r, x, y)) {
                *out_col = i; *out_card = 0; return FH_CASC;
            }
            continue;
        }
        /* Topmost first: the fanned cards overlap, and the one drawn last is
         * the one the pointer is actually on. */
        for (j = n - 1; j >= 0; j--) {
            CRect r = fcw_casc_rect(win, i, j);
            if (j < n - 1) { r.y1 = r.y0 + FCW_FAN; }   /* only the visible strip */
            if (crect_contains(&r, x, y)) {
                *out_col = i; *out_card = j; return FH_CASC;
            }
        }
    }
    return FH_NONE;
}

static void fcw_click(WmWindow *win, FreeCell *g, int x, int y)
{
    int col = 0, card = 0;
    int kind;
    CRect b;

    b = fcw_btn_new(win);
    if (crect_contains(&b, x, y)) {
        (void)ui_hot_press(&g->hot, 0);
        fcw_new_game(g);
        wm_invalidate(win, NULL);
        return;
    }
    b = fcw_btn_home(win);
    if (crect_contains(&b, x, y)) {
        int n;
        (void)ui_hot_press(&g->hot, 1);
        n = fcw_send_home(g);
        g->moves += n;
        g->won = fc_is_won(g->found_count);
        if (n == 0) {
            sys_strlcpy(g->status, "Nothing can go home yet",
                        sizeof g->status);
        } else if (g->won) {
            sys_snprintf(g->status, sizeof g->status,
                         "Solved in %d moves", g->moves);
        } else {
            sys_snprintf(g->status, sizeof g->status,
                         "Sent %d home -- %d moves", n, g->moves);
        }
        fcw_clear_sel(g);
        wm_invalidate(win, NULL);
        return;
    }

    kind = fcw_hit(win, g, x, y, &col, &card);
    if (kind == FH_NONE) { fcw_clear_sel(g); wm_invalidate(win, NULL); return; }

    if (g->sel_kind == FH_NONE) {
        fcw_pick(g, kind, col, card);
        wm_invalidate(win, NULL);
        return;
    }
    /* Clicking the picked card again sends it home if it can go. */
    if (g->sel_kind == kind && g->sel_col == col &&
        (kind != FH_CASC || g->sel_card == card)) {
        int n = 0;
        const Card *src = fcw_sel_cards(g, &n);
        if (n == 1 && src != NULL && fcw_to_foundation(g, &src[0])) {
            fcw_remove_sel(g);
            g->moves++;
            g->won = fc_is_won(g->found_count);
            sys_snprintf(g->status, sizeof g->status,
                         g->won ? "Solved in %d moves" : "%d moves", g->moves);
        }
        fcw_clear_sel(g);
        wm_invalidate(win, NULL);
        return;
    }
    if (!fcw_place(g, kind, col)) {
        /* A refused move drops the selection rather than leaving it armed --
         * an invisible held card is how a click seems to do nothing. */
        fcw_clear_sel(g);
    }
    wm_invalidate(win, NULL);
}

/* ---- painting ----------------------------------------------------------- */
static void fcw_empty_slot(GfxSurface *s, const CRect *r, CColor felt)
{
    /* A recessed slot: darker than the felt, with a lighter inner edge so it
     * reads as a hole rather than a smudge. */
    CRect in;
    gfx_fill_round_rect(s, r, 5, gfx_tint(felt, 0x000000, 60));
    in = crect_inset(r, 2);
    gfx_fill_round_rect(s, &in, 4, gfx_tint(felt, 0x000000, 30));
}

static void fcw_paint(WmWindow *win, GfxSurface *s)
{
    FreeCell *g = (FreeCell *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    CRect c = wm_client_rect(win);
    CColor felt = GFX_RGB(0x1B, 0x6E, 0x3C);
    CRect r;
    char buf[128];   /* status + counters; 64 cut the last number off */
    int i, j;

    if (g == NULL) { return; }
    /*
     * wm_client_rect already returns SCREEN coordinates -- it is derived from
     * win->frame. Offsetting it again by the client origin pushes the felt
     * down and to the right by the window's own position, which leaves the
     * top-left corner of the table showing the desktop face colour. The
     * per-card rectangles below DO need the offset, because those come from
     * the layout helpers and are client-relative.
     */
    gfx_fill_rect(s, &c, felt);

    {
        CRect bar = crect_make(c.x0, c.y0, crect_w(&c), FCW_TOOLBAR_H);
        CRect bt;
        gfx_fill_rect(s, &bar, gfx_tint(felt, 0x000000, 70));
        bt = fcw_btn_new(win);  bt = crect_offset(&bt, o.x, o.y);
        ui_draw_button(s, &bt, "New Game",
                       ui_hot_state(&g->hot, 0, UI_BTN_NORMAL));
        bt = fcw_btn_home(win); bt = crect_offset(&bt, o.x, o.y);
        ui_draw_button(s, &bt, "Send Home",
                       ui_hot_state(&g->hot, 1, UI_BTN_NORMAL));
    }

    for (i = 0; i < FC_CELLS; i++) {
        { CRect t = fcw_cell_rect(win, i); r = crect_offset(&t, o.x, o.y); }
        if (g->cell_used[i]) {
            card_draw_face(s, &r, &g->cell[i],
                           (g->sel_kind == FH_CELL && g->sel_col == i));
        } else {
            fcw_empty_slot(s, &r, felt);
        }
    }
    for (i = 0; i < FC_FOUND; i++) {
        const Card *top = fcw_found_top(g, i);
        { CRect t = fcw_found_rect(win, i); r = crect_offset(&t, o.x, o.y); }
        if (top != NULL) {
            card_draw_face(s, &r, top,
                           (g->sel_kind == FH_FOUND && g->sel_col == i));
        } else {
            /*
             * An empty foundation carries a faint pip of its suit, which is
             * the only thing distinguishing the four foundations from the
             * four free cells beside them -- eight identical holes in a row
             * tell the player nothing about which half does what.
             */
            fcw_empty_slot(s, &r, felt);
            card_pip(s, i, (r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2, 10,
                     gfx_tint(felt, 0xFFFFFF, 46));
        }
    }
    for (i = 0; i < FC_CASCADES; i++) {
        if (g->casc_count[i] == 0) {
            { CRect t = fcw_casc_rect(win, i, 0); r = crect_offset(&t, o.x, o.y); }
            fcw_empty_slot(s, &r, felt);
            continue;
        }
        for (j = 0; j < g->casc_count[i]; j++) {
            cbool sel = (g->sel_kind == FH_CASC && g->sel_col == i &&
                         j >= g->sel_card) ? CTRUE : CFALSE;
            { CRect t = fcw_casc_rect(win, i, j); r = crect_offset(&t, o.x, o.y); }
            card_draw_face(s, &r, &g->casc[i][j], sel);
        }
    }

    /* Status strip along the foot. */
    r = crect_make(c.x0, c.y1 - 18, crect_w(&c), 18);
    gfx_fill_rect(s, &r, gfx_tint(felt, 0x000000, 90));
    sys_snprintf(buf, sizeof buf, "%s  -  %d free, %d empty, move %d",
                 g->status, fcw_free_cells(g), fcw_empty_cols(g),
                 fc_max_move(fcw_free_cells(g), fcw_empty_cols(g), CFALSE));
    /* Fitted, because the status text is assembled from a message that varies
     * and a window that can be narrower than it. */
    {
        char shown[80];
        gfx_text_fit(shown, sizeof shown, buf, crect_w(&c) - 16,
                     GFX_FONT_SYSTEM);
        gfx_draw_text(s, GFX_FONT_SYSTEM, c.x0 + 8, r.y0 + 5, shown,
                      GFX_RGB(0xE0, 0xF0, 0xE4));
    }
    CASTALIA_UNUSED(p);
}

/* ---- window ------------------------------------------------------------- */
static cbool fc_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    FreeCell *g = (FreeCell *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:
        fcw_paint(win, (GfxSurface *)param);
        return CTRUE;
    case WM_MSG_LBUTTONDOWN:
        /* a and b are already CLIENT coordinates -- see wm.h. Subtracting the
         * client origin here would offset every hit test by the window's
         * position on screen, which reads as "clicks land on the wrong card
         * unless the window happens to be at 0,0". */
        fcw_click(win, g, (int)a, (int)b);
        return CTRUE;
    case WM_MSG_MOUSEMOVE: {
        CRect bn = fcw_btn_new(win), bh = fcw_btn_home(win);
        int hit = crect_contains(&bn, (int)a, (int)b) ? 0
                : crect_contains(&bh, (int)a, (int)b) ? 1 : -1;
        if (g == NULL) { return CFALSE; }
        if (ui_hot_move(&g->hot, hit)) {
            CRect br[2];
            br[0] = bn; br[1] = bh;
            ui_hot_repaint(win, &g->hot, br, 2);
        }
        return CTRUE;
    }
    case WM_MSG_LBUTTONUP:
    case WM_MSG_MOUSELEAVE:
        if (g != NULL) {
            cbool redraw = ui_hot_release(&g->hot);
            CRect br[2];
            if (msg == WM_MSG_MOUSELEAVE && ui_hot_move(&g->hot, -1)) {
                redraw = CTRUE;
            }
            br[0] = fcw_btn_new(win); br[1] = fcw_btn_home(win);
            if (redraw) { ui_hot_repaint(win, &g->hot, br, 2); }
        }
        return CTRUE;
    case WM_MSG_KEYDOWN:
        if ((int)a == PLAT_KEY_F2 || (int)a == 'n' || (int)a == 'N') {
            fcw_new_game(g);
            wm_invalidate(win, NULL);
            return CTRUE;
        }
        return CFALSE;
    case WM_MSG_DESTROY:
        sys_free(g, (cu32)sizeof(FreeCell));
        return CTRUE;
    default:
        return CFALSE;
    }
}

void app_freecell_open(void)
{
    FreeCell *g;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw = FCW_BOARD_W + 2 * FCW_MARGIN, ch = 526, fx, fy;

    g = (FreeCell *)sys_calloc(1, (cu32)sizeof(FreeCell));
    if (g == NULL) { SYS_LOGE("app", "freecell: OOM"); return; }
    g->seed = (g_fcw_seed != 0u) ? g_fcw_seed
                                 : (sys_now_ms() * 2654435761u + 7u);
    fcw_new_game(g);

    plat_video_info(&vi);
    if (cw > vi.width - 20) { cw = vi.width - 20; }
    if (ch > vi.height - 60) { ch = vi.height - 60; }
    fx = (vi.width - cw) / 2;
    fy = (vi.height - ch) / 2 - 10;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw, ch);

    w = wm_create("FreeCell", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_BORDER, fc_proc, g);
    if (w == NULL) { sys_free(g, (cu32)sizeof(FreeCell)); return; }
    wm_show(w, CTRUE);
    wm_focus(w);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened FreeCell");
}

/* ---- for the demo scene -------------------------------------------------- */
int app_freecell_moves(WmWindow *win)
{
    FreeCell *g = (FreeCell *)wm_user(win);
    return (g != NULL) ? g->moves : -1;
}

/*
 * Where a thing is on the table, so a scene can click it rather than
 * recompute the layout -- the mistake --net-ping-demo made, where a copied
 * constant went stale the moment the layout changed.
 * kind: 0 = free cell 'i', 1 = foundation 'i', 2 = the TOP card of cascade 'i'.
 */
/* kind 3 = the New Game button, 4 = Send Home. */
cbool app_freecell_rect(WmWindow *win, int kind, int i, CRect *out)
{
    FreeCell *g = (FreeCell *)wm_user(win);
    if (g == NULL || out == NULL) { return CFALSE; }
    if (kind == 0 && i >= 0 && i < FC_CELLS) {
        *out = fcw_cell_rect(win, i); return CTRUE;
    }
    if (kind == 1 && i >= 0 && i < FC_FOUND) {
        *out = fcw_found_rect(win, i); return CTRUE;
    }
    if (kind == 2 && i >= 0 && i < FC_CASCADES && g->casc_count[i] > 0) {
        *out = fcw_casc_rect(win, i, g->casc_count[i] - 1); return CTRUE;
    }
    if (kind == 3) { *out = fcw_btn_new(win);  return CTRUE; }
    if (kind == 4) { *out = fcw_btn_home(win); return CTRUE; }
    return CFALSE;
}

int app_freecell_found_count(WmWindow *win)
{
    FreeCell *g = (FreeCell *)wm_user(win);
    int i, n = 0;
    if (g == NULL) { return -1; }
    for (i = 0; i < FC_FOUND; i++) { n += g->found_count[i]; }
    return n;
}

int app_freecell_cards_on_table(WmWindow *win)
{
    FreeCell *g = (FreeCell *)wm_user(win);
    int i, n = 0;
    if (g == NULL) { return -1; }
    for (i = 0; i < FC_CASCADES; i++) { n += g->casc_count[i]; }
    for (i = 0; i < FC_CELLS; i++) { if (g->cell_used[i]) { n++; } }
    for (i = 0; i < FC_FOUND; i++) { n += g->found_count[i]; }
    return n;
}
