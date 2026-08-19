/*
 * sh_launcher.c - The Start panel ("Castalia" button menu).
 *
 * An XP-style two-column launcher: the left column lists the programs, the
 * right column lists places & system tools, a glossy header band spans the top,
 * and a green footer band carries the session buttons (Log Off / Shut Down).
 * Built once, drawn above the windows when open, positioned just above the
 * launcher button. The two columns are plain UiMenu item lists; this file owns
 * their layout, drawing, hit testing, and keyboard navigation.
 */
#include "sh_internal.h"
#include "castalia/capp_loader.h"
#include "castalia/gfx.h"
#include "castalia/sys.h"

/* ---- layout metrics --------------------------------------------------- */
#define LR_ROW_H      18   /* item row height (fits a 16px icon)            */
#define LR_SEP_H      6    /* separator row height                          */
#define LR_PAD_V      5    /* top/bottom padding inside a column            */
#define LR_ICON       16   /* gutter icon size (square)                     */
#define LR_TEXT_X     26   /* text inset within a column (icon + gap)       */
#define LR_COL_PAD_R  14   /* right padding after the widest label          */
#define LR_HEADER_H   40   /* glossy banner (title + subtitle)              */
#define LR_FOOTER_H   34   /* session-button band                           */
#define LR_LEFT_MINW  162  /* minimum left (programs) column width          */
#define LR_RIGHT_MINW 150  /* minimum right (places) column width           */

/* Resolved geometry of the open panel, recomputed each paint / hit test. */
typedef struct {
    int x, y, w, h;   /* whole panel                                     */
    int lw, rw;       /* left / right column widths                      */
    int col_y, col_h; /* the column band (between header and footer)     */
} LRGeo;

/* Add a launcher item with an optional pack icon (by name; NULL/absent -> no
 * gutter icon, so the menu still works without an icon pack). */
static void add_item(UiMenu *m, int cmd, const char *label, const char *icon)
{
    ui_menu_add(m, cmd, label, CTRUE);
    if (icon != NULL) { ui_menu_set_last_icon(m, sh_iconpack_icon(icon)); }
}

/*
 * How much room is left. The taller column decides the panel's height, and at
 * 640x480 -- the smallest mode this supports -- it has space for TWO more rows
 * than it carries today. A third makes the layout clamp, and clamped rows are
 * not drawn and not clickable.
 *
 * Measured rather than derived: --launch-demo opens the menu at 640x480 and
 * fails if the clamp trips, and two filler items pass while three do not. So
 * adding a third breaks the build instead of quietly making the last entry
 * unreachable -- which is the failure that hid FreeCell and Reversi, arrived
 * at from the other direction.
 *
 * Past that the right answer is a scrolling column, not a smaller font.
 */
void sh_launcher_build(void)
{
    UiMenu *left  = &g_sh.launcher_menu;    /* programs                    */
    UiMenu *right = &g_sh.launcher_menu2;   /* places & system            */
    int addons, i;

    ui_menu_clear(left);
    add_item(left, SH_CMD_NOTEPAD,     "Notepad",          "m-notepad");
    add_item(left, SH_CMD_PAINT,       "Paint",            "m-paint");
    add_item(left, SH_CMD_VIEWER,      "Viewer",           "m-viewer");
    add_item(left, SH_CMD_CALCULATOR,  "Calculator",       "m-calc");
    add_item(left, SH_CMD_WRITE,       "CastaliaWrite",    "m-write");
    add_item(left, SH_CMD_SHEET,       "CastaliaSheet",    "m-sheet");
    add_item(left, SH_CMD_MEDIA,       "Media Player",     "m-media");
    add_item(left, SH_CMD_BENCHMARK,   "Benchmark Suite",  "m-bench");
    add_item(left, SH_CMD_CLOCK,       "Clock, Calendar & Agenda", "m-clock");
    add_item(left, SH_CMD_CHARMAP,     "Character Map",    "m-charmap");
    add_item(left, SH_CMD_MINES,       "Mines",            "m-mines");
    add_item(left, SH_CMD_SOLITAIRE,   "Solitaire",        "m-cards");
    add_item(left, SH_CMD_FREECELL,    "FreeCell",         "m-cards");
    add_item(left, SH_CMD_REVERSI,     "Reversi",          "m-cards");
    /* Discovered, runnable .CAPP add-ons appear as first-class programs (one
     * per indexed package) -- extensibility Windows 98 SE lacked. */
    addons = capp_indexed_count();
    if (addons > 0) {
        ui_menu_add_separator(left);
        for (i = 0; i < addons && left->count < UI_MENU_MAX_ITEMS; i++) {
            add_item(left, SH_CMD_ADDON_BASE + i, capp_indexed_name(i), "m-app");
        }
    }

    ui_menu_clear(right);
    add_item(right, SH_CMD_FILEMANAGER,   "File Manager",       "m-fileman");
    add_item(right, SH_CMD_SYSINFO,       "System Information", "m-sysinfo");
    add_item(right, SH_CMD_CONTROLCENTER, "Control Center",     "m-control");
    add_item(right, SH_CMD_TASKMANAGER,   "Task Manager",       "m-taskman");
    add_item(right, SH_CMD_DISKUSAGE,     "Disk Usage",         "m-diskuse");
    add_item(right, SH_CMD_COMPARE,       "File Compare",       "m-compare");
    add_item(right, SH_CMD_LOGVIEWER,     "Log Viewer",         "m-logview");
    add_item(right, SH_CMD_NETWORK,       "Network",            "m-network");
    add_item(right, SH_CMD_THEMEEDITOR,   "Theme Editor",       "m-theme");
    add_item(right, SH_CMD_CAPTURE,       "Capture Screen",     "m-capture");
    ui_menu_add_separator(right);
    add_item(right, SH_CMD_CONSOLE,       "Console",            "m-terminal");
    add_item(right, SH_CMD_RUN,           "Run...",             "m-run");
    add_item(right, SH_CMD_DOSLAUNCHER,   "DOS Program...",     "m-terminal");
    ui_menu_add_separator(right);
    add_item(right, SH_CMD_WELCOME,       "Welcome Tour",       "m-help");
    add_item(right, SH_CMD_HELP,          "Help",               "m-help");
    add_item(right, SH_CMD_ABOUT,         "About CastaliaOS",   "m-help");
    add_item(right, SH_CMD_EXIT_TO_DOS,   "Exit to DOS",        "m-terminal");
}

