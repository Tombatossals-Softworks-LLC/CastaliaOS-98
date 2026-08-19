/*
 * app_sheet.c - CastaliaSheet: a spreadsheet in the 2001 office idiom.
 *
 * A workbook of three sheets over the pure engine in sheet_core.c, dressed the
 * way the spreadsheets of that era were: a menu bar with real drop-downs, a
 * Standard toolbar whose buttons stay flat until hovered, a formula bar with a
 * Name Box and an fx button, an A1 grid with a select-all corner and headers
 * that highlight the cursor, sheet tabs along the bottom, and a status bar of
 * sunken panels showing the live sum of the current column.
 *
 * Every displayed value is computed by the engine; sheets save to CSV with the
 * formulas preserved.
 */
#include "apps.h"
#include "sheet_core.h"
#include "undo_core.h"
#include "office_ui.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include <stdlib.h>

/* Ctrl-shortcut detection on both encodings the platform contract allows, as
 * in Notepad, Paint and Write: DOS delivers Ctrl+letter as an ASCII control
 * code in 'key', while synthetic and host-pushed input may carry the letter
 * with PLAT_MOD_CTRL set. Testing the control code alone -- which this file
 * did -- answers only half the contract. */
#define SH_CTRL(key, ch, mods, code, letter) \
    ((key) == (code) || \
     (((mods) & PLAT_MOD_CTRL) && ((ch) == (letter) || (ch) == (letter) - 32)))

#define SH_FBAR_H    22
#define SH_NAME_W    58
#define SH_ROWHDR_W  26
#define SH_COLHDR_H  16
#define SH_COL_W     62
#define SH_ROW_H     16
#define SH_TABS_H    20
#define SH_SHEETS    3            /* Sheet1..Sheet3 (a real workbook)     */

/* ---- toolbar ---------------------------------------------------------- */
enum { SB_NEW = 0, SB_OPEN, SB_SAVE, SB_PRINT, SB_CUT, SB_COPY, SB_PASTE,
       SB_SUM, SB_FUNC, SB_N };
static const struct { int icon; int gap; } SH_TB[SB_N] = {
    { OFI_NEW, 0 }, { OFI_OPEN, 0 }, { OFI_SAVE, 0 }, { OFI_PRINT, 0 },
    { OFI_CUT, 1 }, { OFI_COPY, 0 }, { OFI_PASTE, 0 },
    { OFI_SUM, 1 }, { OFI_FUNC, 0 }
};

/* ---- menus ------------------------------------------------------------ */
static const char *const SH_MENU[] = {
    "File", "Edit", "View", "Insert", "Tools", "Help"
};
#define SH_MENU_N 6

enum {
    SC_NEW = 300, SC_OPEN, SC_SAVE, SC_SAVEAS, SC_CLOSE,
    SC_CUT, SC_COPY, SC_PASTE, SC_CLEAR,
    SC_FBAR, SC_STATUS,
    SC_FSUM, SC_FAVG,
    SC_RECALC, SC_ABOUT, SC_FUNCS
};

typedef struct {
    Sheet sh[SH_SHEETS];
    int   book;                   /* active sheet                          */
    int   cur_r[SH_SHEETS], cur_c[SH_SHEETS];
    int   top_r[SH_SHEETS], left_c[SH_SHEETS];
    int   sel_r[SH_SHEETS], sel_c[SH_SHEETS];  /* selection anchor    */
    cbool selecting;            /* dragging out a range                  */
    cbool editing;
    char  edit[SHEET_TEXT];
    int   edit_len;
    char  clip[SHEET_TEXT];       /* one-cell clipboard                    */
    int   hover_tb;
    int   hot_sb_v, hot_sb_h;   /* active scroll-bar parts             */
    cbool sb_drag_v, sb_drag_h;
    int   menu_open, menu_x, menu_y;
    UiMenu menu;
    cbool show_fbar, show_status;
    char  status[80];
    cu32  saved_sum;      /* the workbook as it was when last saved/loaded  */
    char  path[CASTALIA_MAX_PATH];
    CellUndo undo;              /* Ctrl+Z over the edits below           */
} SheetApp;

typedef struct {
    CRect menubar;
    CRect tbar;
    CRect btn[SB_N];
    CRect namebox, fxbtn, fbar;
    CRect grid;
    CRect vbar, hbar;
    CRect tabs;
    CRect tab[SH_SHEETS];
    CRect status;
    int   vis_r, vis_c;
} ShLayout;

/* Shorthands for the active sheet's state. */
#define CURSH(a)  (&(a)->sh[(a)->book])
#define CURR(a)   ((a)->cur_r[(a)->book])
#define CURC(a)   ((a)->cur_c[(a)->book])
#define TOPR(a)   ((a)->top_r[(a)->book])
#define LEFTC(a)  ((a)->left_c[(a)->book])
#define SELR(a)   ((a)->sel_r[(a)->book])
#define SELC(a)   ((a)->sel_c[(a)->book])

/* The selected rectangle, normalized (the anchor and the cursor are corners). */
static void sh_sel_rect(const SheetApp *a, int *r0, int *c0, int *r1, int *c1)
{
    int ar = SELR(a), ac = SELC(a), cr = CURR(a), cc = CURC(a);
    *r0 = (ar < cr) ? ar : cr;  *r1 = (ar > cr) ? ar : cr;
    *c0 = (ac < cc) ? ac : cc;  *c1 = (ac > cc) ? ac : cc;
}
static cbool sh_multi(const SheetApp *a)
{
    return (SELR(a) != CURR(a) || SELC(a) != CURC(a)) ? CTRUE : CFALSE;
}

static void sh_layout(WmWindow *win, const SheetApp *a, ShLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    int i, x = 4, y;
    int fh = a->show_fbar ? SH_FBAR_H : 0;
    int sh = a->show_status ? OF_STATUS_H : 0;

    L->menubar = crect_make(0, 0, cw, OF_MENUBAR_H);
    L->tbar    = crect_make(0, OF_MENUBAR_H, cw, OF_TOOLBAR_H);
    for (i = 0; i < SB_N; i++) {
        if (SH_TB[i].gap) { x += 7; }
        L->btn[i] = crect_make(x, L->tbar.y0 + 2, OF_BTN_W, OF_BTN_H);
        x += OF_BTN_W + 1;
    }
    y = OF_MENUBAR_H + OF_TOOLBAR_H;
    L->namebox = crect_make(3, y + 2, SH_NAME_W, fh - 5);
    L->fxbtn   = crect_make(3 + SH_NAME_W + 4, y + 2, 20, fh - 5);
    L->fbar    = crect_make(3 + SH_NAME_W + 28, y + 2,
                            cw - (3 + SH_NAME_W + 28) - 4, fh - 5);
    y += fh;
    {
        int gh = ch - y - SH_TABS_H - sh - UI_SB_W;
        if (gh < SH_COLHDR_H + SH_ROW_H) { gh = SH_COLHDR_H + SH_ROW_H; }
        L->grid = crect_make(0, y, cw - UI_SB_W, gh);
        L->vbar = crect_make(cw - UI_SB_W, y, UI_SB_W, gh);
        L->hbar = crect_make(0, y + gh, cw - UI_SB_W, UI_SB_W);
    }
    L->tabs   = crect_make(0, ch - SH_TABS_H - sh, cw, SH_TABS_H);
    for (i = 0; i < SH_SHEETS; i++) {
        L->tab[i] = crect_make(24 + i * 56, L->tabs.y0 + 2, 54, SH_TABS_H - 3);
    }
    L->status = crect_make(0, ch - sh, cw, sh);
    L->vis_c = (crect_w(&L->grid) - SH_ROWHDR_W) / SH_COL_W;
    L->vis_r = (crect_h(&L->grid) - SH_COLHDR_H) / SH_ROW_H;
    if (L->vis_c < 1) { L->vis_c = 1; }
    if (L->vis_r < 1) { L->vis_r = 1; }
}