/* ---- column measurement ---------------------------------------------- */
static int lr_row_height(const UiMenuItem *it)
{
    return (it->id == UI_MENU_SEPARATOR_ID) ? LR_SEP_H : LR_ROW_H;
}

static int lr_col_height(const UiMenu *m)
{
    int i, h = LR_PAD_V * 2;
    for (i = 0; i < m->count; i++) { h += lr_row_height(&m->items[i]); }
    return h;
}

static int lr_col_width(const UiMenu *m, int minw)
{
    int i, maxw = 0;
    for (i = 0; i < m->count; i++) {
        int w = gfx_text_width(GFX_FONT_SYSTEM, m->items[i].label);
        if (w > maxw) { maxw = w; }
    }
    maxw += LR_TEXT_X + LR_COL_PAD_R;
    return (maxw < minw) ? minw : maxw;
}

/* Did the last layout have to drop rows to fit the work area? See the note
 * where it is set. */
static cbool g_lr_clamped = CFALSE;
/* ...and whether we have already said so, see where it is logged. */
static cbool g_lr_said = CFALSE;

cbool sh_launcher_clamped(void) { return g_lr_clamped; }

/* ---- panel geometry --------------------------------------------------- */
static void launcher_geometry(LRGeo *g)
{
    int lh, rh, ch, band;
    g->lw = lr_col_width(&g_sh.launcher_menu, LR_LEFT_MINW);
    g->rw = lr_col_width(&g_sh.launcher_menu2, LR_RIGHT_MINW);
    lh = lr_col_height(&g_sh.launcher_menu);
    rh = lr_col_height(&g_sh.launcher_menu2);
    ch = (lh > rh) ? lh : rh;
    /*
     * A column that will carry a scroll bar is made WIDER by exactly the bar,
     * rather than having the bar take fifteen pixels off its labels.
     *
     * lr_col_width sizes a column to its longest label plus a fixed right
     * padding of fourteen. A fifteen-pixel bar dropped inside that eats the
     * padding and then a pixel of the last glyph, so the longest entry in the
     * menu -- and only that one -- comes out clipped, on exactly the machines
     * where enough add-ons were installed to make the column scroll. There is
     * no circularity in asking first: whether a column overflows depends on
     * its HEIGHT, which does not depend on its width.
     */
    band = g_sh.taskbar_rect.y0 - LR_HEADER_H - LR_FOOTER_H;
    if (band < LR_ROW_H) { band = LR_ROW_H; }
    if (lh > band) { g->lw += UI_SB_W; }
    if (rh > band) { g->rw += UI_SB_W; }
    g->col_h = ch;
    g->w = g->lw + g->rw;
    g->h = LR_HEADER_H + ch + LR_FOOTER_H;
    /* The panel must fit the work area above the taskbar. Enough indexed
     * add-ons can otherwise push the columns -- and the Log Off / Shut Down
     * footer with them -- below the screen, where nothing can be clicked. */
    {
        int avail = g_sh.taskbar_rect.y0;
        int max_col = avail - LR_HEADER_H - LR_FOOTER_H;
        if (max_col < LR_ROW_H) { max_col = LR_ROW_H; }
        g_lr_clamped = CFALSE;
        if (g->col_h <= max_col) { g_lr_said = CFALSE; }
        if (g->col_h > max_col) {
            /*
             * The panel is capped at the work area and the columns SCROLL.
             *
             * Rows past this point used to be neither drawn nor clickable.
             * The log line always said so; nothing ever read it, and a Start
             * menu entry you cannot click is indistinguishable from one that
             * was never added -- the same failure that hid FreeCell and
             * Reversi, arrived at from the other direction.
             *
             * Detecting it was not enough. --launch-demo runs at 640x480, the
             * smallest mode this supports, and still fails if this trips, so
             * adding one item too many to the SHIPPED lists breaks the build
             * rather than the menu. But .CAPP add-ons are discovered at RUN
             * time, one row each, and no build-time check can count them --
             * so on somebody else's machine the overflow has to be survivable
             * rather than merely noticed. --lscroll-demo is that case.
             */
            /* Said ONCE per transition into the state, not once per
             * geometry call -- this runs on every paint and every hit test,
             * and a log that repeats sixty times a second is a log nobody
             * reads, including the harness that greps it. */
            if (!g_lr_said) {
                SYS_LOGW("sh", "launcher taller than the work area (%d > %d "
                         "px): the columns scroll", g->h, avail);
                g_lr_said = CTRUE;
            }
            g_lr_clamped = CTRUE;
            g->col_h = max_col;
            g->h = LR_HEADER_H + g->col_h + LR_FOOTER_H;
        }
    }
    g->x = g_sh.launcher_button.x0;
    g->y = g_sh.taskbar_rect.y0 - g->h;
    if (g->y < 0) { g->y = 0; }
    if (g->x + g->w > g_sh.screen_w) { g->x = g_sh.screen_w - g->w; }
    if (g->x < 0) { g->x = 0; }
    g->col_y = g->y + LR_HEADER_H;
    g_sh.launcher_rect = crect_make(g->x, g->y, g->w, g->h);
}

/* ---- a column, resolved ----------------------------------------------- */
/*
 * Everything about one column that drawing, hit testing and row geometry all
 * need, worked out in ONE place.
 *
 * They used to work it out separately -- three walks down the same items with
 * the same arithmetic spelled three times -- and that was survivable while a
 * column was just "items from the top". A column that can be scrolled has a
 * pixel offset, a narrower content width when it carries a bar, and a band it
 * must not draw outside of; three copies of that is three chances for the
 * highlight to be drawn on a different row from the one the click lands on.
 */
typedef struct {
    int   x, w;        /* the whole column box                             */
    int   content_w;   /* width the rows get (w, less any scroll bar)      */
    int   y, h;        /* the band rows live in                            */
    int   total;       /* content height in pixels                         */
    cbool bar;         /* is it taller than the band                       */
    int   pos;         /* scroll offset in pixels, clamped                 */
    CRect bar_rect;    /* the scroll bar, when there is one                */
} LRCol;

/* Clamp a column's stored scroll offset to what its content allows, and hand
 * back the resolved geometry. 'which': 0 left, 1 right. */
static void lr_column(const LRGeo *g, int which, LRCol *c)
{
    const UiMenu *m = (which == 0) ? &g_sh.launcher_menu : &g_sh.launcher_menu2;
    int max;
    c->x = (which == 0) ? g->x : g->x + g->lw;
    c->w = (which == 0) ? g->lw : g->rw;
    c->y = g->col_y;
    c->h = g->col_h;
    c->total = lr_col_height(m);
    c->bar = (c->total > c->h) ? CTRUE : CFALSE;
    c->content_w = c->bar ? (c->w - UI_SB_W) : c->w;
    if (c->content_w < 1) { c->content_w = 1; }
    max = c->total - c->h;
    if (max < 0) { max = 0; }
    c->pos = g_sh.launcher_scroll[which];
    if (c->pos < 0) { c->pos = 0; }
    if (c->pos > max) { c->pos = max; }
    g_sh.launcher_scroll[which] = c->pos;
    c->bar_rect = c->bar
        ? crect_make(c->x + c->content_w, c->y, UI_SB_W, c->h)
        : crect_make(0, 0, 0, 0);
}

/* Where row 'i' sits, in screen coordinates, scroll included. CFALSE for a
 * separator, an index out of range, or a row scrolled entirely out of the
 * band -- callers use it to decide what to repaint, and a row nobody can see
 * is not a row anybody needs repainted. */
static cbool lr_col_row_rect(const UiMenu *m, const LRCol *c, int i, CRect *out)
{
    int k, cy;
    CRect r, band;
    if (m == NULL || c == NULL || out == NULL || i < 0 || i >= m->count) {
        return CFALSE;
    }
    if (m->items[i].id == UI_MENU_SEPARATOR_ID) { return CFALSE; }
    cy = c->y + LR_PAD_V - c->pos;
    for (k = 0; k < i; k++) { cy += lr_row_height(&m->items[k]); }
    r = crect_make(c->x, cy, c->content_w, lr_row_height(&m->items[i]));
    band = crect_make(c->x, c->y, c->content_w, c->h);
    if (!crect_overlaps(&r, &band)) { return CFALSE; }
    *out = crect_intersect(&r, &band);
    return CTRUE;
}

/* ---- footer session buttons ------------------------------------------ */
/* Fill the two footer button rects (Log Off, Shut Down), right-aligned. */
static void lr_footer_buttons(const LRGeo *g, CRect *logoff, CRect *shut)
{
    int fy = g->y + g->h - LR_FOOTER_H;
    int bh = LR_FOOTER_H - 10;
    int by = fy + 5;
    int sw = LR_ICON + 6 + gfx_text_width(GFX_FONT_SYSTEM, "Shut Down") + 18;
    int lw = LR_ICON + 6 + gfx_text_width(GFX_FONT_SYSTEM, "Log Off") + 18;
    int sx = g->x + g->w - 8 - sw;
    int lx = sx - 6 - lw;
    *shut   = crect_make(sx, by, sw, bh);
    *logoff = crect_make(lx, by, lw, bh);
}

static void launcher_header_paint(GfxSurface *s, const LRGeo *g)
{
    const ShTheme *t = &g_sh.theme;
    CRect band = crect_make(g->x, g->y, g->w, LR_HEADER_H);
    if (sh_glossy()) {
        gfx_vgradient3(s, &band, t->title_active_l,
                       gfx_tint(t->title_active_l, 0xFFFFFF, 90),
                       t->title_active_r, 340);
        gfx_hline(s, g->x, g->y, g->w, gfx_tint(t->title_active_l, 0xFFFFFF, 170));
    } else {
        gfx_fill_rect(s, &band, t->title_active_l);
    }
    gfx_hline(s, g->x, g->y + LR_HEADER_H - 1, g->w,
              gfx_tint(t->title_active_r, 0x000000, 90));
    sh_logo_draw_plated(s, g->x + 5, g->y + 3, LR_HEADER_H - 6);
    gfx_draw_text_shadow(s, GFX_FONT_BOLD, g->x + LR_HEADER_H + 6, g->y + 9,
                         "CastaliaOS 98 PE", GFX_RGB(0xFF, 0xFF, 0xFF),
                         GFX_RGB(0x0A, 0x1E, 0x4E));
    gfx_draw_text(s, GFX_FONT_SYSTEM, g->x + LR_HEADER_H + 6, g->y + 24,
                  "Portable Edition",
                  gfx_tint(t->title_active_l, 0xFFFFFF, 150));
}