static CRect sh_cell_rect(const ShLayout *L, const SheetApp *a, int r, int c)
{
    return crect_make(L->grid.x0 + SH_ROWHDR_W + (c - LEFTC(a)) * SH_COL_W,
                      L->grid.y0 + SH_COLHDR_H + (r - TOPR(a)) * SH_ROW_H,
                      SH_COL_W, SH_ROW_H);
}

/*
 * What a SCROLL changed: the grid, and the two bars whose thumbs moved with
 * it. The menu bar, the toolbar, the formula bar, the sheet tabs and the
 * status line stay where they are -- scrolling does not touch any of them,
 * and repainting them was most of what a thumb drag used to cost.
 */
static void sh_invalidate_scroll(WmWindow *win, const ShLayout *L)
{
    CPoint o = wm_client_origin(win);
    CRect r = crect_union(&L->grid, &L->vbar);
    r = crect_union(&r, &L->hbar);
    r = crect_offset(&r, o.x, o.y);
    wm_invalidate(win, &r);
}

static void sh_fit(char *buf, int maxpx)
{
    int n = (int)sys_strnlen(buf, 64);
    while (n > 0 && gfx_text_width(GFX_FONT_SYSTEM, buf) > maxpx) {
        buf[--n] = '\0';
    }
}

static void sh_scroll_to_cursor(SheetApp *a, const ShLayout *L)
{
    if (CURR(a) < TOPR(a)) { TOPR(a) = CURR(a); }
    if (CURR(a) >= TOPR(a) + L->vis_r) { TOPR(a) = CURR(a) - L->vis_r + 1; }
    if (CURC(a) < LEFTC(a)) { LEFTC(a) = CURC(a); }
    if (CURC(a) >= LEFTC(a) + L->vis_c) { LEFTC(a) = CURC(a) - L->vis_c + 1; }
    if (TOPR(a) < 0) { TOPR(a) = 0; }
    if (LEFTC(a) < 0) { LEFTC(a) = 0; }
}

/*
 * Every edit the USER makes goes through here so it can be taken back.
 *
 * sheet_set is still called directly in two places that are deliberately not
 * undoable: loading a file, and laying out the sample book. Neither is
 * something the user did, and putting them on the stack would mean Ctrl+Z
 * silently dismantling a document that was never edited.
 */
static void sh_set(SheetApp *a, int r, int c, const char *text)
{
    Sheet *s = CURSH(a);
    char before[SHEET_TEXT];
    sys_strlcpy(before, sheet_raw(s, r, c), sizeof before);
    sheet_set(s, r, c, text);
    cundo_record(&a->undo, a->book, r, c, before, text);
}

static void sh_commit(SheetApp *a)
{
    if (!a->editing) { return; }
    sh_set(a, CURR(a), CURC(a), a->edit);
    a->editing = CFALSE;
    a->edit[0] = '\0';
    a->edit_len = 0;
}

static void sh_begin_edit(SheetApp *a, cbool keep)
{
    if (a->editing) { return; }
    a->editing = CTRUE;
    if (keep) {
        sys_strlcpy(a->edit, sheet_raw(CURSH(a), CURR(a), CURC(a)),
                    (cu32)sizeof a->edit);
    } else { a->edit[0] = '\0'; }
    a->edit_len = (int)sys_strnlen(a->edit, (cu32)sizeof a->edit);
}

static void sh_move(SheetApp *a, const ShLayout *L, int dr, int dc)
{
    sh_commit(a);
    CURR(a) += dr;
    CURC(a) += dc;
    if (CURR(a) < 0) { CURR(a) = 0; }
    if (CURC(a) < 0) { CURC(a) = 0; }
    if (CURR(a) >= SHEET_ROWS) { CURR(a) = SHEET_ROWS - 1; }
    if (CURC(a) >= SHEET_COLS) { CURC(a) = SHEET_COLS - 1; }
    SELR(a) = CURR(a);
    SELC(a) = CURC(a);
    sh_scroll_to_cursor(a, L);
}

/* =SUM()/=AVG() over the run of numbers above the cursor. */
static void sh_function_above(SheetApp *a, const char *fn)
{
    Sheet *s = CURSH(a);
    int r = CURR(a) - 1, first;
    char buf[SHEET_TEXT], from[8], to[8];
    if (r < 0) {
        sys_snprintf(a->status, sizeof a->status,
                     "%s needs numbers above the cell", fn);
        return;
    }
    while (r >= 0) {
        int k = sheet_kind(s, r, CURC(a));
        if (k != SHEET_NUMBER && k != SHEET_FORMULA) { break; }
        r--;
    }
    first = r + 1;
    if (first > CURR(a) - 1) {
        sys_snprintf(a->status, sizeof a->status,
                     "%s needs numbers above the cell", fn);
        return;
    }
    sheet_cellname(first, CURC(a), from, (int)sizeof from);
    sheet_cellname(CURR(a) - 1, CURC(a), to, (int)sizeof to);
    sys_snprintf(buf, sizeof buf, "=%s(%s:%s)", fn, from, to);
    sh_set(a, CURR(a), CURC(a), buf);
    sys_snprintf(a->status, sizeof a->status, "%s", buf);
}

static void sh_sample(SheetApp *a)
{
    static const char *item[6] = {
        "Cartridge", "Floppies", "Mouse", "Sound card", "Modem", "Manual"
    };
    static const char *qty[6]  = { "2", "10", "1", "1", "1", "3" };
    static const char *price[6] = { "24.95", "1.5", "18", "89.9", "45", "12.5" };
    Sheet *s = &a->sh[0];
    int i;
    sheet_clear(s);
    sheet_set(s, 0, 0, "Item");
    sheet_set(s, 0, 1, "Qty");
    sheet_set(s, 0, 2, "Price");
    sheet_set(s, 0, 3, "Total");
    for (i = 0; i < 6; i++) {
        char f[SHEET_TEXT];
        sheet_set(s, i + 1, 0, item[i]);
        sheet_set(s, i + 1, 1, qty[i]);
        sheet_set(s, i + 1, 2, price[i]);
        sys_snprintf(f, sizeof f, "=B%d*C%d", i + 2, i + 2);
        sheet_set(s, i + 1, 3, f);
    }
    sheet_set(s, 7, 0, "Subtotal");
    sheet_set(s, 7, 3, "=SUM(D2:D7)");
    sheet_set(s, 8, 0, "Tax 21%");
    sheet_set(s, 8, 3, "=D8*0.21");
    sheet_set(s, 9, 0, "TOTAL");
    sheet_set(s, 9, 3, "=D8+D9");
    sheet_set(s, 11, 0, "Items");
    sheet_set(s, 11, 3, "=COUNT(D2:D7)");
    sheet_set(s, 12, 0, "Dearest");
    sheet_set(s, 12, 3, "=MAX(D2:D7)");
    a->cur_r[0] = 9; a->cur_c[0] = 3;
    a->top_r[0] = 0; a->left_c[0] = 0;
    sys_strlcpy(a->status, "Ready", sizeof a->status);
}

/* ---- files ------------------------------------------------------------ */
/*
 * A checksum of the whole workbook, DERIVED rather than tracked.
 *
 * The alternative is a "modified" flag set by every command that edits, and
 * that flag is wrong the first time somebody adds an edit path and forgets
 * it -- wrong in the direction that loses work, silently. A sum over the
 * cells cannot miss an edit, because there is nowhere for one to hide.
 */