/* ---- footer ----------------------------------------------------------- */
/* One session button: an icon + label, highlighted when hovered. */
static void lr_footer_button(GfxSurface *s, const CRect *r, const char *icon,
                             const char *label, cbool hot, cbool glossy)
{
    int iw = crect_w(r), ih = crect_h(r);
    const GfxSurface *ic = sh_iconpack_icon(icon);
    CColor txt   = glossy ? GFX_RGB(0xFF, 0xFF, 0xFF) : g_sh.theme.ui.text;
    CColor shade = glossy ? GFX_RGB(0x30, 0x48, 0x14) : g_sh.theme.ui.light;
    if (hot) {
        if (glossy) {
            CRect in = crect_make(r->x0, r->y0, iw, ih);
            gfx_vgradient3(s, &in, GFX_RGB(0xC6, 0xDE, 0x8C),
                           GFX_RGB(0xB0, 0xCE, 0x70),
                           GFX_RGB(0x8E, 0xB0, 0x50), 300);
            gfx_bevel(s, &in, GFX_BEVEL_RAISED,
                      GFX_RGB(0xE4, 0xF0, 0xC0), GFX_RGB(0x5E, 0x82, 0x2E),
                      GFX_RGB(0xB0, 0xCE, 0x70));
        } else {
            CRect in = crect_make(r->x0, r->y0, iw, ih);
            gfx_bevel(s, &in, GFX_BEVEL_RAISED, g_sh.theme.ui.light,
                      g_sh.theme.ui.dark, g_sh.theme.ui.face);
        }
    }
    if (ic != NULL) {
        CRect src = crect_make(0, 0, LR_ICON, LR_ICON);
        gfx_blit(s, r->x0 + 8, r->y0 + (ih - LR_ICON) / 2, ic, &src,
                 GFX_BLIT_KEYED);
    }
    if (glossy) {
        gfx_draw_text_shadow(s, GFX_FONT_SYSTEM, r->x0 + 8 + LR_ICON + 6,
                             r->y0 + (ih - 8) / 2, label, txt, shade);
    } else {
        gfx_draw_text(s, GFX_FONT_SYSTEM, r->x0 + 8 + LR_ICON + 6,
                      r->y0 + (ih - 8) / 2, label, txt);
    }
}

static void launcher_footer_paint(GfxSurface *s, const LRGeo *g)
{
    cbool glossy = sh_glossy();
    int fy = g->y + g->h - LR_FOOTER_H;
    CRect band = crect_make(g->x, fy, g->w, LR_FOOTER_H);
    CRect logoff, shut;
    if (glossy) {
        gfx_vgradient3(s, &band, GFX_RGB(0x8C, 0xAE, 0x50),
                       GFX_RGB(0x9E, 0xC0, 0x60), GFX_RGB(0x5B, 0x82, 0x2E), 300);
        gfx_hline(s, g->x, fy, g->w, GFX_RGB(0xCA, 0xDE, 0x94));
    } else {
        gfx_fill_rect(s, &band, g_sh.theme.ui.face);
        gfx_hline(s, g->x, fy, g->w, g_sh.theme.ui.light);
    }
    lr_footer_buttons(g, &logoff, &shut);
    lr_footer_button(s, &logoff, "m-restart",  "Log Off",
                     g_sh.launcher_foot_hl == 0, glossy);
    lr_footer_button(s, &shut,   "m-shutdown", "Shut Down",
                     g_sh.launcher_foot_hl == 1, glossy);
}

/* ---- column drawing --------------------------------------------------- */
/* Draw one column's items into [cx, g->col_y .. ] with the given width and
 * background, full-width highlight on the hovered/keyboard-selected row. */