static cu32 sh_doc_sum(const SheetApp *a)
{
    cu32 h = 2166136261u;
    int i, r, c, k;
    for (i = 0; i < SH_SHEETS; i++) {
        for (r = 0; r < SHEET_ROWS; r++) {
            for (c = 0; c < SHEET_COLS; c++) {
                const char *t = a->sh[i].raw[r][c];
                for (k = 0; t[k] != '\0'; k++) {
                    h = (h ^ (cu32)(unsigned char)t[k]) * 16777619u;
                }
                h = (h ^ 1u) * 16777619u;   /* the cell boundary counts */
            }
        }
    }
    return h;
}

static void sh_on_discard(UiDialogResult result, void *user)
{
    WmWindow *win = (WmWindow *)user;
    if (result == UI_DR_YES && win != NULL) { wm_destroy(win); }
}

/* Closing a workbook with unsaved changes asks first -- see app_notepad.c. */
/* One predicate, asked by the close box and by the shutdown dialog. */
static cbool sh_has_unsaved(WmWindow *win)
{
    SheetApp *a = (win != NULL) ? (SheetApp *)wm_user(win) : NULL;
    return (a != NULL && sh_doc_sum(a) != a->saved_sum) ? CTRUE : CFALSE;
}

static cbool sh_on_close(WmWindow *win)
{
    SheetApp *a = (win != NULL) ? (SheetApp *)wm_user(win) : NULL;
    char msg[160];
    if (!sh_has_unsaved(win)) { return CFALSE; }
    sys_snprintf(msg, sizeof(msg),
                 "%s has changes you have not saved.\n\n"
                 "Close it and lose them?",
                 (a->path[0] != '\0') ? a->path : "This workbook");
    ui_msgbox("CastaliaSheet", msg, UI_MB_YESNO, sh_on_discard, win);
    return CTRUE;
}

/* Where documents live, created on demand so the file dialog opens on a real
 * folder rather than on nothing. */
static void sh_save(SheetApp *a, const char *name)
{
    char path[CASTALIA_MAX_PATH];
    PlatFile *f;
    char *buf;
    int n;
    /* Called for the folder it creates, not for the name it returns: a Save
     * into a documents folder that was never made would fail for a reason the
     * user cannot see. */
    (void)office_docs_dir();
    office_doc_path(path, sizeof path, name);

    buf = (char *)sys_alloc((cu32)SHEET_CSV_MAX);
    if (buf == NULL) {
        sys_strlcpy(a->status, "Out of memory -- not saved", sizeof a->status);
        return;
    }
    n = sheet_to_csv(CURSH(a), buf, SHEET_CSV_MAX);
    /* The buffer is sized for the worst case, so this cannot normally fire --
     * but never hand plat_fwrite a length the buffer does not hold. */
    if (n < 0 || n >= SHEET_CSV_MAX) {
        sys_strlcpy(a->status, "Sheet too large to save intact",
                    sizeof a->status);
        sys_free(buf, (cu32)SHEET_CSV_MAX);
        return;
    }
    f = plat_fopen(path, "wb");
    if (f == NULL) {
        sys_snprintf(a->status, sizeof a->status, "Could not save '%s'", name);
        sys_free(buf, (cu32)SHEET_CSV_MAX);
        return;
    }
    plat_fwrite(f, buf, (cu32)n);
    plat_fclose(f);
    sys_free(buf, (cu32)SHEET_CSV_MAX);
    sys_strlcpy(a->path, name, sizeof a->path);
    a->saved_sum = sh_doc_sum(a);
    sys_snprintf(a->status, sizeof a->status, "Saved '%s'", name);
}

static void sh_open_file(SheetApp *a, const char *name)
{
    char path[CASTALIA_MAX_PATH];
    PlatFile *f;
    char *buf;
    cu32 got;
    SheetLoad li;
    cbool whole;
    office_doc_path(path, sizeof path, name);
    f = plat_fopen(path, "rb");
    if (f == NULL) {
        sys_snprintf(a->status, sizeof a->status, "Could not open '%s'", name);
        return;
    }
    buf = (char *)sys_alloc((cu32)SHEET_CSV_MAX);
    if (buf == NULL) {
        plat_fclose(f);
        sys_strlcpy(a->status, "Out of memory -- not opened", sizeof a->status);
        return;
    }
    got = plat_fread(f, buf, (cu32)SHEET_CSV_MAX - 1);
    plat_fclose(f);
    buf[got] = '\0';
    whole = sheet_from_csv(CURSH(a), buf, &li);
    sys_free(buf, (cu32)SHEET_CSV_MAX);
    CURR(a) = 0; CURC(a) = 0; TOPR(a) = 0; LEFTC(a) = 0;
    /*
     * A CSV bigger than the grid is SHOWN, because seeing the first 48 rows
     * is worth something -- but the file it came from is not adopted, so
     * Ctrl+S offers BOOK1.CSV instead of writing 48 rows back over 100. It
     * used to say "Opened" and do exactly that.
     */
    if (!whole) {
        a->path[0] = '\0';
        if (li.dropped_rows > 0) {
            sys_snprintf(a->status, sizeof a->status,
                         "'%s' has %d rows -- showing %d. Save will ask for a "
                         "new name.", name, li.rows, SHEET_ROWS);
        } else if (li.dropped_cols > 0) {
            sys_snprintf(a->status, sizeof a->status,
                         "'%s' has %d columns -- showing %d. Save will ask for "
                         "a new name.", name, li.cols, SHEET_COLS);
        } else {
            sys_snprintf(a->status, sizeof a->status,
                         "'%s': %d cell%s too long for this sheet. Save will "
                         "ask for a new name.", name, li.clipped,
                         (li.clipped == 1) ? " is" : "s are");
        }
        return;
    }
    sys_strlcpy(a->path, name, sizeof a->path);
    a->saved_sum = sh_doc_sum(a);
    sys_snprintf(a->status, sizeof a->status, "Opened '%s'", name);
}

static void sh_on_save(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    SheetApp *a = (win != NULL) ? (SheetApp *)wm_user(win) : NULL;
    if (!ok || a == NULL || text == NULL || text[0] == '\0') { return; }
    sh_save(a, text);
    wm_invalidate(win, NULL);
}
static void sh_on_open(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    SheetApp *a = (win != NULL) ? (SheetApp *)wm_user(win) : NULL;
    if (!ok || a == NULL || text == NULL || text[0] == '\0') { return; }
    sh_open_file(a, text);
    wm_invalidate(win, NULL);
}

/* ---- menus ------------------------------------------------------------ */
static void sh_build_menu(SheetApp *a, int idx)
{
    UiMenu *m = &a->menu;
    ui_menu_clear(m);
    switch (idx) {
    case 0:
        ui_menu_add(m, SC_NEW, "New", CTRUE);
        ui_menu_add(m, SC_OPEN, "Open...\tCtrl+O", CTRUE);
        ui_menu_add(m, SC_SAVE, "Save\tCtrl+S", CTRUE);
        ui_menu_add(m, SC_SAVEAS, "Save As...", CTRUE);
        ui_menu_add_separator(m);
        ui_menu_add(m, SC_CLOSE, "Close", CTRUE);
        break;
    case 1:
        ui_menu_add(m, SC_CUT, "Cut\tCtrl+X", CTRUE);
        ui_menu_add(m, SC_COPY, "Copy\tCtrl+C", CTRUE);
        ui_menu_add(m, SC_PASTE, "Paste\tCtrl+V",
                    (a->clip[0] != '\0') ? CTRUE : CFALSE);
        ui_menu_add_separator(m);
        ui_menu_add(m, SC_CLEAR, "Clear", CTRUE);
        break;
    case 2:
        ui_menu_add(m, SC_FBAR,
                    a->show_fbar ? "Hide Formula Bar" : "Formula Bar", CTRUE);
        ui_menu_add(m, SC_STATUS,
                    a->show_status ? "Hide Status Bar" : "Status Bar", CTRUE);
        break;
    case 3:
        ui_menu_add(m, SC_FSUM, "Function: SUM", CTRUE);
        ui_menu_add(m, SC_FAVG, "Function: AVG", CTRUE);
        ui_menu_add_separator(m);
        ui_menu_add(m, SC_FUNCS, "Function List...", CTRUE);
        break;
    case 4:
        ui_menu_add(m, SC_RECALC, "Recalculate", CTRUE);
        break;
    default:
        ui_menu_add(m, SC_ABOUT, "About CastaliaSheet...", CTRUE);
        break;
    }
    m->highlight = -1;
}