static void lr_draw_column(GfxSurface *s, const UiMenu *m, const LRCol *c,
                           CColor bg, int hot_part)
{
    const UiPalette *p = ui_palette();
    int i, cy, cx = c->x, cw = c->content_w;
    CRect col = crect_make(cx, c->y, cw, c->h);
    CRect prev_clip;
    gfx_fill_rect(s, &col, bg);
    /*
     * Rows are clipped to the band rather than skipped when they cross it.
     * A scrolled column shows PART of a row at each end -- that partial row
     * is what tells the eye there is more to see -- and a column that only
     * ever drew whole rows would look like a complete list that happens to
     * be short.
     */
    /*
     * NARROW, never replace. gfx_set_clip takes what it is given, and the
     * clip already in force is the caller's -- the region rect being
     * repainted, and during the slide-up the part of the panel that has
     * actually risen. Replacing it draws the whole list at its final
     * position over a desktop the panel has not reached yet: 738 pixels of
     * menu floating above a menu that is two fifths of the way up, measured.
     */
    prev_clip = gfx_clip_narrow(s, &col);
    cy = c->y + LR_PAD_V - c->pos;
    for (i = 0; i < m->count; i++) {
        const UiMenuItem *it = &m->items[i];
        int ih = lr_row_height(it);
        if (cy >= col.y1) { break; }          /* the rest is below the band */
        if (cy + ih <= col.y0) { cy += ih; continue; }  /* ...and above it  */
        if (it->id == UI_MENU_SEPARATOR_ID) {
            gfx_hline(s, cx + 8, cy + ih / 2, cw - 16, p->dark);
            gfx_hline(s, cx + 8, cy + ih / 2 + 1, cw - 16, p->light);
        } else {
            CColor tcol = it->enabled ? p->text : p->text_disabled;
            if (i == m->highlight && it->enabled) {
                CRect hl = crect_make(cx + 2, cy, cw - 4, ih);
                gfx_fill_rect(s, &hl, p->accent);
                tcol = p->accent_text;
            }
            if (it->icon != NULL) {
                const GfxSurface *ic = it->icon;
                CRect src = crect_make(0, 0, LR_ICON, LR_ICON);
                gfx_blit(s, cx + 5, cy + (ih - LR_ICON) / 2, ic, &src,
                         GFX_BLIT_KEYED);
            }
            gfx_draw_text(s, GFX_FONT_SYSTEM, cx + LR_TEXT_X,
                          cy + (ih - 8) / 2, it->label, tcol);
        }
        cy += ih;
    }
    gfx_set_clip(s, &prev_clip);
    if (c->bar) {
        ui_scrollbar_draw(s, &c->bar_rect, CTRUE, c->total, c->h, c->pos,
                          hot_part);
    }
}

void sh_launcher_paint(const CRect *clip)
{
    LRGeo g;
    GfxSurface *bk = g_sh.back;
    int vh, r;
    CRect vis, svis, cl, panel;
    CColor lbg, rbg;
    if (!g_sh.launcher_open) { return; }
    launcher_geometry(&g);

    /* Slide-up reveal: draw the whole panel but clip to its bottom 'vh' so it
     * appears to rise out of the taskbar (layers behind repaint the rest). */
    r  = g_sh.launcher_reveal;
    vh = (r >= SH_MENU_REVEAL) ? g.h : (g.h * r / SH_MENU_REVEAL);
    if (vh < 1) { vh = 1; }
    vis = crect_make(g.x, g.y + g.h - vh, g.w, vh);
    svis = vis;
    if (sh_glossy()) { svis.x1 += SH_SHADOW; svis.y1 += SH_SHADOW; }
    if (clip != NULL && !crect_overlaps(&svis, clip)) { return; }
    cl = (clip != NULL) ? crect_intersect(&svis, clip) : svis;
    if (crect_empty(&cl)) { return; }
    gfx_set_clip(bk, &cl);

    if (sh_glossy()) { gfx_drop_shadow(bk, &vis, SH_SHADOW, 90); }

    lbg = GFX_RGB(0xFF, 0xFF, 0xFF);
    rbg = sh_glossy() ? gfx_tint(g_sh.theme.title_active_l, 0xFFFFFF, 208)
                      : g_sh.theme.ui.face;
    {
        LRCol lc, rc;
        lr_column(&g, 0, &lc);
        lr_column(&g, 1, &rc);
        lr_draw_column(bk, &g_sh.launcher_menu,  &lc, lbg,
                       (g_sh.launcher_bar_col == 0) ? g_sh.launcher_bar_hot : 0);
        lr_draw_column(bk, &g_sh.launcher_menu2, &rc, rbg,
                       (g_sh.launcher_bar_col == 1) ? g_sh.launcher_bar_hot : 0);
    }
    /* Divider between the two columns. */
    gfx_vline(bk, g.x + g.lw, g.col_y, g.col_h,
              sh_glossy() ? gfx_tint(g_sh.theme.title_active_l, 0xFFFFFF, 150)
                          : g_sh.theme.ui.dark);

    launcher_header_paint(bk, &g);
    launcher_footer_paint(bk, &g);

    /* A thin frame around the whole panel for definition (border only -- the
     * interior is already painted, so leave it alone). */
    panel = crect_make(g.x, g.y, g.w, g.h);
    gfx_bevel(bk, &panel, GFX_BEVEL_RAISED_THIN,
              gfx_tint(g_sh.theme.title_active_l, 0xFFFFFF, 120),
              gfx_tint(g_sh.theme.title_active_r, 0x000000, 110),
              GFX_NO_FILL);

    gfx_reset_clip(bk);
}

/* ---- hit testing ------------------------------------------------------ */
/*
 * Which item of a column is at (px,py), or -1.
 *
 * The scroll bar is NOT a row: a press on it is a scroll, and answering with
 * the row behind it would launch a program when somebody meant to drag a
 * thumb. It is excluded by testing against content_w rather than the column
 * width.
 */
static int lr_col_hit(const UiMenu *m, const LRCol *c, int px, int py)
{
    int i, cy;
    if (px < c->x || px >= c->x + c->content_w) { return -1; }
    if (py < c->y || py >= c->y + c->h) { return -1; }
    cy = c->y + LR_PAD_V - c->pos;
    for (i = 0; i < m->count; i++) {
        int ih = lr_row_height(&m->items[i]);
        if (py >= cy && py < cy + ih) {
            if (m->items[i].id == UI_MENU_SEPARATOR_ID) { return -1; }
            if (!m->items[i].enabled) { return -1; }
            return i;
        }
        cy += ih;
    }
    return -1;
}

/*
 * What a click at (x,y) inside the open panel would launch, or -1.
 *
 * For the scenes. "Is this entry reachable" is not a question a screenshot
 * can answer -- an entry scrolled off the bottom of a column looks exactly
 * like an entry that was never added, which is the failure this whole
 * scrolling business exists to make impossible.
 */
int sh_launcher_command_at(int x, int y)
{
    LRGeo g;
    LRCol lc, rc;
    int hit;
    if (!g_sh.launcher_open) { return -1; }
    launcher_geometry(&g);
    lr_column(&g, 0, &lc);
    lr_column(&g, 1, &rc);
    hit = lr_col_hit(&g_sh.launcher_menu, &lc, x, y);
    if (hit >= 0) { return g_sh.launcher_menu.items[hit].id; }
    hit = lr_col_hit(&g_sh.launcher_menu2, &rc, x, y);
    if (hit >= 0) { return g_sh.launcher_menu2.items[hit].id; }
    return -1;
}

/* Does a column carry a scroll bar right now, and where is it? For the
 * scenes, and for nothing else. */
cbool sh_launcher_bar_rect(int which, CRect *out)
{
    LRGeo g;
    LRCol c;
    if (!g_sh.launcher_open || which < 0 || which > 1) { return CFALSE; }
    launcher_geometry(&g);
    lr_column(&g, which, &c);
    if (!c.bar) { return CFALSE; }
    if (out != NULL) { *out = c.bar_rect; }
    return CTRUE;
}

/*
 * Mark the two rows a highlight moved between, in one column -- the other
 * direction of the question lr_col_hit answers, so a highlight change can
 * repaint the two rows it moved between instead of the panel.
 *
 * Sliding the pointer down the Start menu marked the whole launcher
 * rectangle dirty on every row it crossed: 137,973 pixels of a
 * 134,268-pixel panel, per row, measured. It is the most-used menu in the
 * system and the one the window menus' narrowing never reached, because it
 * is a shell layer rather than a window and has its own geometry.
 */
static void lr_mark_rows(const UiMenu *m, const LRCol *c, int was, int now)
{
    CRect r;
    if (lr_col_row_rect(m, c, was, &r)) { sh_mark_dirty(&r); }
    if (lr_col_row_rect(m, c, now, &r)) { sh_mark_dirty(&r); }
}

/* ---- the scroll bar --------------------------------------------------- */
/*
 * Scrolling is in PIXELS, not rows, because the rows are not all one height:
 * a separator is six pixels and an item is eighteen. Counting in rows would
 * make the thumb the wrong size on any column with a separator in it, and the
 * thumb's size is the only thing telling you how much list there is.
 *
 * The wheel still moves in ROWS -- ui_scroll_wheel's three-lines-a-notch is
 * the shared rule for the whole system -- which is what multiplying the
 * notches by a row height does here.
 */
static void lr_mark_column(const LRCol *c)
{
    CRect r = crect_make(c->x, c->y, c->w, c->h);
    sh_mark_dirty(&r);
}

/* Move a column to an absolute pixel offset, marking what has to come back.
 * CTRUE when it actually moved. */
static cbool lr_scroll_to(int which, int pos)
{
    LRGeo g;
    LRCol c;
    int max;
    launcher_geometry(&g);
    lr_column(&g, which, &c);
    if (!c.bar) { return CFALSE; }
    max = c.total - c.h;
    if (pos < 0) { pos = 0; }
    if (pos > max) { pos = max; }
    if (pos == c.pos) { return CFALSE; }
    g_sh.launcher_scroll[which] = pos;
    /* The whole column: every row in it moved. */
    lr_mark_column(&c);
    return CTRUE;
}

/* A thumb drag: put the offset where the pointer is along the track. */
static void lr_bar_drag(const LRGeo *g, int which, int y)
{
    LRCol c;
    int pos;
    lr_column(g, which, &c);
    if (!c.bar) { return; }
    pos = ui_scroll_pos_from_coord(c.h, c.total, c.h, y - c.bar_rect.y0);
    lr_scroll_to(which, pos);
}

/* A press somewhere on a column's scroll bar. CTRUE if it was on one. */
static cbool lr_bar_press(const LRGeo *g, int which, int x, int y)
{
    LRCol c;
    int part;
    lr_column(g, which, &c);
    if (!c.bar || !crect_contains(&c.bar_rect, x, y)) { return CFALSE; }
    part = ui_scrollbar_hit(&c.bar_rect, CTRUE, c.total, c.h, c.pos, x, y);
    g_sh.launcher_bar_hot = part;
    g_sh.launcher_bar_col = which;
    switch (part) {
    case UI_SB_LINE_UP:   lr_scroll_to(which, c.pos - LR_ROW_H); break;
    case UI_SB_LINE_DOWN: lr_scroll_to(which, c.pos + LR_ROW_H); break;
    case UI_SB_PAGE_UP:   lr_scroll_to(which, c.pos - c.h);      break;
    case UI_SB_PAGE_DOWN: lr_scroll_to(which, c.pos + c.h);      break;
    case UI_SB_THUMB:     lr_bar_drag(g, which, y);              break;
    default: break;
    }
    /* The bar itself changes appearance while a part is held. */
    if (part != 0) { sh_mark_dirty(&c.bar_rect); }
    return CTRUE;
}

/*
 * The wheel over the panel scrolls whichever column the pointer is over.
 *
 * The Start menu is not a window, so tools/check_wheel.sh -- which looks for
 * windows that keep a scroll offset -- cannot see it. It is exactly the kind
 * of place a wheel gets forgotten, which is the argument that check was
 * written to make.
 */