/* office_menu_key rebuilds the list as it moves along the bar. */
static void sh_menu_build_cb(void *user, int idx)
{
    sh_build_menu((SheetApp *)user, idx);
}

static void sh_open_menu(WmWindow *win, SheetApp *a, int idx)
{
    ShLayout L;
    CRect w;
    sh_layout(win, a, &L);
    w = office_menu_word(&L.menubar, SH_MENU, idx);
    a->menu_open = idx;
    a->menu_x = w.x0;
    a->menu_y = L.menubar.y1;
    sh_build_menu(a, idx);
}

static void sh_command(WmWindow *win, SheetApp *a, int cmd)
{
    Sheet *s;
    /* A cell being typed into is part of the document: commit it before any
     * command reads or writes the sheet, exactly as the toolbar path does. */
    sh_commit(a);
    s = CURSH(a);
    switch (cmd) {
    case SC_NEW:
        sheet_clear(s);
        CURR(a) = 0; CURC(a) = 0; TOPR(a) = 0; LEFTC(a) = 0;
        sys_strlcpy(a->path, "SHEET1.CSV", sizeof a->path);
        a->saved_sum = sh_doc_sum(a);   /* File > New starts clean */
        sys_strlcpy(a->status, "New sheet", sizeof a->status);
        break;
    case SC_OPEN:
        ui_file_dialog("Open", office_docs_dir(), "CSV", CFALSE, a->path,
                       sh_on_open, win);
        break;
    case SC_SAVE:
    case SC_SAVEAS:
        ui_file_dialog("Save As", office_docs_dir(), "CSV", CTRUE,
                       (a->path[0] != '\0') ? a->path : "BOOK1.CSV",
                       sh_on_save, win);
        break;
    case SC_CLOSE: wm_destroy(win); return;
    case SC_COPY:
        sys_strlcpy(a->clip, sheet_raw(s, CURR(a), CURC(a)), sizeof a->clip);
        sys_strlcpy(a->status, "Copied", sizeof a->status);
        break;
    case SC_CUT:
        sys_strlcpy(a->clip, sheet_raw(s, CURR(a), CURC(a)), sizeof a->clip);
        sh_set(a, CURR(a), CURC(a), "");
        sys_strlcpy(a->status, "Cut", sizeof a->status);
        break;
    case SC_PASTE:
        if (a->clip[0] != '\0') {
            sh_set(a, CURR(a), CURC(a), a->clip);
            sys_strlcpy(a->status, "Pasted", sizeof a->status);
        }
        break;
    case SC_CLEAR:
        sh_set(a, CURR(a), CURC(a), "");
        sys_strlcpy(a->status, "Cleared", sizeof a->status);
        break;
    case SC_FBAR:   a->show_fbar = !a->show_fbar; break;
    case SC_STATUS: a->show_status = !a->show_status; break;
    case SC_FSUM:   sh_function_above(a, "SUM"); break;
    case SC_FAVG:   sh_function_above(a, "AVG"); break;
    case SC_RECALC:
        sheet_recalc(s);
        sys_strlcpy(a->status, "Recalculated", sizeof a->status);
        break;
    case SC_FUNCS:
        ui_msgbox("Function List",
                  "SUM(range)    AVG(range)\n"
                  "MIN(range)    MAX(range)\n"
                  "COUNT(range)  ABS(x)\n"
                  "INT(x)        ROUND(x)\n\n"
                  "Ranges are A1:B9; + - * / and\n"
                  "parentheses work as expected.",
                  UI_MB_OK, NULL, NULL);
        break;
    case SC_ABOUT:
        ui_msgbox("About CastaliaSheet",
                  "CastaliaSheet\nThe CastaliaOS spreadsheet.\n\n"
                  "A real formula engine: references,\n"
                  "ranges, functions and error cells.\n"
                  "Saves CSV with formulas preserved.",
                  UI_MB_OK, NULL, NULL);
        break;
    default: break;
    }
}

static void sh_toolbar_cmd(WmWindow *win, SheetApp *a, int i)
{
    static const int map[SB_N] = {
        SC_NEW, SC_OPEN, SC_SAVE, SC_ABOUT,
        SC_CUT, SC_COPY, SC_PASTE,
        SC_FSUM, SC_FUNCS
    };
    if (i == SB_PRINT) {
        ui_msgbox("Print", "No printer driver in this build yet.\n"
                  "Save the sheet and print it from DOS.", UI_MB_OK, NULL, NULL);
        return;
    }
    sh_command(win, a, map[i]);
}