cbool sh_launcher_handle_wheel(int x, int y, int notches)
{
    LRGeo g;
    LRCol c;
    int which, pos;
    if (!g_sh.launcher_open) { return CFALSE; }
    if (!crect_contains(&g_sh.launcher_rect, x, y)) { return CFALSE; }
    launcher_geometry(&g);
    which = (x < g.x + g.lw) ? 0 : 1;
    lr_column(&g, which, &c);
    if (!c.bar) { return CTRUE; }  /* over the panel, but nothing to scroll */
    pos = c.pos;
    if (!ui_scroll_wheel(&pos, notches * LR_ROW_H, c.total, c.h)) {
        return CTRUE;
    }
    lr_scroll_to(which, pos);
    return CTRUE;
}

/* Let go of whatever bar part was held. */
void sh_launcher_release(void)
{
    if (g_sh.launcher_bar_hot == 0) { return; }
    g_sh.launcher_bar_hot = 0;
    {
        LRGeo g;
        LRCol c;
        launcher_geometry(&g);
        lr_column(&g, g_sh.launcher_bar_col, &c);
        if (c.bar) { sh_mark_dirty(&c.bar_rect); }
    }
}

/* Which footer button is at (px,py): 0 Log Off, 1 Shut Down, or -1. */
static int lr_footer_hit(const LRGeo *g, int px, int py)
{
    CRect logoff, shut;
    lr_footer_buttons(g, &logoff, &shut);
    if (crect_contains(&logoff, px, py)) { return 0; }
    if (crect_contains(&shut, px, py))   { return 1; }
    return -1;
}

void sh_launcher_handle_motion(int x, int y)
{
    LRGeo g;
    LRCol lc, rc;
    int lhit, rhit, fhit, was_focus;
    cbool changed = CFALSE;
    if (!g_sh.launcher_open) { return; }
    was_focus = g_sh.launcher_focus;
    launcher_geometry(&g);
    lr_column(&g, 0, &lc);
    lr_column(&g, 1, &rc);
    /* A held scroll thumb follows the pointer wherever it goes, including off
     * the bar -- letting go is what ends a drag, not leaving the track. */
    if (g_sh.launcher_bar_hot == UI_SB_THUMB) {
        lr_bar_drag(&g, g_sh.launcher_bar_col, y);
        return;
    }
    lhit = lr_col_hit(&g_sh.launcher_menu,  &lc, x, y);
    rhit = lr_col_hit(&g_sh.launcher_menu2, &rc, x, y);
    fhit = lr_footer_hit(&g, x, y);
    if (lhit >= 0) { g_sh.launcher_focus = 0; }
    else if (rhit >= 0) { g_sh.launcher_focus = 1; }
    if (g_sh.launcher_menu.highlight != lhit) {
        int was = g_sh.launcher_menu.highlight;
        g_sh.launcher_menu.highlight = lhit;
        lr_mark_rows(&g_sh.launcher_menu, &lc, was, lhit);
        changed = CTRUE;
    }
    if (g_sh.launcher_menu2.highlight != rhit) {
        int was = g_sh.launcher_menu2.highlight;
        g_sh.launcher_menu2.highlight = rhit;
        lr_mark_rows(&g_sh.launcher_menu2, &rc, was, rhit);
        changed = CTRUE;
    }
    if (g_sh.launcher_foot_hl != fhit) {
        /* The footer is two buttons in a strip; the strip is small and the
         * two are adjacent, so it comes back whole. */
        CRect logoff, shut, strip;
        g_sh.launcher_foot_hl = fhit;
        lr_footer_buttons(&g, &logoff, &shut);
        strip = crect_union(&logoff, &shut);
        sh_mark_dirty(&strip);
        changed = CTRUE;
    }
    /*
     * The FOCUS marker moves with the pointer between columns, and it is
     * drawn on the column rather than on a row -- so a move that changed
     * which column is focused has to bring both columns back.
     */
    if (changed && (lhit >= 0 || rhit >= 0)) {
        if (g_sh.launcher_focus != was_focus) {
            sh_mark_dirty(&g_sh.launcher_rect);
        }
    }
}

cbool sh_launcher_handle_click(int x, int y, int buttons)
{
    LRGeo g;
    int hit, cmd = 0;
    CASTALIA_UNUSED(buttons);
    if (!g_sh.launcher_open) { return CFALSE; }
    launcher_geometry(&g);
    if (!crect_contains(&g_sh.launcher_rect, x, y)) {
        sh_open_launcher(CFALSE);
        return CFALSE; /* let the click also reach whatever is under it */
    }
    /* A scroll bar answers first: it sits inside a column, and the row behind
     * it must not be launched by somebody reaching for a thumb. */
    if (lr_bar_press(&g, 0, x, y) || lr_bar_press(&g, 1, x, y)) {
        return CTRUE;
    }
    hit = lr_footer_hit(&g, x, y);
    if (hit >= 0) {
        cmd = (hit == 0) ? SH_CMD_RESTART_SHELL : SH_CMD_SHUTDOWN;
        sh_open_launcher(CFALSE);
        sh_dispatch_command(cmd);
        return CTRUE;
    }
    {
        LRCol lc, rc;
        lr_column(&g, 0, &lc);
        lr_column(&g, 1, &rc);
        hit = lr_col_hit(&g_sh.launcher_menu, &lc, x, y);
        if (hit >= 0) { cmd = g_sh.launcher_menu.items[hit].id; }
        else {
            hit = lr_col_hit(&g_sh.launcher_menu2, &rc, x, y);
            if (hit >= 0) { cmd = g_sh.launcher_menu2.items[hit].id; }
        }
    }
    if (hit >= 0) {
        sh_open_launcher(CFALSE);
        sh_dispatch_command(cmd);
    }
    return CTRUE;
}

/* ---- keyboard navigation --------------------------------------------- */

/*
 * Mark row 'i' of whichever column 'm' is, in screen coordinates -- the
 * keyboard's way into the same narrowing the pointer got, since the arrow
 * keys move exactly the highlight the mouse does and there is no reason for
 * one of them to cost a panel and the other two rows.
 */
static void lr_mark_one(const UiMenu *m, int i)
{
    LRGeo g;
    LRCol c;
    CRect r;
    int which;
    if (i < 0) { return; }
    if (m == &g_sh.launcher_menu)       { which = 0; }
    else if (m == &g_sh.launcher_menu2) { which = 1; }
    else                                { return; }
    launcher_geometry(&g);
    lr_column(&g, which, &c);
    if (lr_col_row_rect(m, &c, i, &r)) { sh_mark_dirty(&r); }
}

/*
 * Bring row 'i' of a column fully into view, scrolling if it is not.
 *
 * The arrow keys can walk past the bottom of a scrolled column, and a
 * highlight you cannot see is worse than no highlight: Enter then launches
 * something that was never on screen. Answered in pixels because the rows
 * are not one height.
 */
static void lr_scroll_into_view(int which, int i)
{
    LRGeo g;
    LRCol c;
    const UiMenu *m = (which == 0) ? &g_sh.launcher_menu : &g_sh.launcher_menu2;
    int k, top, bottom, want;
    if (i < 0 || i >= m->count) { return; }
    launcher_geometry(&g);
    lr_column(&g, which, &c);
    if (!c.bar) { return; }
    top = LR_PAD_V;
    for (k = 0; k < i; k++) { top += lr_row_height(&m->items[k]); }
    bottom = top + lr_row_height(&m->items[i]);
    want = c.pos;
    if (top < want)          { want = top; }
    if (bottom > want + c.h) { want = bottom - c.h; }
    if (want != c.pos) { lr_scroll_to(which, want); }
}

/* The footer strip, for when a highlight leaves it. */
static void lr_mark_footer(void)
{
    LRGeo g;
    CRect logoff, shut, strip;
    launcher_geometry(&g);
    lr_footer_buttons(&g, &logoff, &shut);
    strip = crect_union(&logoff, &shut);
    sh_mark_dirty(&strip);
}

/* Move a column's highlight by 'dir' (+1 down / -1 up), skipping separators
 * and disabled rows, wrapping around the ends. */
static void lr_step(UiMenu *m, int dir)
{
    int prev = m->highlight;
    if (ui_menu_step(m, dir)) {
        int which = (m == &g_sh.launcher_menu2) ? 1 : 0;
        /* Into view FIRST: scrolling moves every row, so the two-row marking
         * below would be marking rows at their old positions. lr_scroll_to
         * marks the whole column when it moves, which covers both. */
        lr_scroll_into_view(which, m->highlight);
        lr_mark_one(m, prev);
        lr_mark_one(m, m->highlight);
    }
}

/* Ensure the focused column has a landed highlight; drop the other's. */
static void lr_focus_column(int col)
{
    UiMenu *foc   = (col == 0) ? &g_sh.launcher_menu : &g_sh.launcher_menu2;
    UiMenu *other = (col == 0) ? &g_sh.launcher_menu2 : &g_sh.launcher_menu;
    g_sh.launcher_focus = col;
    other->highlight = -1;
    g_sh.launcher_foot_hl = -1;
    if (!ui_menu_selectable(foc, foc->highlight)) {
        foc->highlight = -1;
        lr_step(foc, +1);
    }
    sh_mark_dirty(&g_sh.launcher_rect);
}

cbool sh_launcher_handle_key(int key)
{
    UiMenu *foc, *other;
    if (!g_sh.launcher_open) { return CFALSE; }
    foc   = (g_sh.launcher_focus == 0) ? &g_sh.launcher_menu
                                       : &g_sh.launcher_menu2;
    other = (g_sh.launcher_focus == 0) ? &g_sh.launcher_menu2
                                       : &g_sh.launcher_menu;
    if (key == PLAT_KEY_UP || key == PLAT_KEY_DOWN) {
        /* Step within the focused column; only one highlight shows at a time.
         * The other column's highlight and the footer's are dropped here, so
         * whatever they were showing has to be brought back -- the two rows
         * lr_step marks are in THIS column and would leave the abandoned
         * highlight lit somewhere else on the panel. */
        if (other->highlight >= 0) { lr_mark_one(other, other->highlight); }
        other->highlight = -1;
        if (g_sh.launcher_foot_hl >= 0) { lr_mark_footer(); }
        g_sh.launcher_foot_hl = -1;
        lr_step(foc, (key == PLAT_KEY_DOWN) ? +1 : -1);
        return CTRUE;
    }
    if (key == PLAT_KEY_LEFT)  { lr_focus_column(0); return CTRUE; }
    if (key == PLAT_KEY_RIGHT) { lr_focus_column(1); return CTRUE; }
    if (key == PLAT_KEY_ENTER) {
        if (ui_menu_selectable(foc, foc->highlight)) {
            int cmd = foc->items[foc->highlight].id;
            sh_open_launcher(CFALSE);
            sh_dispatch_command(cmd);
        }
        return CTRUE;
    }
    /* Swallow other keys so they do not leak to a window while the menu owns
     * the keyboard; Esc is handled by the caller (closes the menu). */
    return CTRUE;
}