/* ---- paint ------------------------------------------------------------ */
static void sh_paint(WmWindow *win, GfxSurface *s)
{
    SheetApp *a = (SheetApp *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    Sheet *sheet;
    ShLayout L;
    CRect r;
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor grid  = GFX_RGB(0xC8, 0xCE, 0xD8);
    CColor hdr_a = gfx_tint(p->face, 0xFFFFFF, 130);
    CColor hdr_b = gfx_tint(p->face, 0x000000, 20);
    char buf[64];
    int i, rr, cc;
    CColor sel_band;

    if (a == NULL) { return; }
    /* The band only: this sheet writes its own ink, and a block wash carries
     * no text of its own. */
    ui_sel_colors(wm_has_focus(win), &sel_band, (CColor *)0);
    sheet = CURSH(a);
    sh_layout(win, a, &L);

    /* menu bar + toolbar */
    r = crect_offset(&L.menubar, o.x, o.y);
    office_menubar(s, &r, SH_MENU, SH_MENU_N, a->menu_open);
    r = crect_offset(&L.tbar, o.x, o.y);
    office_band(s, &r);
    for (i = 0; i < SB_N; i++) {
        CRect b = crect_offset(&L.btn[i], o.x, o.y);
        int st = (i == a->hover_tb) ? OFB_HOVER : OFB_NORMAL;
        if (i == SB_PASTE && a->clip[0] == '\0') { st = OFB_DISABLED; }
        if (SH_TB[i].gap) {
            office_sep(s, b.x0 - 4, L.tbar.y0 + o.y + 2, OF_TOOLBAR_H - 4);
        }
        office_button(s, &b, SH_TB[i].icon, st);
    }

    /* formula bar: Name Box, fx, and the cell's raw text */
    if (a->show_fbar) {
        CRect band = crect_make(o.x, L.namebox.y0 + o.y - 2,
                                crect_w(&L.grid), SH_FBAR_H);
        gfx_fill_rect(s, &band, p->face);
        r = crect_offset(&L.namebox, o.x, o.y);
        gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
        sheet_cellname(CURR(a), CURC(a), buf, (int)sizeof buf);
        gfx_draw_text_rect(s, GFX_FONT_BOLD, &r, buf, p->text,
                           GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
        r = crect_offset(&L.fxbtn, o.x, o.y);
        office_button(s, &r, OFI_FUNC,
                      (a->hover_tb == SB_N) ? OFB_HOVER : OFB_NORMAL);
        r = crect_offset(&L.fbar, o.x, o.y);
        gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
        {
            const char *txt = a->editing ? a->edit
                                         : sheet_raw(sheet, CURR(a), CURC(a));
            int tx = r.x0 + 5, ty = r.y0 + (crect_h(&r) - 8) / 2;
            gfx_draw_text(s, GFX_FONT_SYSTEM, tx, ty, txt, p->text);
            if (a->editing && wm_has_focus(win)) {
                gfx_vline(s, tx + gfx_text_width(GFX_FONT_SYSTEM, txt) + 1,
                          r.y0 + 3, crect_h(&r) - 6, p->text);
            }
        }
    }

    /* grid well */
    r = crect_offset(&L.grid, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);

    /* column headers */
    for (cc = 0; cc < L.vis_c; cc++) {
        int col = LEFTC(a) + cc;
        CRect hr;
        if (col >= SHEET_COLS) { break; }
        hr = crect_make(r.x0 + SH_ROWHDR_W + cc * SH_COL_W, r.y0,
                        SH_COL_W, SH_COLHDR_H);
        {   /* the header lights up across the whole selected span */
            int r0, c0, r1, c1;
            cbool in_span;
            sh_sel_rect(a, &r0, &c0, &r1, &c1);
            in_span = (col >= c0 && col <= c1) ? CTRUE : CFALSE;
            if (in_span) {
                gfx_vgradient(s, &hr, gfx_tint(p->accent, 0xFFFFFF, 150),
                              gfx_tint(p->accent, 0xFFFFFF, 90));
            } else {
                gfx_vgradient(s, &hr, hdr_a, hdr_b);
            }
            gfx_hline(s, hr.x0, hr.y0, crect_w(&hr), p->light);
            gfx_vline(s, hr.x1 - 1, hr.y0, crect_h(&hr), p->dark);
            gfx_hline(s, hr.x0, hr.y1 - 1, crect_w(&hr), p->dark);
            sheet_colname(col, buf, (int)sizeof buf);
            gfx_draw_text_rect(s, in_span ? GFX_FONT_BOLD : GFX_FONT_SYSTEM,
                               &hr, buf, p->text,
                               GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
        }
    }
    /* row headers */
    for (rr = 0; rr < L.vis_r; rr++) {
        int row = TOPR(a) + rr;
        CRect hr;
        if (row >= SHEET_ROWS) { break; }
        hr = crect_make(r.x0, r.y0 + SH_COLHDR_H + rr * SH_ROW_H,
                        SH_ROWHDR_W, SH_ROW_H);
        {
            int r0, c0, r1, c1;
            cbool in_span;
            sh_sel_rect(a, &r0, &c0, &r1, &c1);
            in_span = (row >= r0 && row <= r1) ? CTRUE : CFALSE;
            if (in_span) {
                gfx_vgradient(s, &hr, gfx_tint(p->accent, 0xFFFFFF, 150),
                              gfx_tint(p->accent, 0xFFFFFF, 90));
            } else {
                gfx_vgradient(s, &hr, hdr_a, hdr_b);
            }
            gfx_vline(s, hr.x0, hr.y0, crect_h(&hr), p->light);
            gfx_vline(s, hr.x1 - 1, hr.y0, crect_h(&hr), p->dark);
            gfx_hline(s, hr.x0, hr.y1 - 1, crect_w(&hr), p->dark);
            sys_snprintf(buf, sizeof buf, "%d", row + 1);
            gfx_draw_text_rect(s, in_span ? GFX_FONT_BOLD : GFX_FONT_SYSTEM,
                               &hr, buf, p->text,
                               GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
        }
    }
    /* select-all corner */
    {
        CRect cr = crect_make(r.x0, r.y0, SH_ROWHDR_W, SH_COLHDR_H);
        gfx_vgradient(s, &cr, hdr_a, hdr_b);
        gfx_hline(s, cr.x0, cr.y1 - 1, crect_w(&cr), p->dark);
        gfx_vline(s, cr.x1 - 1, cr.y0, crect_h(&cr), p->dark);
        for (i = 0; i < 5; i++) {
            gfx_hline(s, cr.x1 - 4 - (4 - i), cr.y1 - 7 + i, i + 1,
                      p->text_disabled);
        }
    }

    /* cells */
    for (rr = 0; rr < L.vis_r; rr++) {
        int row = TOPR(a) + rr;
        if (row >= SHEET_ROWS) { break; }
        for (cc = 0; cc < L.vis_c; cc++) {
            int col = LEFTC(a) + cc, kind, stat;
            CRect cell, base;
            cbool is_cur;
            if (col >= SHEET_COLS) { break; }
            base = sh_cell_rect(&L, a, row, col);
            cell = crect_offset(&base, o.x, o.y);
            is_cur = (row == CURR(a) && col == CURC(a)) ? CTRUE : CFALSE;
            {   /* a selected block reads as a pale accent wash */
                int r0, c0, r1, c1;
                sh_sel_rect(a, &r0, &c0, &r1, &c1);
                if (!is_cur && sh_multi(a) &&
                    row >= r0 && row <= r1 && col >= c0 && col <= c1) {
                    /* Off the same band every other selection in the system
                     * uses, so a book nobody is typing into goes quiet with
                     * the rest of them. */
                    gfx_fill_rect(s, &cell,
                                  gfx_tint(sel_band, 0xFFFFFF, 205));
                } else {
                    gfx_fill_rect(s, &cell, white);
                }
            }
            gfx_vline(s, cell.x1 - 1, cell.y0, crect_h(&cell), grid);
            gfx_hline(s, cell.x0, cell.y1 - 1, crect_w(&cell), grid);
            kind = sheet_kind(sheet, row, col);
            stat = sheet_status(sheet, row, col);
            if (is_cur && a->editing) { sys_strlcpy(buf, a->edit, sizeof buf); }
            else { sheet_display(sheet, row, col, buf, (int)sizeof buf); }
            if (buf[0] != '\0') {
                CRect tr = cell;
                CColor tc = (stat != SHEET_OK && !(is_cur && a->editing))
                          ? GFX_RGB(0xC0, 0x20, 0x20) : p->text;
                int align = (kind == SHEET_LABEL || (is_cur && a->editing))
                          ? GFX_ALIGN_LEFT : GFX_ALIGN_RIGHT;
                tr.x0 += 3; tr.x1 -= 3;
                sh_fit(buf, crect_w(&tr));
                gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &tr, buf, tc,
                                   align | GFX_ALIGN_VCENTER);
            }
            if (is_cur) {
                CRect sel = cell;
                sel.x1--; sel.y1--;
                gfx_frame_rect(s, &sel, p->accent);
                sel = crect_inset(&sel, 1);
                gfx_frame_rect(s, &sel, p->accent);
                if (a->editing) {
                    gfx_vline(s, cell.x0 + 3 +
                              gfx_text_width(GFX_FONT_SYSTEM, buf) + 1,
                              cell.y0 + 3, SH_ROW_H - 6, p->text);
                }
            }
        }
    }

    /* scroll bars: the grid is a window onto the whole sheet */
    r = crect_offset(&L.vbar, o.x, o.y);
    ui_scrollbar_draw(s, &r, CTRUE, SHEET_ROWS, L.vis_r, TOPR(a),
                      (a->hot_sb_v != UI_SB_NONE) ? a->hot_sb_v : UI_SB_NONE);
    r = crect_offset(&L.hbar, o.x, o.y);
    ui_scrollbar_draw(s, &r, CFALSE, SHEET_COLS, L.vis_c, LEFTC(a),
                      (a->hot_sb_h != UI_SB_NONE) ? a->hot_sb_h : UI_SB_NONE);

    /* sheet tabs */
    r = crect_offset(&L.tabs, o.x, o.y);
    gfx_fill_rect(s, &r, p->face);
    gfx_hline(s, r.x0, r.y0, crect_w(&r), gfx_tint(p->face, 0x000000, 40));
    for (i = 0; i < SH_SHEETS; i++) {
        CRect t = crect_offset(&L.tab[i], o.x, o.y);
        cbool act = (i == a->book) ? CTRUE : CFALSE;
        if (act) { gfx_fill_rect(s, &t, white); }
        else     { gfx_vgradient(s, &t, hdr_a, hdr_b); }
        gfx_hline(s, t.x0 + 1, t.y0, crect_w(&t) - 2, p->light);
        gfx_vline(s, t.x0, t.y0 + 1, crect_h(&t) - 1, p->light);
        gfx_vline(s, t.x1 - 1, t.y0 + 1, crect_h(&t) - 1, p->dark);
        sys_snprintf(buf, sizeof buf, "Sheet%d", i + 1);
        gfx_draw_text_rect(s, act ? GFX_FONT_BOLD : GFX_FONT_SYSTEM, &t, buf,
                           p->text, GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
    }

    /* status bar: message, the cursor's value, and the column's live sum */
    if (a->show_status) {
        int px, pw;
        r = crect_offset(&L.status, o.x, o.y);
        office_statusbar(s, &r);
        px = r.x0 + 2;
        pw = crect_w(&r) - 4 - 300;
        if (pw < 60) { pw = 60; }
        { CRect pn = crect_make(px, r.y0 + 2, pw, crect_h(&r) - 4);
          office_status_panel(s, &pn, a->status, GFX_ALIGN_LEFT); px += pw + 2; }
        sheet_display(sheet, CURR(a), CURC(a), buf, (int)sizeof buf);
        { CRect pn = crect_make(px, r.y0 + 2, 110, crect_h(&r) - 4);
          office_status_panel(s, &pn, buf, GFX_ALIGN_HCENTER); px += 112; }
        {   /* Sum / Average / Count over the selection -- a whole column
             * when nothing is dragged out, the block when it is. */
            double sum = 0.0;
            char nb[32], ab[32];
            int r0, c0, r1, c1, n = 0;
            if (sh_multi(a)) {
                sh_sel_rect(a, &r0, &c0, &r1, &c1);
            } else {
                r0 = 0; r1 = SHEET_ROWS - 1; c0 = CURC(a); c1 = CURC(a);
            }
            for (rr = r0; rr <= r1; rr++) {
                for (cc = c0; cc <= c1; cc++) {
                    int k = sheet_kind(sheet, rr, cc);
                    if (k == SHEET_NUMBER || k == SHEET_FORMULA) {
                        sum += sheet_value(sheet, rr, cc);
                        n++;
                    }
                }
            }
            sheet_fmt_num(sum, nb, (int)sizeof nb);
            if (n > 0) {
                sheet_fmt_num(sum / (double)n, ab, (int)sizeof ab);
                sys_snprintf(buf, sizeof buf, "Sum=%s  Avg=%s  Count=%d",
                             nb, ab, n);
            } else {
                sys_snprintf(buf, sizeof buf, "Sum=0  Count=0");
            }
        }
        { CRect pn = crect_make(px, r.y0 + 2, crect_w(&r) - 2 - (px - r.x0),
                                crect_h(&r) - 4);
          office_status_panel(s, &pn, buf, GFX_ALIGN_HCENTER); }
    }

    /* an open drop-down, over everything */
    if (a->menu_open >= 0) {
        int mx = o.x + a->menu_x, my = o.y + a->menu_y, mw, mh;
        CRect mr;
        ui_menu_measure(&a->menu, GFX_FONT_SYSTEM, &mw, &mh);
        mr = crect_make(mx, my, mw, mh);
        gfx_drop_shadow(s, &mr, 4, 90);
        ui_menu_draw(s, &a->menu, mx, my, GFX_FONT_SYSTEM);
    }
}

/* ---- input ------------------------------------------------------------ */
static void sh_click(WmWindow *win, int px, int py)
{
    SheetApp *a = (SheetApp *)wm_user(win);
    ShLayout L;
    int i;
    if (a == NULL) { return; }
    sh_layout(win, a, &L);

    if (a->menu_open >= 0) {
        int hit = ui_menu_hit(&a->menu, a->menu_x, a->menu_y,
                              GFX_FONT_SYSTEM, px, py);
        if (hit >= 0) {
            int cmd = a->menu.items[hit].id;
            a->menu_open = -1;
            sh_command(win, a, cmd);
            wm_invalidate(win, NULL);
            return;
        }
        a->menu_open = -1;
    }
    if (crect_contains(&L.menubar, px, py)) {
        for (i = 0; i < SH_MENU_N; i++) {
            CRect w = office_menu_word(&L.menubar, SH_MENU, i);
            if (crect_contains(&w, px, py)) {
                sh_open_menu(win, a, i);
                wm_invalidate(win, NULL);
                return;
            }
        }
        wm_invalidate(win, NULL);
        return;
    }
    for (i = 0; i < SB_N; i++) {
        if (crect_contains(&L.btn[i], px, py)) {
            sh_toolbar_cmd(win, a, i);      /* sh_command commits the edit */
            wm_invalidate(win, NULL);
            return;
        }
    }
    if (a->show_fbar && crect_contains(&L.fxbtn, px, py)) {
        sh_command(win, a, SC_FUNCS);
        wm_invalidate(win, NULL);
        return;
    }
    for (i = 0; i < SH_SHEETS; i++) {
        if (crect_contains(&L.tab[i], px, py)) {
            sh_commit(a);
            a->book = i;
            sys_snprintf(a->status, sizeof a->status, "Sheet%d", i + 1);
            wm_invalidate(win, NULL);
            return;
        }
    }
    if (crect_contains(&L.vbar, px, py)) {
        int part = ui_scrollbar_hit(&L.vbar, CTRUE, SHEET_ROWS, L.vis_r,
                                    TOPR(a), px, py);
        a->hot_sb_v = part;
        switch (part) {
        case UI_SB_LINE_UP:   TOPR(a)--; break;
        case UI_SB_LINE_DOWN: TOPR(a)++; break;
        case UI_SB_PAGE_UP:   TOPR(a) -= L.vis_r; break;
        case UI_SB_PAGE_DOWN: TOPR(a) += L.vis_r; break;
        case UI_SB_THUMB:     a->sb_drag_v = CTRUE; break;
        default: break;
        }
        if (TOPR(a) > SHEET_ROWS - L.vis_r) { TOPR(a) = SHEET_ROWS - L.vis_r; }
        if (TOPR(a) < 0) { TOPR(a) = 0; }
        sh_invalidate_scroll(win, &L);
        return;
    }
    if (crect_contains(&L.hbar, px, py)) {
        int part = ui_scrollbar_hit(&L.hbar, CFALSE, SHEET_COLS, L.vis_c,
                                    LEFTC(a), px, py);
        a->hot_sb_h = part;
        switch (part) {
        case UI_SB_LINE_UP:   LEFTC(a)--; break;
        case UI_SB_LINE_DOWN: LEFTC(a)++; break;
        case UI_SB_PAGE_UP:   LEFTC(a) -= L.vis_c; break;
        case UI_SB_PAGE_DOWN: LEFTC(a) += L.vis_c; break;
        case UI_SB_THUMB:     a->sb_drag_h = CTRUE; break;
        default: break;
        }
        if (LEFTC(a) > SHEET_COLS - L.vis_c) { LEFTC(a) = SHEET_COLS - L.vis_c; }
        if (LEFTC(a) < 0) { LEFTC(a) = 0; }
        sh_invalidate_scroll(win, &L);
        return;
    }
    if (crect_contains(&L.grid, px, py)) {
        int gx = px - L.grid.x0 - SH_ROWHDR_W;
        int gy = py - L.grid.y0 - SH_COLHDR_H;
        if (gx >= 0 && gy >= 0) {
            int col = LEFTC(a) + gx / SH_COL_W;
            int row = TOPR(a) + gy / SH_ROW_H;
            if (col < SHEET_COLS && row < SHEET_ROWS) {
                sh_commit(a);
                CURR(a) = row;
                CURC(a) = col;
                SELR(a) = row;              /* anchor a possible drag  */
                SELC(a) = col;
                a->selecting = CTRUE;
                sheet_cellname(row, col, a->status, (int)sizeof a->status);
                wm_invalidate(win, NULL);
            }
        }
    }
}

static void sh_motion(WmWindow *win, int px, int py)
{
    SheetApp *a = (SheetApp *)wm_user(win);
    ShLayout L;
    int i, hov = -1;
    if (a == NULL) { return; }
    sh_layout(win, a, &L);
    if (a->selecting && crect_contains(&L.grid, px, py)) {
        int gx = px - L.grid.x0 - SH_ROWHDR_W;
        int gy = py - L.grid.y0 - SH_COLHDR_H;
        if (gx >= 0 && gy >= 0) {
            int col = LEFTC(a) + gx / SH_COL_W;
            int row = TOPR(a) + gy / SH_ROW_H;
            if (col >= SHEET_COLS) { col = SHEET_COLS - 1; }
            if (row >= SHEET_ROWS) { row = SHEET_ROWS - 1; }
            if (row != CURR(a) || col != CURC(a)) {
                CURR(a) = row;
                CURC(a) = col;
                wm_invalidate(win, NULL);
            }
        }
        return;
    }
    if (a->sb_drag_v) {
        int was = TOPR(a);
        TOPR(a) = ui_scroll_pos_from_coord(crect_h(&L.vbar), SHEET_ROWS,
                                           L.vis_r, py - L.vbar.y0);
        /* The grid and its bars moved; the menu bar, the toolbar, the
         * formula bar, the sheet tabs and the status line did not. On the
         * transition only -- a pixel of thumb travel that lands on the same
         * row changes nothing. */
        if (TOPR(a) != was) { sh_invalidate_scroll(win, &L); }
        return;
    }
    if (a->sb_drag_h) {
        int was = LEFTC(a);
        LEFTC(a) = ui_scroll_pos_from_coord(crect_w(&L.hbar), SHEET_COLS,
                                            L.vis_c, px - L.hbar.x0);
        if (LEFTC(a) != was) { sh_invalidate_scroll(win, &L); }
        return;
    }
    if (a->menu_open >= 0 && crect_contains(&L.menubar, px, py)) {
        for (i = 0; i < SH_MENU_N; i++) {
            CRect w = office_menu_word(&L.menubar, SH_MENU, i);
            if (crect_contains(&w, px, py) && i != a->menu_open) {
                sh_open_menu(win, a, i);
                wm_invalidate(win, NULL);
                return;
            }
        }
    }
    if (a->menu_open >= 0) {
        int hit = ui_menu_hit(&a->menu, a->menu_x, a->menu_y,
                              GFX_FONT_SYSTEM, px, py);
        if (hit != a->menu.highlight) {
            int was = a->menu.highlight;
            a->menu.highlight = hit;
            ui_menu_repaint(win, &a->menu, a->menu_x, a->menu_y,
                            GFX_FONT_SYSTEM, was, hit);
        }
        return;
    }
    for (i = 0; i < SB_N; i++) {
        if (crect_contains(&L.btn[i], px, py)) { hov = i; break; }
    }
    if (hov < 0 && a->show_fbar && crect_contains(&L.fxbtn, px, py)) {
        hov = SB_N;                       /* the fx button */
    }
    if (hov != a->hover_tb) {
        a->hover_tb = hov;
        wm_invalidate(win, NULL);
    }
}

/*
 * Typing into a cell repainted the WHOLE window: 290,625 pixels per
 * character, 108% of the client, every cell of the grid redrawn although
 * none of them had changed. Measured by --cost-demo off the shell's own
 * frame counter.
 *
 * Nothing is re-evaluated until an edit is COMMITTED, so while somebody is
 * typing the only things that differ are the cell they are typing into, the
 * formula bar echoing it, the name box and the status line. Moving the
 * cursor, committing and clearing still repaint whole -- each of those
 * really can change any cell in the book, and none of them is the key
 * pressed thirty times in a row. (Scrolling has its own narrowing now, in
 * sh_invalidate_scroll: it changes every cell but no chrome.)
 */
static void sh_invalidate_edit(WmWindow *win, SheetApp *a, const ShLayout *L)
{
    CPoint o = wm_client_origin(win);
    CRect r;
    if (a->show_fbar) {
        r = crect_offset(&L->fbar, o.x, o.y);
        wm_invalidate(win, &r);
        r = crect_offset(&L->namebox, o.x, o.y);
        wm_invalidate(win, &r);
    }
    if (a->show_status) {
        r = crect_offset(&L->status, o.x, o.y);
        wm_invalidate(win, &r);
    }
    /* Two pixels out: an edited cell wears an outline that sits outside the
     * cell rectangle proper. */
    r = sh_cell_rect(L, a, CURR(a), CURC(a));
    r = crect_inset(&r, -2);
    r = crect_offset(&r, o.x, o.y);
    wm_invalidate(win, &r);
}

static cbool sh_key(WmWindow *win, int key, int ch, int mods)
{
    SheetApp *a = (SheetApp *)wm_user(win);
    ShLayout L;
    if (a == NULL) { return CFALSE; }
    /*
     * The menu bar, from the keyboard: F10 opens it, the arrows walk it,
     * Enter chooses, Esc closes. These menus were mouse-only, and on the
     * target the mouse is a driver somebody may not have loaded.
     */
    {
        OfficeMenuNav nav;
        ShLayout ML;
        int mcmd = -1;
        sh_layout(win, a, &ML);
        nav.open = &a->menu_open; nav.x = &a->menu_x; nav.y = &a->menu_y;
        nav.menu = &a->menu; nav.names = SH_MENU; nav.count = SH_MENU_N;
        nav.bar = ML.menubar; nav.build = sh_menu_build_cb; nav.user = a;
        if (office_menu_key_win(win, &nav, key, &mcmd)) {
            if (mcmd >= 0) { sh_command(win, a, mcmd); wm_invalidate(win, NULL); }
            return CTRUE;
        }
    }
    sh_layout(win, a, &L);
    /* Where the cursor and the view were before this key, so the exit can
     * tell an edit-in-place from anything that moves or recalculates. */
    {
    int was_r = CURR(a), was_c = CURC(a);
    int was_t = TOPR(a), was_l = LEFTC(a);
    cbool book = CFALSE;

    /* Ctrl+Z / Ctrl+Y. Restoring a cell re-evaluates the book, so a formula
     * that depended on it recovers too -- which is the whole point of undoing
     * a spreadsheet rather than a document. */
    if (SH_CTRL(key, ch, mods, 26, 'z')) {          /* Ctrl+Z */
        int bk = 0, r = 0, c = 0;
        const char *t = cundo_undo(&a->undo, &bk, &r, &c);
        if (t != NULL) {
            a->editing = CFALSE;
            a->book = bk;
            sheet_set(&a->sh[bk], r, c, t);
            CURR(a) = r; CURC(a) = c;
            sys_strlcpy(a->status, "Undo", sizeof a->status);
        } else {
            sys_strlcpy(a->status, "Nothing to undo", sizeof a->status);
        }
        wm_invalidate(win, NULL);
        return CTRUE;
    }
    if (SH_CTRL(key, ch, mods, 25, 'y')) {          /* Ctrl+Y */
        int bk = 0, r = 0, c = 0;
        const char *t = cundo_redo(&a->undo, &bk, &r, &c);
        if (t != NULL) {
            a->editing = CFALSE;
            a->book = bk;
            sheet_set(&a->sh[bk], r, c, t);
            CURR(a) = r; CURC(a) = c;
            sys_strlcpy(a->status, "Redo", sizeof a->status);
        } else {
            sys_strlcpy(a->status, "Nothing to redo", sizeof a->status);
        }
        wm_invalidate(win, NULL);
        return CTRUE;
    }
    /*
     * The rest of the standard set, routed through the same sh_command the
     * menus call so there is no second implementation to drift out of step.
     *
     * Only while NOT editing a cell: mid-edit these are characters, and a
     * Ctrl+C that cut the cell out from under a half-typed formula would be
     * the sort of surprise that costs an afternoon.
     */
    if (!a->editing) {
        if (SH_CTRL(key, ch, mods, 24, 'x')) { sh_command(win, a, SC_CUT);   return CTRUE; }
        if (SH_CTRL(key, ch, mods, 3,  'c')) { sh_command(win, a, SC_COPY);  return CTRUE; }
        if (SH_CTRL(key, ch, mods, 22, 'v')) { sh_command(win, a, SC_PASTE); return CTRUE; }
        if (SH_CTRL(key, ch, mods, 19, 's')) { sh_command(win, a, SC_SAVE);  return CTRUE; }
        if (SH_CTRL(key, ch, mods, 15, 'o')) { sh_command(win, a, SC_OPEN);  return CTRUE; }
    }

    switch (key) {
    case PLAT_KEY_UP:    sh_move(a, &L, -1, 0); break;
    case PLAT_KEY_DOWN:  sh_move(a, &L, +1, 0); break;
    case PLAT_KEY_LEFT:  if (!a->editing) { sh_move(a, &L, 0, -1); } break;
    case PLAT_KEY_RIGHT: if (!a->editing) { sh_move(a, &L, 0, +1); } break;
    case PLAT_KEY_PGUP:  sh_move(a, &L, -L.vis_r, 0); break;
    case PLAT_KEY_PGDN:  sh_move(a, &L, +L.vis_r, 0); break;
    case PLAT_KEY_HOME:  sh_commit(a); book = CTRUE;
                         CURC(a) = 0; LEFTC(a) = 0; break;
    case PLAT_KEY_ENTER:
        if (a->editing) { sh_commit(a); book = CTRUE; sh_move(a, &L, +1, 0); }
        else { sh_begin_edit(a, CTRUE); }
        break;
    case PLAT_KEY_TAB:   sh_commit(a); book = CTRUE;
                         sh_move(a, &L, 0, +1); break;
    case PLAT_KEY_ESC:
        a->editing = CFALSE; a->edit[0] = '\0'; a->edit_len = 0;
        break;
    case PLAT_KEY_F2:    sh_begin_edit(a, CTRUE); break;
    case PLAT_KEY_DELETE:
        a->editing = CFALSE;
        sh_set(a, CURR(a), CURC(a), "");
        book = CTRUE;
        sys_strlcpy(a->status, "Cleared", sizeof a->status);
        break;
    case PLAT_KEY_BACKSP:
        if (a->editing && a->edit_len > 0) { a->edit[--a->edit_len] = '\0'; }
        break;
    default:
        if (ch >= 32 && ch < 127) {
            if (!a->editing) { sh_begin_edit(a, CFALSE); }
            if (a->edit_len < (int)sizeof a->edit - 1) {
                a->edit[a->edit_len++] = (char)ch;
                a->edit[a->edit_len] = '\0';
            }
            break;
        }
        return CFALSE;
    }
    sh_scroll_to_cursor(a, &L);
    if (!book && CURR(a) == was_r && CURC(a) == was_c &&
        TOPR(a) == was_t && LEFTC(a) == was_l) {
        sh_invalidate_edit(win, a, &L);
    } else {
        wm_invalidate(win, NULL);
    }
    }
    return CTRUE;
}

static cbool sheet_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    switch (msg) {
    case WM_MSG_CLOSE:       return sh_on_close(win);
    case WM_MSG_QUERY_UNSAVED: return sh_has_unsaved(win);
    case WM_MSG_PAINT:       sh_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: sh_click(win, (int)a, (int)b);      return CTRUE;
    case WM_MSG_MOUSEMOVE:   sh_motion(win, (int)a, (int)b);     return CTRUE;
    case WM_MSG_LBUTTONUP: {
        SheetApp *sa = (SheetApp *)wm_user(win);
        if (sa != NULL) {
            /* Letting go of either bar puts it back: ui_scrollbar_draw takes
             * the hot part and draws it active, and nothing repainted the
             * bars here. Hidden for as long as every motion event during the
             * drag repainted the window. */
            cbool was_sb = (sa->sb_drag_v || sa->sb_drag_h ||
                            sa->hot_sb_v != UI_SB_NONE ||
                            sa->hot_sb_h != UI_SB_NONE) ? CTRUE : CFALSE;
            sa->selecting = CFALSE;
            sa->sb_drag_v = CFALSE; sa->sb_drag_h = CFALSE;
            sa->hot_sb_v = UI_SB_NONE; sa->hot_sb_h = UI_SB_NONE;
            if (was_sb) {
                /* The two BARS only: a release changes which part of a bar
                 * is drawn held, and moves no cell. */
                ShLayout L;
                CPoint o = wm_client_origin(win);
                CRect r;
                sh_layout(win, sa, &L);
                r = crect_offset(&L.vbar, o.x, o.y);
                wm_invalidate(win, &r);
                r = crect_offset(&L.hbar, o.x, o.y);
                wm_invalidate(win, &r);
            }
        }
        return CTRUE;
    }
    case WM_MSG_MOUSEWHEEL: {
        SheetApp *sa = (SheetApp *)wm_user(win);
        ShLayout L;
        if (sa == NULL) { return CFALSE; }
        sh_layout(win, sa, &L);
        if (ui_scroll_wheel(&TOPR(sa), (int)a, SHEET_ROWS, L.vis_r)) {
            sh_invalidate_scroll(win, &L);
        }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:
        return sh_key(win, (int)a, (int)b,
                      (param != NULL) ? *(const int *)param : 0);
    case WM_MSG_DESTROY: {
        SheetApp *sa = (SheetApp *)wm_user(win);
        if (sa != NULL) { sys_free(sa, (cu32)sizeof(SheetApp)); }
        return CTRUE;
    }
    default: return CFALSE;
    }
}

static WmWindow *sh_open_window(void)
{
    SheetApp *a;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int i, cw = 620, ch = 460;

    a = (SheetApp *)sys_calloc(1, (cu32)sizeof(SheetApp));
    if (a == NULL) { SYS_LOGE("app", "sheet: OOM"); return NULL; }
    for (i = 0; i < SH_SHEETS; i++) { sheet_clear(&a->sh[i]); }
    a->hover_tb = -1;
    a->menu_open = -1;
    a->show_fbar = CTRUE;
    a->show_status = CTRUE;
    sys_strlcpy(a->path, "BUDGET.CSV", sizeof a->path);
    sh_sample(a);
    a->saved_sum = sh_doc_sum(a);   /* the sample is not unsaved work */

    plat_video_info(&vi);
    /* The WORK area, not the screen: centring a fixed height in the screen
     * puts half the excess under the taskbar, which at 640x480 was exactly
     * this window's status bar -- the row carrying Sum, Avg and Count. */
    frame = wm_place_centered(cw, ch);

    w = wm_create("Book1 - CastaliaSheet", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_MAXIMIZE | WM_STYLE_BORDER, sheet_proc, a);
    if (w == NULL) { sys_free(a, (cu32)sizeof(SheetApp)); return NULL; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened CastaliaSheet");
    return w;
}

const char *app_sheet_cur_raw(WmWindow *win)
{
    SheetApp *a = (SheetApp *)wm_user(win);
    if (a == NULL) { return ""; }
    return sheet_raw(CURSH(a), CURR(a), CURC(a));
}

void app_sheet_open(void) { (void)sh_open_window(); }

/* Open the spreadsheet on a CSV -- what a double-click on one does. */
void app_sheet_open_file(const char *path)
{
    WmWindow *w = sh_open_window();
    SheetApp *a = (w != NULL) ? (SheetApp *)wm_user(w) : NULL;
    if (a == NULL || path == NULL || path[0] == '\0') { return; }
    sh_open_file(a, path);
    wm_invalidate(w, NULL);
}
