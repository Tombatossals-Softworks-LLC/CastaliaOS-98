/*
 * app_notepad.c - CastaliaOS Notepad: a plain-text multiline editor.
 *
 * Phase 3 app. The document is a flat char buffer with '\n' line breaks and a
 * caret index; line/column are derived by scanning. Features: type/insert,
 * Backspace/Delete, arrow/Home/End/PageUp/PageDown navigation, vertical and
 * horizontal scrolling, click-to-position, New/Open/Save via prompt dialogs,
 * and an Ln:Col status bar. Fixed-pitch layout using the system font.
 *
 * Open/Save use the platform file API, so it works on host and DOS alike.
 */
#include "apps.h"
#include "wrap_core.h"
#include "undo_core.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "office_ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include "castalia/clip.h"

#include <string.h>

/* Ctrl-shortcut detection working on both backends (DOS delivers Ctrl+letter as
 * an ASCII control code in 'key'; synthetic input may carry the letter + CTRL). */
#define NP_CTRL(key, ch, mods, code, letter) \
    ((key) == (code) || \
     (((mods) & PLAT_MOD_CTRL) && ((ch) == (letter) || (ch) == (letter) - 32)))

#define NP_MAX       16384
/* The most a DOCUMENT can be: the buffer keeps one byte for the terminator.
 * Named because it is a promise to the person editing -- it is the number the
 * refusal in np_load_file quotes, and the ceiling everything else stops at. */
#define NP_FILE_MAX  (NP_MAX - 1)
#define NP_TOOLBAR   26
#define NP_STATUS    16
#define NP_LINE_H    10
#define NP_CHAR_W    6
#define NP_TAB       4
#define NP_MAX_VROWS 2048   /* wrapped visual rows tracked for display/nav */

typedef struct {
    char  buf[NP_MAX];
    UndoStack undo;   /* Ctrl+Z / Ctrl+Y over the edits below            */
    int   len;
    int   caret;
    int   top_line;   /* first visible line              */
    int   left_col;   /* first visible column            */
    char  path[CASTALIA_MAX_PATH];
    cbool modified;
    char  status[80];
    char  search[64];
    int   match_start;   /* highlighted match, or -1 */
    int   match_len;
    cbool wrap;          /* word-wrap long lines to the text width */
    int   sel_anchor;    /* fixed end of the selection, or -1 if none */
    cbool selecting;     /* a mouse drag-select is in progress */
    int   hot_sb;        /* active scroll-bar part (UI_SB_*)       */
    cbool sb_drag;       /* dragging the scroll thumb              */
    int   menu_open;     /* open menu index, or -1                 */
    int   menu_x, menu_y;
    UiMenu menu;
    UiHot hot_tb;        /* toolbar button under the pointer       */
} Notepad;

/*
 * Menu commands.
 *
 * Notepad had no menu bar at all -- five toolbar buttons and nothing else --
 * so its eight keyboard shortcuts were undiscoverable: nowhere in the program
 * said that Ctrl+Z would undo. Every other app in the suite has a menu; the
 * one people actually type into did not.
 *
 * The commands were also written inline in the click handler, which is why
 * the toolbar and the keyboard had grown separate copies of "new document".
 * They go through np_command now, so the menu, the buttons and the keys are
 * three doors into one room.
 */
enum {
    NC_NEW = 100, NC_OPEN, NC_SAVE, NC_SAVEAS, NC_CLOSE,
    NC_UNDO, NC_REDO, NC_CUT, NC_COPY, NC_PASTE, NC_SELALL, NC_DELETE,
    NC_FIND, NC_FINDNEXT, NC_WRAP, NC_TIME, NC_ABOUT
};

static const char *const NP_MENU[] = { "File", "Edit", "Search", "Help" };
#define NP_MENU_N 4

/* The key handler runs the same commands the menu does, and comes first in
 * the file; the alternative is moving four hundred lines to please the
 * compiler. */
static void np_command(struct WmWindow *win, Notepad *n, int cmd);
static cbool np_on_close(struct WmWindow *win);
static cbool np_has_unsaved(struct WmWindow *win);
static void np_menu_build_cb(void *user, int idx);

/* Word-wrap layout: byte index at which each visual row begins. Rebuilt from
 * the buffer + column width each time it is needed (rendering / navigation).
 * A single shared buffer keeps this off the (small) DOS stack. */
static int g_vrow[NP_MAX_VROWS];

/* ---- buffer helpers -------------------------------------------------- */
/* Line boundaries and the wrap layout are wrap_core.c, where
 * tests/test_wrap.c can reach them; these just supply the buffer. */
static int np_line_start(Notepad *n, int idx)
{
    return wrap_line_start(n->buf, idx);
}
static int np_line_end(Notepad *n, int idx)
{
    return wrap_line_end(n->buf, n->len, idx);
}
static int np_caret_line(Notepad *n)
{
    int i, ln = 0;
    for (i = 0; i < n->caret; i++) { if (n->buf[i] == '\n') { ln++; } }
    return ln;
}
static int np_caret_col(Notepad *n)
{
    return n->caret - np_line_start(n, n->caret);
}

/* ---- word-wrap layout ------------------------------------------------ */
/* Build the visual-row table for the current wrap width. Returns the count.
 * Each logical line ('\n'-terminated) is greedily broken at the last space
 * that fits, falling back to a hard break for long words. */
static int np_build_vrows(Notepad *n, int vis_cols)
{
    return wrap_build(n->buf, n->len, vis_cols, g_vrow, NP_MAX_VROWS);
}

static int np_vrow_end(Notepad *n, int r, int count)
{
    return wrap_row_end(n->buf, n->len, g_vrow, count, r);
}

static int np_caret_vrow(Notepad *n, int count)
{
    return wrap_row_of(n->buf, n->len, g_vrow, count, n->caret);
}

/* ---- selection ------------------------------------------------------- */
static cbool np_has_sel(Notepad *n)
{
    return (n->sel_anchor >= 0 && n->sel_anchor != n->caret) ? CTRUE : CFALSE;
}
static int np_sel_lo(Notepad *n)
{
    if (n->sel_anchor < 0) { return n->caret; }
    return (n->sel_anchor < n->caret) ? n->sel_anchor : n->caret;
}
static int np_sel_hi(Notepad *n)
{
    if (n->sel_anchor < 0) { return n->caret; }
    return (n->sel_anchor > n->caret) ? n->sel_anchor : n->caret;
}
static void np_clear_sel(Notepad *n) { n->sel_anchor = -1; }

static void np_delete_range(Notepad *n, int lo, int hi)
{
    int i, cnt;
    if (lo < 0) { lo = 0; }
    if (hi > n->len) { hi = n->len; }
    if (hi <= lo) { return; }
    cnt = hi - lo;
    /* Recorded BEFORE the characters go, because undoing a delete needs
     * them -- a record of just the position and length restores the right
     * number of wrong characters. */
    undo_record(&n->undo, UNDO_DELETE, lo, &n->buf[lo], cnt);
    for (i = lo; i + cnt <= n->len; i++) { n->buf[i] = n->buf[i + cnt]; }
    n->len -= cnt;
    n->buf[n->len] = '\0';
    n->modified = CTRUE;
    n->match_start = -1;
}

/* Delete the current selection (if any); leaves the caret at its start. */
static cbool np_delete_sel(Notepad *n)
{
    int lo, hi;
    if (!np_has_sel(n)) { return CFALSE; }
    lo = np_sel_lo(n); hi = np_sel_hi(n);
    np_delete_range(n, lo, hi);
    n->caret = lo;
    np_clear_sel(n);
    return CTRUE;
}
static void np_copy_sel(Notepad *n)
{
    if (!np_has_sel(n)) { return; }
    clip_set_text(&n->buf[np_sel_lo(n)], np_sel_hi(n) - np_sel_lo(n));
}
static void np_select_all(Notepad *n) { n->sel_anchor = 0; n->caret = n->len; }

static void np_insert(Notepad *n, int c)
{
    int i;
    np_delete_sel(n); /* typing replaces any selection */
    if (n->len >= NP_FILE_MAX) {
        /* Typing that stops happening is the same lie one keystroke wide:
         * the caret sits still and the character is gone. Say so. */
        sys_snprintf(n->status, sizeof(n->status),
                     "Document is full (%d characters)", NP_FILE_MAX);
        return;
    }
    for (i = n->len; i > n->caret; i--) { n->buf[i] = n->buf[i - 1]; }
    n->buf[n->caret] = (char)c;
    { char one = (char)c; undo_record(&n->undo, UNDO_INSERT, n->caret, &one, 1); }
    n->len++;
    n->caret++;
    n->buf[n->len] = '\0';
    n->modified = CTRUE;
    n->match_start = -1;
}
static void np_delete_at(Notepad *n, int idx)
{
    int i;
    if (idx < 0 || idx >= n->len) { return; }
    undo_record(&n->undo, UNDO_DELETE, idx, &n->buf[idx], 1);
    for (i = idx; i < n->len - 1; i++) { n->buf[i] = n->buf[i + 1]; }
    n->len--;
    n->buf[n->len] = '\0';
    n->modified = CTRUE;
    n->match_start = -1;
}

/* Paste clipboard text at the caret, replacing any selection. Only printable
 * characters plus newline/tab are inserted, keeping the buffer clean. */
static void np_paste(Notepad *n)
{
    const char *t = clip_get_text();
    int dropped = 0;
    np_delete_sel(n);
    for (; *t != '\0'; t++) {
        unsigned char c = (unsigned char)*t;
        int i;
        if (!(c == '\n' || c == '\t' || (c >= 32 && c < 127))) { continue; }
        if (n->len >= NP_FILE_MAX) { dropped++; continue; }
        for (i = n->len; i > n->caret; i--) { n->buf[i] = n->buf[i - 1]; }
        n->buf[n->caret++] = (char)c;
        n->len++;
    }
    n->buf[n->len] = '\0';
    n->modified = CTRUE;
    n->match_start = -1;
    /* A paste that ran out of room stopped mid-text. Silently short is how
     * you find out later, from the file. */
    if (dropped > 0) {
        sys_snprintf(n->status, sizeof(n->status),
                     "Document full -- %d character%s not pasted", dropped,
                     (dropped == 1) ? "" : "s");
    }
}

/* ---- layout ---------------------------------------------------------- */
typedef struct {
    CRect menubar;
    CRect toolbar, btn_new, btn_open, btn_save, btn_find, btn_wrap, text, status;
    CRect vbar;
    int   vis_lines, vis_cols;
} NpLayout;

/* Defined with the other invalidate helpers, below the painter; the click
 * handler needs it and lives above them. */
static void np_invalidate_scroll(struct WmWindow *win, const NpLayout *L);

/* The toolbar, in one order, so what is drawn and what is hit cannot
 * disagree about which button is which. */
enum { NPB_NEW = 0, NPB_OPEN, NPB_SAVE, NPB_FIND, NPB_WRAP, NPB_COUNT };

static CRect np_btn_rect(const NpLayout *L, int i)
{
    switch (i) {
    case NPB_NEW:  return L->btn_new;
    case NPB_OPEN: return L->btn_open;
    case NPB_SAVE: return L->btn_save;
    case NPB_FIND: return L->btn_find;
    default:       return L->btn_wrap;
    }
}

static int np_tb_at(const NpLayout *L, int px, int py)
{
    int i;
    for (i = 0; i < NPB_COUNT; i++) {
        CRect r = np_btn_rect(L, i);
        if (crect_contains(&r, px, py)) { return i; }
    }
    return -1;
}

static void np_layout(int cw, int ch, NpLayout *L)
{
    int tb = OF_MENUBAR_H;          /* the toolbar now starts below the menu */
    L->menubar  = crect_make(0, 0, cw, OF_MENUBAR_H);
    L->toolbar  = crect_make(0, tb, cw, NP_TOOLBAR);
    L->btn_new  = crect_make(4, tb + 3, 42, NP_TOOLBAR - 6);
    L->btn_open = crect_make(50, tb + 3, 48, NP_TOOLBAR - 6);
    L->btn_save = crect_make(102, tb + 3, 48, NP_TOOLBAR - 6);
    L->btn_find = crect_make(154, tb + 3, 44, NP_TOOLBAR - 6);
    L->btn_wrap = crect_make(202, tb + 3, 48, NP_TOOLBAR - 6);
    {
        int ty = tb + NP_TOOLBAR;
        int th = ch - ty - NP_STATUS;
        if (th < NP_LINE_H) { th = NP_LINE_H; }
        L->text   = crect_make(0, ty, cw - UI_SB_W, th);
        L->vbar   = crect_make(cw - UI_SB_W, ty, UI_SB_W, th);
        L->status = crect_make(0, ch - NP_STATUS, cw, NP_STATUS);
        L->vis_lines = (th - 4) / NP_LINE_H;
        L->vis_cols  = (crect_w(&L->text) - 8) / NP_CHAR_W;
        if (L->vis_lines < 1) { L->vis_lines = 1; }
        if (L->vis_cols < 1) { L->vis_cols = 1; }
    }
}

/* How many rows the document occupies right now: visual rows when wrapped,
 * logical lines otherwise. The scroll bar and the caret logic share it. */
static int np_total_rows(Notepad *n, int vis_cols)
{
    int i, lines = 1;
    if (n->wrap) { return np_build_vrows(n, vis_cols); }
    for (i = 0; i < n->len; i++) {
        if (n->buf[i] == '\n') { lines++; }
    }
    return lines;
}

static void np_scroll_to_caret(Notepad *n, NpLayout *L)
{
    int line, col;
    if (n->wrap) {
        /* top_line is a visual-row index; no horizontal scroll when wrapped. */
        int count = np_build_vrows(n, L->vis_cols);
        int r = np_caret_vrow(n, count);
        if (r < n->top_line) { n->top_line = r; }
        if (r >= n->top_line + L->vis_lines) { n->top_line = r - L->vis_lines + 1; }
        if (n->top_line < 0) { n->top_line = 0; }
        n->left_col = 0;
        return;
    }
    line = np_caret_line(n);
    col = np_caret_col(n);
    if (line < n->top_line) { n->top_line = line; }
    if (line >= n->top_line + L->vis_lines) { n->top_line = line - L->vis_lines + 1; }
    if (col < n->left_col) { n->left_col = col; }
    if (col >= n->left_col + L->vis_cols) { n->left_col = col - L->vis_cols + 1; }
    if (n->top_line < 0) { n->top_line = 0; }
    if (n->left_col < 0) { n->left_col = 0; }
}

/* ---- file I/O -------------------------------------------------------- */
/*
 * A file too big to hold is REFUSED, not truncated.
 *
 * This read the first 16K of whatever it was pointed at, set the status line
 * to "Opened FILE.TXT (16383 bytes)" -- the same sentence a successful open
 * gives, and for every file that fits, 16383 really would be its size -- and
 * then Save wrote that buffer back over the original. A 20K file came out of
 * it 16K long. Nothing at any point said a byte had been dropped.
 *
 * Every other reader in the system already refuses or admits it: the
 * console's fc and the File Compare window say the size and decline, and the
 * Viewer, which cannot write at all, still prints "(truncated)". The one
 * program that writes back was the one that said nothing.
 *
 * So the ceiling is checked against the file's real size before a byte is
 * read, and the path is NOT adopted on refusal -- Ctrl+S afterwards offers
 * UNTITLED.TXT, so an empty document cannot be saved over the file that was
 * too big to open.
 */
static void np_load_file(Notepad *n, const char *path)
{
    PlatFile *f;
    long size = plat_file_size(path);

    if (size < 0) {
        sys_snprintf(n->status, sizeof(n->status), "Cannot open %s",
                     ui_path_base(path));
        return;
    }
    if (size > (long)NP_FILE_MAX) {
        sys_snprintf(n->status, sizeof(n->status),
                     "%s is %ld bytes -- too large to edit (limit %d)",
                     ui_path_base(path), size, NP_FILE_MAX);
        return;
    }
    f = plat_fopen(path, "rb");
    if (f == NULL) {
        sys_snprintf(n->status, sizeof(n->status), "Cannot open %s",
                     ui_path_base(path));
        return;
    }
    n->len = (int)plat_fread(f, n->buf, (cu32)NP_FILE_MAX);
    plat_fclose(f);
    if (n->len < 0) { n->len = 0; }
    n->buf[n->len] = '\0';
    n->caret = 0;
    n->top_line = 0;
    n->left_col = 0;
    n->modified = CFALSE;
    n->match_start = -1;
    n->sel_anchor = -1;
    n->selecting = CFALSE;
    sys_strlcpy(n->path, path, sizeof(n->path));
    sys_snprintf(n->status, sizeof(n->status), "Opened %s (%d bytes)",
                 ui_path_base(path), n->len);
}

static void np_save_file(Notepad *n, const char *path)
{
    PlatFile *f = plat_fopen(path, "wb");
    cu32 w;
    if (f == NULL) {
        sys_snprintf(n->status, sizeof(n->status), "Cannot write %s",
                     ui_path_base(path));
        return;
    }
    w = plat_fwrite(f, n->buf, (cu32)n->len);
    plat_fclose(f);
    sys_strlcpy(n->path, path, sizeof(n->path));
    n->modified = CFALSE;
    sys_snprintf(n->status, sizeof(n->status), "Saved %s (%lu bytes)",
                 ui_path_base(path), (unsigned long)w);
}

/* ---- find ------------------------------------------------------------ */
static int np_cilower(int c) { return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c; }

static int np_find_from(Notepad *n, int from)
{
    int slen = (int)sys_strnlen(n->search, sizeof(n->search));
    int i, j;
    if (slen == 0) { return -1; }
    for (i = from; i + slen <= n->len; i++) {
        for (j = 0; j < slen; j++) {
            if (np_cilower(n->buf[i + j]) != np_cilower(n->search[j])) { break; }
        }
        if (j == slen) { return i; }
    }
    return -1;
}

static void np_find_next(Notepad *n)
{
    int slen = (int)sys_strnlen(n->search, sizeof(n->search));
    int pos;
    if (slen == 0) { return; }
    pos = np_find_from(n, n->caret);
    if (pos < 0) { pos = np_find_from(n, 0); } /* wrap around */
    if (pos >= 0) {
        n->match_start = pos;
        n->match_len = slen;
        n->caret = pos + slen;
        sys_snprintf(n->status, sizeof(n->status), "Found '%s'", n->search);
    } else {
        n->match_start = -1;
        sys_snprintf(n->status, sizeof(n->status), "'%s' not found", n->search);
    }
}

/* ---- dialog callbacks ------------------------------------------------ */
static void np_on_open(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    Notepad *n = (Notepad *)wm_user(win);
    if (ok && n != NULL && text[0] != '\0') {
        np_load_file(n, text);
        wm_invalidate(win, NULL);
    }
}
static void np_on_save(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    Notepad *n = (Notepad *)wm_user(win);
    if (ok && n != NULL && text[0] != '\0') {
        np_save_file(n, text);
        wm_invalidate(win, NULL);
    }
}
static void np_on_find(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    Notepad *n = (Notepad *)wm_user(win);
    CRect client;
    NpLayout L;
    if (!ok || n == NULL || text[0] == '\0') { return; }
    sys_strlcpy(n->search, text, sizeof(n->search));
    np_find_next(n);
    client = wm_client_rect(win);
    np_layout(crect_w(&client), crect_h(&client), &L);
    np_scroll_to_caret(n, &L);
    wm_invalidate(win, NULL);
}

/* ---- painting -------------------------------------------------------- */
static void np_paint(WmWindow *win, GfxSurface *s)
{
    Notepad *n = (Notepad *)wm_user(win);
    const UiPalette *p = ui_palette();
    CRect client = wm_client_rect(win);
    CPoint o = wm_client_origin(win);
    NpLayout L;
    CRect r;
    int li, idx, y;
    int slo, shi;
    cbool hassel;
    CColor sel_band, sel_ink;
    if (n == NULL) { return; }
    np_layout(crect_w(&client), crect_h(&client), &L);
    slo = np_sel_lo(n); shi = np_sel_hi(n); hassel = np_has_sel(n);
    /* A selection in a window nobody is typing into goes quiet, the same way
     * the File Manager's rows and every list control already do. */
    ui_sel_colors(wm_has_focus(win), &sel_band, &sel_ink);

    /* Menu bar, then the toolbar under it. */
    r = crect_offset(&L.menubar, o.x, o.y);
    office_menubar(s, &r, NP_MENU, NP_MENU_N, n->menu_open);

    /* Toolbar. */
    r = crect_offset(&L.toolbar, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    r = crect_offset(&L.btn_new, o.x, o.y);
    ui_draw_button(s, &r, "New",  ui_hot_state(&n->hot_tb, NPB_NEW,  UI_BTN_NORMAL));
    r = crect_offset(&L.btn_open, o.x, o.y);
    ui_draw_button(s, &r, "Open", ui_hot_state(&n->hot_tb, NPB_OPEN, UI_BTN_NORMAL));
    r = crect_offset(&L.btn_save, o.x, o.y);
    ui_draw_button(s, &r, "Save", ui_hot_state(&n->hot_tb, NPB_SAVE, UI_BTN_NORMAL));
    r = crect_offset(&L.btn_find, o.x, o.y);
    ui_draw_button(s, &r, "Find", ui_hot_state(&n->hot_tb, NPB_FIND, UI_BTN_NORMAL));
    r = crect_offset(&L.btn_wrap, o.x, o.y);
    ui_draw_button(s, &r, "Wrap",
                   ui_hot_state(&n->hot_tb, NPB_WRAP,
                                n->wrap ? UI_BTN_PRESSED : UI_BTN_NORMAL));

    /* Text well. */
    r = crect_offset(&L.text, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, GFX_RGB(0xFF,0xFF,0xFF));

    if (n->wrap) {
        /* Wrapped: render visual rows from the vrow table. */
        int count = np_build_vrows(n, L.vis_cols);
        int vi;
        for (vi = 0; vi < L.vis_lines; vi++) {
            int row = n->top_line + vi;
            int rs, re, k;
            int x = r.x0 + 4;
            int ry = r.y0 + 2 + vi * NP_LINE_H;
            if (row >= count) { break; }
            rs = g_vrow[row];
            re = np_vrow_end(n, row, count);
            for (k = rs; k < re; k++) {
                int cx = x + (k - rs) * NP_CHAR_W;
                char ch[2];
                CColor tcol = p->text;
                cbool hl_on = (hassel && k >= slo && k < shi) ||
                    (n->match_start >= 0 && k >= n->match_start &&
                     k < n->match_start + n->match_len);
                if (hl_on) {
                    CRect hl = crect_make(cx, ry, NP_CHAR_W, 9);
                    gfx_fill_rect(s, &hl, sel_band);
                    tcol = sel_ink;
                }
                ch[0] = n->buf[k]; ch[1] = '\0';
                gfx_draw_text(s, GFX_FONT_SYSTEM, cx, ry, ch, tcol);
            }
        }
        /* Caret at its visual row/col -- and only in the window that has
         * the keyboard. A caret is a promise that what you type lands here,
         * and two windows showing one at once is two windows making it. */
        {
            int cr = np_caret_vrow(n, count);
            if (wm_has_focus(win) &&
                cr >= n->top_line && cr < n->top_line + L.vis_lines) {
                int cc = n->caret - g_vrow[cr];
                int cx = r.x0 + 4 + cc * NP_CHAR_W;
                int cy = r.y0 + 2 + (cr - n->top_line) * NP_LINE_H;
                gfx_vline(s, cx, cy, NP_LINE_H - 1, p->text);
            }
        }
    } else {
        /* Find the byte index of the first visible line. */
        idx = 0;
        {
            int ln = 0;
            while (ln < n->top_line && idx < n->len) {
                if (n->buf[idx] == '\n') { ln++; }
                idx++;
            }
        }

        y = r.y0 + 2;
        for (li = 0; li < L.vis_lines && idx <= n->len; li++) {
            int le = np_line_end(n, idx);
            int col, x = r.x0 + 4;
            for (col = n->left_col; col < le - idx && (col - n->left_col) < L.vis_cols; col++) {
                int bi = idx + col;
                int cx = x + (col - n->left_col) * NP_CHAR_W;
                char ch[2];
                CColor tcol = p->text;
                cbool hl_on = (hassel && bi >= slo && bi < shi) ||
                    (n->match_start >= 0 && bi >= n->match_start &&
                     bi < n->match_start + n->match_len);
                if (hl_on) {
                    CRect hl = crect_make(cx, y, NP_CHAR_W, 9);
                    gfx_fill_rect(s, &hl, sel_band);
                    tcol = sel_ink;
                }
                ch[0] = n->buf[bi];
                ch[1] = '\0';
                gfx_draw_text(s, GFX_FONT_SYSTEM, cx, y, ch, tcol);
            }
            y += NP_LINE_H;
            if (le >= n->len) { idx = n->len + 1; break; }
            idx = le + 1;
        }

        /* Caret. */
        {
            int cl = np_caret_line(n), cc = np_caret_col(n);
            if (wm_has_focus(win) &&
                cl >= n->top_line && cl < n->top_line + L.vis_lines &&
                cc >= n->left_col && cc <= n->left_col + L.vis_cols) {
                int cx = r.x0 + 4 + (cc - n->left_col) * NP_CHAR_W;
                int cy = r.y0 + 2 + (cl - n->top_line) * NP_LINE_H;
                gfx_vline(s, cx, cy, NP_LINE_H - 1, p->text);
            }
        }
    }

    /* Status bar: Ln:Col, file, modified marker. */
    /* The document's scroll bar. */
    r = crect_offset(&L.vbar, o.x, o.y);
    ui_scrollbar_draw(s, &r, CTRUE, np_total_rows(n, L.vis_cols), L.vis_lines,
                      n->top_line, n->hot_sb);

    r = crect_offset(&L.status, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, p->face);
    {
        char info[96];
        CRect tr = r; tr.x0 += 5;
        if (n->status[0] != '\0') {
            sys_strlcpy(info, n->status, sizeof(info));
        } else {
            sys_snprintf(info, sizeof(info), "Ln %d, Col %d%s",
                         np_caret_line(n) + 1, np_caret_col(n) + 1,
                         n->modified ? "  *modified" : "");
        }
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &tr, info, p->text,
                           GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
    }

    /* The drop-down last, over everything, with its shadow. */
    if (n->menu_open >= 0) {
        int mx = o.x + n->menu_x, my = o.y + n->menu_y, mw, mh;
        CRect mr;
        ui_menu_measure(&n->menu, GFX_FONT_SYSTEM, &mw, &mh);
        mr = crect_make(mx, my, mw, mh);
        gfx_drop_shadow(s, &mr, 4, 90);
        ui_menu_draw(s, &n->menu, mx, my, GFX_FONT_SYSTEM);
    }
}

/* ---- input ----------------------------------------------------------- */
/* Move the caret up/down by one *visual* row when wrapping is on. */
static void np_move_vert_wrap(Notepad *n, int dir, int vis_cols)
{
    int count = np_build_vrows(n, vis_cols);
    int r = np_caret_vrow(n, count);
    int col = n->caret - g_vrow[r];
    int tr = r + dir;
    if (tr < 0) { n->caret = 0; return; }
    if (tr >= count) { n->caret = n->len; return; }
    {
        int rs = g_vrow[tr];
        int re = np_vrow_end(n, tr, count);
        int nc = (col < re - rs) ? col : re - rs;
        n->caret = rs + nc;
    }
}

static void np_move_vert(Notepad *n, int dir)
{
    int col = np_caret_col(n);
    if (dir < 0) {
        int ls = np_line_start(n, n->caret);
        if (ls == 0) { n->caret = 0; return; }
        {
            int ps = np_line_start(n, ls - 1);
            int pe = ls - 1;
            int nc = (col < pe - ps) ? col : pe - ps;
            n->caret = ps + nc;
        }
    } else {
        int le = np_line_end(n, n->caret);
        if (le >= n->len) { n->caret = n->len; return; }
        {
            int ns = le + 1;
            int ne = np_line_end(n, ns);
            int nc = (col < ne - ns) ? col : ne - ns;
            n->caret = ns + nc;
        }
    }
}

/*
 * ---- repainting what changed, rather than the window --------------------
 *
 * Typing composited 167,325 pixels per character: 111% of the client, being
 * the whole text area plus the frame and the shadow strip every dirty rect
 * is inflated by. Measured off the shell's own frame counter, not guessed.
 * On the target every one of those crosses the ISA bus, and that is the
 * difference between an editor that feels instant and one that feels like a
 * modem. It is now the edited row and the status line: 17,091 pixels, 11%.
 *
 * NpMark is everything that decides WHICH rows a key changes. The rules are
 * conservative in every case that is not a plain edit or a caret move on one
 * line -- a scroll, a reflow, or anything to do with a selection repaints
 * the whole well exactly as before, because working out the minimum there is
 * harder than it is worth and none of them is the hot path.
 *
 * What keeps this honest is the rule --shrink-demo applies to every window
 * in the system, and --cost-demo applies here: repainting PART of a window
 * must leave it looking exactly as a full repaint would. A band that missed
 * the row it was meant to cover leaves the old glyphs on screen, and only
 * that notices.
 */
typedef struct {
    int top, left, row, rows, len, anchor;
} NpMark;

static void np_mark(Notepad *n, const NpLayout *L, NpMark *m)
{
    m->top = n->top_line;
    m->left = n->left_col;
    m->len = n->len;
    m->anchor = n->sel_anchor;
    if (n->wrap) {
        int count = np_build_vrows(n, L->vis_cols);
        m->rows = count;
        m->row = np_caret_vrow(n, count);
    } else {
        m->rows = np_total_rows(n, L->vis_cols);
        m->row = np_caret_line(n);
    }
}

static void np_invalidate_edit(WmWindow *win, Notepad *n, const NpLayout *L,
                               const NpMark *was)
{
    CPoint o = wm_client_origin(win);
    NpMark now;
    CRect r;
    int r0, r1;
    np_mark(n, L, &now);

    /* The status line carries Ln:Col, so it changes on every key without
     * exception. Sixteen pixels tall; it is never the expensive part. */
    r = crect_offset(&L->status, o.x, o.y);
    wm_invalidate(win, &r);

    if (now.top != was->top || now.left != was->left ||
        now.anchor != was->anchor || n->sel_anchor >= 0 || was->anchor >= 0) {
        r = crect_offset(&L->text, o.x, o.y);
        wm_invalidate(win, &r);
        r = crect_offset(&L->vbar, o.x, o.y);
        wm_invalidate(win, &r);
        return;
    }

    r0 = (now.row < was->row) ? now.row : was->row;
    r1 = (now.row > was->row) ? now.row : was->row;
    /*
     * A change in the number of rows pushes everything under it down, and a
     * wrapped edit re-breaks the lines below even when the count comes out
     * the same -- so both run to the bottom of the well.
     */
    if (now.rows != was->rows || (n->wrap && now.len != was->len)) {
        r1 = now.top + L->vis_lines;
    }
    {
        int y0 = L->text.y0 + 2 + (r0 - now.top) * NP_LINE_H;
        int y1 = L->text.y0 + 2 + (r1 - now.top + 1) * NP_LINE_H;
        CRect band;
        if (y0 < L->text.y0) { y0 = L->text.y0; }
        if (y1 > L->text.y1) { y1 = L->text.y1; }
        if (y1 <= y0) { y1 = y0 + NP_LINE_H; }
        band = crect_make(L->text.x0, y0, crect_w(&L->text), y1 - y0);
        band = crect_offset(&band, o.x, o.y);
        wm_invalidate(win, &band);
    }
    if (now.rows != was->rows) {
        r = crect_offset(&L->vbar, o.x, o.y);
        wm_invalidate(win, &r);
    }
}

static void np_on_key(WmWindow *win, int key, int ch, int mods)
{
    Notepad *n = (Notepad *)wm_user(win);
    CRect client = wm_client_rect(win);
    NpLayout L;
    NpMark before;
    cbool is_move;
    if (n == NULL) { return; }
    np_layout(crect_w(&client), crect_h(&client), &L);
    np_mark(n, &L, &before);

    /*
     * The menu bar, from the keyboard. F10 opens it, the arrows walk it,
     * Enter chooses. Until this existed the menus could be reached by mouse
     * alone -- and on DOS the mouse is a driver somebody may not have loaded.
     */
    {
        OfficeMenuNav nav;
        int cmd = -1;
        CRect c2 = wm_client_rect(win);
        NpLayout L2;
        np_layout(crect_w(&c2), crect_h(&c2), &L2);
        nav.open = &n->menu_open; nav.x = &n->menu_x; nav.y = &n->menu_y;
        nav.menu = &n->menu; nav.names = NP_MENU; nav.count = NP_MENU_N;
        nav.bar = L2.menubar; nav.build = np_menu_build_cb; nav.user = n;
        if (office_menu_key_win(win, &nav, key, &cmd)) {
            if (cmd >= 0) { np_command(win, n, cmd); }
            return;
        }
    }

    /*
     * Back to the live Ln:Col -- but only for a key the DOCUMENT got.
     *
     * This used to happen above the menu block, so walking the menu bar with
     * the arrows silently rewrote the status line while repainting only the
     * two menu rows that changed: the bar kept showing the old message, and
     * the new one appeared later, when something unrelated repainted the
     * window. It was invisible while every menu key repainted everything,
     * and the pixel-for-pixel check on the narrowed repaint is what found
     * it. A key that walks a menu is not a key the document received.
     */
    n->status[0] = '\0';

    /*
     * Shortcuts, every one of them the same command the menu runs. They were
     * six inline blocks here and five more in the click handler until the
     * menu arrived and made three copies of each command untenable.
     */
    if (NP_CTRL(key, ch, mods, 26, 'z')) { np_command(win, n, NC_UNDO);   return; }
    if (NP_CTRL(key, ch, mods, 25, 'y')) { np_command(win, n, NC_REDO);   return; }
    if (NP_CTRL(key, ch, mods,  1, 'a')) { np_command(win, n, NC_SELALL); return; }
    if (NP_CTRL(key, ch, mods,  3, 'c')) { np_command(win, n, NC_COPY);   return; }
    if (NP_CTRL(key, ch, mods, 24, 'x')) { np_command(win, n, NC_CUT);    return; }
    if (NP_CTRL(key, ch, mods, 22, 'v')) { np_command(win, n, NC_PASTE);  return; }
    if (NP_CTRL(key, ch, mods, 14, 'n')) { np_command(win, n, NC_NEW);    return; }
    if (NP_CTRL(key, ch, mods, 15, 'o')) { np_command(win, n, NC_OPEN);   return; }
    if (NP_CTRL(key, ch, mods, 19, 's')) { np_command(win, n, NC_SAVE);   return; }
    if (NP_CTRL(key, ch, mods,  6, 'f')) { np_command(win, n, NC_FIND);   return; }
    if (key == PLAT_KEY_F3) { np_command(win, n, NC_FINDNEXT); return; }

    /* Shift + a navigation key extends the selection; a bare navigation key
     * drops it. Anchor is captured lazily the first time Shift is held. */
    is_move = (key == PLAT_KEY_LEFT || key == PLAT_KEY_RIGHT ||
               key == PLAT_KEY_UP || key == PLAT_KEY_DOWN ||
               key == PLAT_KEY_HOME || key == PLAT_KEY_END ||
               key == PLAT_KEY_PGUP || key == PLAT_KEY_PGDN);
    if (is_move) {
        if (mods & PLAT_MOD_SHIFT) {
            if (n->sel_anchor < 0) { n->sel_anchor = n->caret; }
        } else {
            np_clear_sel(n);
        }
    }

    if (ch >= 32 && ch < 127) {
        np_insert(n, ch);
    } else {
        switch (key) {
        case PLAT_KEY_ENTER:  np_insert(n, '\n'); break;
        case PLAT_KEY_TAB: { int k; for (k = 0; k < NP_TAB; k++) { np_insert(n, ' '); } break; }
        case PLAT_KEY_BACKSP:
            if (np_has_sel(n)) { np_delete_sel(n); }
            else if (n->caret > 0) { np_delete_at(n, n->caret - 1); n->caret--; }
            break;
        case PLAT_KEY_DELETE:
            if (np_has_sel(n)) { np_delete_sel(n); }
            else if (n->caret < n->len) { np_delete_at(n, n->caret); }
            break;
        case PLAT_KEY_LEFT:  if (n->caret > 0) { n->caret--; } break;
        case PLAT_KEY_RIGHT: if (n->caret < n->len) { n->caret++; } break;
        case PLAT_KEY_UP:
            if (n->wrap) { np_move_vert_wrap(n, -1, L.vis_cols); } else { np_move_vert(n, -1); }
            break;
        case PLAT_KEY_DOWN:
            if (n->wrap) { np_move_vert_wrap(n, +1, L.vis_cols); } else { np_move_vert(n, +1); }
            break;
        case PLAT_KEY_HOME:  n->caret = np_line_start(n, n->caret); break;
        case PLAT_KEY_END:   n->caret = np_line_end(n, n->caret); break;
        case PLAT_KEY_PGUP:  { int k; for (k = 0; k < L.vis_lines; k++) {
            if (n->wrap) { np_move_vert_wrap(n, -1, L.vis_cols); } else { np_move_vert(n, -1); } } break; }
        case PLAT_KEY_PGDN:  { int k; for (k = 0; k < L.vis_lines; k++) {
            if (n->wrap) { np_move_vert_wrap(n, +1, L.vis_cols); } else { np_move_vert(n, +1); } } break; }
        case PLAT_KEY_F3:    np_find_next(n); break;  /* find next match */
        default: return;
        }
    }
    np_scroll_to_caret(n, &L);
    np_invalidate_edit(win, n, &L, &before);
}

/* Map a pixel in the text well to a caret byte index (handles wrap + scroll). */
static int np_caret_from_px(Notepad *n, NpLayout *L, int px, int py)
{
    int col = (px - (L->text.x0 + 4) + NP_CHAR_W / 2) / NP_CHAR_W;
    if (col < 0) { col = 0; }
    if (n->wrap) {
        int count = np_build_vrows(n, L->vis_cols);
        int row = n->top_line + (py - (L->text.y0 + 2)) / NP_LINE_H;
        int rs, re;
        if (row < 0) { row = 0; }
        if (row >= count) { row = count - 1; }
        rs = g_vrow[row]; re = np_vrow_end(n, row, count);
        return (rs + col > re) ? re : rs + col;
    } else {
        int line = n->top_line + (py - (L->text.y0 + 2)) / NP_LINE_H;
        int idx = 0, ln = 0, le;
        col += n->left_col;
        if (line < 0) { line = 0; }
        while (ln < line && idx < n->len) { if (n->buf[idx] == '\n') { ln++; } idx++; }
        le = np_line_end(n, idx);
        return (idx + col > le) ? le : idx + col;
    }
}

/* ---- commands ---------------------------------------------------------- */
/*
 * One body per command, reached from the menu, the toolbar and the keyboard
 * alike. Before this the toolbar and the key handler each carried their own
 * copy of "new document" and "open"; two copies of a command is how one of
 * them quietly stops matching the other.
 */
static void np_command(WmWindow *win, Notepad *n, int cmd)
{
    CRect client = wm_client_rect(win);
    NpLayout L;
    np_layout(crect_w(&client), crect_h(&client), &L);
    switch (cmd) {
    case NC_NEW:
        n->len = 0; n->caret = 0; n->buf[0] = '\0'; n->path[0] = '\0';
        n->top_line = 0; n->left_col = 0; n->modified = CFALSE;
        n->match_start = -1; n->sel_anchor = -1; n->selecting = CFALSE;
        undo_init(&n->undo);   /* a new document has no history to walk back */
        sys_strlcpy(n->status, "New document", sizeof(n->status));
        break;
    case NC_OPEN:
        ui_file_dialog("Open", NULL, "TXT", CFALSE, n->path, np_on_open, win);
        return;
    case NC_SAVE:
    case NC_SAVEAS:
        ui_file_dialog("Save As", NULL, "TXT", CTRUE,
                       (n->path[0] != '\0') ? n->path : "UNTITLED.TXT",
                       np_on_save, win);
        return;
    case NC_CLOSE:
        wm_destroy(win);
        return;                /* the app is gone; do not touch it */
    case NC_UNDO:
    case NC_REDO: {
        int at = (cmd == NC_UNDO)
                 ? undo_undo(&n->undo, n->buf, &n->len, NP_MAX)
                 : undo_redo(&n->undo, n->buf, &n->len, NP_MAX);
        if (at >= 0) {
            n->buf[n->len] = '\0';
            n->caret = (at <= n->len) ? at : n->len;
            np_clear_sel(n);
            n->modified = CTRUE;
            n->match_start = -1;
            sys_strlcpy(n->status, (cmd == NC_UNDO) ? "Undo" : "Redo",
                        sizeof n->status);
        } else {
            sys_strlcpy(n->status,
                        (cmd == NC_UNDO) ? "Nothing to undo"
                                         : "Nothing to redo",
                        sizeof n->status);
        }
        break;
    }
    case NC_SELALL:  np_select_all(n); break;
    case NC_COPY:
        if (np_has_sel(n)) {
            int cnt = np_sel_hi(n) - np_sel_lo(n);
            np_copy_sel(n);
            sys_snprintf(n->status, sizeof(n->status), "Copied %d chars", cnt);
        } else {
            sys_strlcpy(n->status, "Nothing selected", sizeof(n->status));
        }
        break;
    case NC_CUT:     np_copy_sel(n); np_delete_sel(n); break;
    case NC_DELETE:  np_delete_sel(n); break;
    case NC_PASTE:   np_paste(n); break;
    case NC_FIND:
        ui_prompt("Find", "Search for:", n->search, np_on_find, win);
        return;
    case NC_FINDNEXT:
        np_find_from(n, n->caret + 1);
        break;
    case NC_WRAP:
        n->wrap = !n->wrap;
        n->top_line = 0; n->left_col = 0;  /* row semantics change */
        n->status[0] = '\0';
        break;
    case NC_TIME: {
        int h, mi, se, y, mo, d, wd;
        char buf[48];
        plat_wall_clock(&h, &mi, &se);
        plat_wall_date(&y, &mo, &d, &wd);
        sys_snprintf(buf, sizeof buf, "%02d:%02d %d-%02d-%02d", h, mi, y, mo, d);
        {
            int k;
            for (k = 0; buf[k] != '\0'; k++) { np_insert(n, buf[k]); }
        }
        break;
    }
    case NC_ABOUT:
        sys_strlcpy(n->status, "Notepad -- Ctrl+Z undo, Ctrl+F find",
                    sizeof n->status);
        break;
    default: break;
    }
    np_scroll_to_caret(n, &L);
    wm_invalidate(win, NULL);
}

/* ---- menus ------------------------------------------------------------- */
static void np_build_menu(Notepad *n, int idx)
{
    UiMenu *m = &n->menu;
    ui_menu_clear(m);
    switch (idx) {
    case 0:
        ui_menu_add(m, NC_NEW,    "New\tCtrl+N", CTRUE);
        ui_menu_add(m, NC_OPEN,   "Open...\tCtrl+O", CTRUE);
        ui_menu_add(m, NC_SAVE,   "Save\tCtrl+S", CTRUE);
        ui_menu_add(m, NC_SAVEAS, "Save As...", CTRUE);
        ui_menu_add_separator(m);
        ui_menu_add(m, NC_CLOSE,  "Exit", CTRUE);
        break;
    case 1:
        /* Greyed when there is nothing to take back, so the menu reports the
         * state of the document rather than merely listing verbs. */
        ui_menu_add(m, NC_UNDO, "Undo\tCtrl+Z", undo_can_undo(&n->undo));
        ui_menu_add(m, NC_REDO, "Redo\tCtrl+Y", undo_can_redo(&n->undo));
        ui_menu_add_separator(m);
        ui_menu_add(m, NC_CUT,   "Cut\tCtrl+X", np_has_sel(n));
        ui_menu_add(m, NC_COPY,  "Copy\tCtrl+C", np_has_sel(n));
        ui_menu_add(m, NC_PASTE, "Paste\tCtrl+V",
                    (clip_get_text() != NULL && clip_get_text()[0] != '\0')
                        ? CTRUE : CFALSE);
        ui_menu_add(m, NC_DELETE, "Delete", np_has_sel(n));
        ui_menu_add_separator(m);
        ui_menu_add(m, NC_SELALL, "Select All\tCtrl+A", CTRUE);
        ui_menu_add(m, NC_TIME,   "Time/Date", CTRUE);
        break;
    case 2:
        ui_menu_add(m, NC_FIND, "Find...\tCtrl+F", CTRUE);
        ui_menu_add(m, NC_FINDNEXT, "Find Next\tF3",
                    (n->search[0] != '\0') ? CTRUE : CFALSE);
        ui_menu_add_separator(m);
        ui_menu_add(m, NC_WRAP, n->wrap ? "Word Wrap (on)" : "Word Wrap",
                    CTRUE);
        break;
    default:
        ui_menu_add(m, NC_ABOUT, "About Notepad", CTRUE);
        break;
    }
    m->highlight = -1;
}

/* office_menu_key rebuilds the list when it moves along the bar. */
static void np_menu_build_cb(void *user, int idx)
{
    np_build_menu((Notepad *)user, idx);
}

static void np_open_menu(WmWindow *win, Notepad *n, int idx)
{
    CRect client = wm_client_rect(win);
    NpLayout L;
    CRect w;
    np_layout(crect_w(&client), crect_h(&client), &L);
    w = office_menu_word(&L.menubar, NP_MENU, idx);
    n->menu_open = idx;
    n->menu_x = w.x0;
    n->menu_y = L.menubar.y1;
    np_build_menu(n, idx);
    wm_invalidate(win, NULL);
}

static void np_on_click(WmWindow *win, int px, int py)
{
    Notepad *n = (Notepad *)wm_user(win);
    CRect client = wm_client_rect(win);
    NpLayout L;
    if (n == NULL) { return; }
    np_layout(crect_w(&client), crect_h(&client), &L);

    /* An open menu eats the click first, wherever it lands: choosing an item,
     * or dismissing the menu by clicking away from it. */
    if (n->menu_open >= 0) {
        int hit = ui_menu_hit(&n->menu, n->menu_x, n->menu_y,
                              GFX_FONT_SYSTEM, px, py);
        if (hit >= 0) {
            int cmd = n->menu.items[hit].id;
            n->menu_open = -1;
            np_command(win, n, cmd);
            return;
        }
        n->menu_open = -1;
        wm_invalidate(win, NULL);
    }
    if (crect_contains(&L.menubar, px, py)) {
        int i;
        for (i = 0; i < NP_MENU_N; i++) {
            CRect w = office_menu_word(&L.menubar, NP_MENU, i);
            if (crect_contains(&w, px, py)) { np_open_menu(win, n, i); return; }
        }
        return;
    }

    /* The bar takes the click before the text does, so pressing it can never
     * move the caret or start a selection. */
    if (crect_contains(&L.vbar, px, py)) {
        int total = np_total_rows(n, L.vis_cols);
        int part = ui_scrollbar_hit(&L.vbar, CTRUE, total, L.vis_lines,
                                    n->top_line, px, py);
        n->hot_sb = part;
        switch (part) {
        case UI_SB_LINE_UP:   n->top_line--; break;
        case UI_SB_LINE_DOWN: n->top_line++; break;
        case UI_SB_PAGE_UP:   n->top_line -= L.vis_lines; break;
        case UI_SB_PAGE_DOWN: n->top_line += L.vis_lines; break;
        case UI_SB_THUMB:     n->sb_drag = CTRUE; break;
        default: break;
        }
        if (n->top_line > total - L.vis_lines) {
            n->top_line = total - L.vis_lines;
        }
        if (n->top_line < 0) { n->top_line = 0; }
        /* The text and the bar; the toolbar and the status line did not
         * move. The same region the thumb drag uses -- an arrow held down is
         * the same gesture at a different speed. */
        np_invalidate_scroll(win, &L);
        return;
    }

    {
        int tb = np_tb_at(&L, px, py);
        if (tb >= 0) {
            static const int NPB_CMD[NPB_COUNT] = {
                NC_NEW, NC_OPEN, NC_SAVE, NC_FIND, NC_WRAP
            };
            (void)ui_hot_press(&n->hot_tb, tb);
            np_command(win, n, NPB_CMD[tb]);
            return;
        }
    }
    if (crect_contains(&L.text, px, py)) {
        int pos = np_caret_from_px(n, &L, px, py);
        n->caret = pos;
        n->sel_anchor = pos;   /* start a (possibly empty) selection */
        n->selecting = CTRUE;  /* extend on drag until the button is released */
        n->status[0] = '\0';
        wm_invalidate(win, NULL);
    }
}

/*
 * What a SCROLL changed: the text well and the bar whose thumb moved with it.
 * Not the toolbar, not the menu bar, not the status line -- none of those
 * move when the page does.
 */
static void np_invalidate_scroll(WmWindow *win, const NpLayout *L)
{
    CPoint o = wm_client_origin(win);
    CRect r;
    r = crect_offset(&L->text, o.x, o.y);
    wm_invalidate(win, &r);
    r = crect_offset(&L->vbar, o.x, o.y);
    wm_invalidate(win, &r);
}

/*
 * The toolbar strip, in SCREEN coordinates -- which is what wm_invalidate
 * wants, and the mistake worth factoring out: a client-relative rectangle
 * handed to it lands a title bar's height too high and repaints the wrong
 * band. Every toolbar hover transition goes through here.
 */
static void np_invalidate_toolbar(WmWindow *win)
{
    CRect client = wm_client_rect(win);
    CPoint o = wm_client_origin(win);
    NpLayout L;
    CRect tb;
    np_layout(crect_w(&client), crect_h(&client), &L);
    tb = crect_offset(&L.toolbar, o.x, o.y);
    wm_invalidate(win, &tb);
}

/* Extend the selection while the mouse button is held (drag-select). */
static void np_on_motion(WmWindow *win, int px, int py)
{
    Notepad *n = (Notepad *)wm_user(win);
    CRect client;
    NpLayout L;
    if (n == NULL) { return; }
    /*
     * An open menu highlights the row under the pointer.
     *
     * It did not: you could run the mouse down Notepad's File menu and
     * nothing lit up until you clicked, which in this idiom reads as a menu
     * that is not responding. CastaliaWrite and CastaliaSheet already did
     * this; Notepad and Paint were the two that did not.
     */
    if (n->menu_open >= 0) {
        int i;
        client = wm_client_rect(win);
        np_layout(crect_w(&client), crect_h(&client), &L);
        /*
         * ...and sliding along the BAR with one open switches to the menu
         * you slide onto, rather than making you close this one and click
         * again. Write and Sheet already did this; Notepad and Paint were
         * the two that did not. Checked before the drop-down below, because
         * the bar is above the open menu and the pointer crosses it.
         */
        if (crect_contains(&L.menubar, px, py)) {
            for (i = 0; i < NP_MENU_N; i++) {
                CRect w = office_menu_word(&L.menubar, NP_MENU, i);
                if (crect_contains(&w, px, py) && i != n->menu_open) {
                    np_open_menu(win, n, i);
                    return;
                }
            }
        }
        {
            int hit = ui_menu_hit(&n->menu, n->menu_x, n->menu_y,
                                  GFX_FONT_SYSTEM, px, py);
            if (hit != n->menu.highlight) {
                int was = n->menu.highlight;
                n->menu.highlight = hit;
                ui_menu_repaint(win, &n->menu, n->menu_x, n->menu_y,
                                GFX_FONT_SYSTEM, was, hit);
            }
        }
        return;
    }
    if (n->sb_drag) {
        int was_top;
        client = wm_client_rect(win);
        np_layout(crect_w(&client), crect_h(&client), &L);
        was_top = n->top_line;
        n->top_line = ui_scroll_pos_from_coord(crect_h(&L.vbar),
                                               np_total_rows(n, L.vis_cols),
                                               L.vis_lines, py - L.vbar.y0);
        /* The text and the thumb moved; the toolbar and the status line did
         * not. On the transition only -- a pointer that slid a pixel without
         * landing on a new row changed nothing. */
        if (n->top_line != was_top) { np_invalidate_scroll(win, &L); }
        return;
    }
    if (!n->selecting) {
        /* Nothing else is going on, so the toolbar may light under the
         * pointer. On the TRANSITION only -- ui_hot_move answers CTRUE when
         * what should be drawn changed, not when the pointer moved -- and
         * over the toolbar strip rather than the whole window. */
        client = wm_client_rect(win);
        np_layout(crect_w(&client), crect_h(&client), &L);
        if (ui_hot_move(&n->hot_tb, np_tb_at(&L, px, py))) {
            np_invalidate_toolbar(win);
        }
        return;
    }
    client = wm_client_rect(win);
    np_layout(crect_w(&client), crect_h(&client), &L);
    n->caret = np_caret_from_px(n, &L, px, py);
    np_scroll_to_caret(n, &L);
    wm_invalidate(win, NULL);
}

static void np_on_lbup(WmWindow *win)
{
    Notepad *n = (Notepad *)wm_user(win);
    if (n != NULL) {
        /*
         * Letting go of the scroll bar puts it back.
         *
         * ui_scrollbar_draw takes hot_sb and draws that part active, so a
         * thumb released with nothing repainting the bar stayed drawn as
         * though it were still held -- until the next keystroke or hover
         * happened to redraw it. Nothing repainted it here, and nothing had
         * to while every motion event during the drag repainted the window:
         * the release landed on a bar the last of those had just drawn hot.
         * Narrowing the drag is what exposed it, and the pixel check on the
         * narrowed repaint is what found it.
         */
        cbool was_sb = (n->sb_drag || n->hot_sb != UI_SB_NONE) ? CTRUE : CFALSE;
        n->selecting = CFALSE;
        n->sb_drag = CFALSE;
        n->hot_sb = UI_SB_NONE;
        if (was_sb) {
            CRect client = wm_client_rect(win);
            CPoint o = wm_client_origin(win);
            NpLayout L;
            CRect r;
            np_layout(crect_w(&client), crect_h(&client), &L);
            r = crect_offset(&L.vbar, o.x, o.y);
            wm_invalidate(win, &r);
        }
        if (ui_hot_release(&n->hot_tb)) { np_invalidate_toolbar(win); }
    }
}

static void np_on_leave(WmWindow *win)
{
    Notepad *n = (Notepad *)wm_user(win);
    cbool redraw;
    if (n == NULL) { return; }
    redraw = ui_hot_release(&n->hot_tb);
    if (ui_hot_move(&n->hot_tb, -1)) { redraw = CTRUE; }
    if (redraw) { np_invalidate_toolbar(win); }
}

static cbool np_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    switch (msg) {
    case WM_MSG_CLOSE:       return np_on_close(win);
    case WM_MSG_QUERY_UNSAVED: return np_has_unsaved(win);
    case WM_MSG_PAINT:       np_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: np_on_click(win, (int)a, (int)b);   return CTRUE;
    case WM_MSG_MOUSEMOVE:   np_on_motion(win, (int)a, (int)b);  return CTRUE;
    case WM_MSG_LBUTTONUP:   np_on_lbup(win);                    return CTRUE;
    case WM_MSG_MOUSELEAVE:  np_on_leave(win);                   return CTRUE;
    case WM_MSG_MOUSEWHEEL: {
        Notepad *n = (Notepad *)wm_user(win);
        CRect client;
        NpLayout L;
        if (n == NULL) { return CFALSE; }
        client = wm_client_rect(win);
        np_layout(crect_w(&client), crect_h(&client), &L);
        if (ui_scroll_wheel(&n->top_line, (int)a,
                            np_total_rows(n, L.vis_cols), L.vis_lines)) {
            np_invalidate_scroll(win, &L);
        }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:
        np_on_key(win, (int)a, (int)b, (param != NULL) ? *(const int *)param : 0);
        return CTRUE;
    case WM_MSG_DESTROY: {
        Notepad *n = (Notepad *)wm_user(win);
        if (n != NULL) { sys_free(n, (cu32)sizeof(Notepad)); }
        return CTRUE;
    }
    default: return CFALSE;
    }
}

/* Create and show a Notepad window; returns its payload (or NULL on failure). */
static Notepad *np_spawn(void)
{
    Notepad *n;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int fw = 480, fh = 340, fx, fy;

    n = (Notepad *)sys_calloc(1, (cu32)sizeof(Notepad));
    if (n == NULL) { SYS_LOGE("app", "notepad: OOM"); return NULL; }
    n->match_start = -1;
    n->sel_anchor = -1;
    n->menu_open = -1;    /* sys_calloc would leave menu 0 hanging open */

    plat_video_info(&vi);
    fx = (vi.width - fw) / 2 + 20;
    fy = (vi.height - fh) / 2 - 10;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, fw, fh);
    w = wm_create("Notepad", &frame, WM_STYLE_APP, np_proc, n);
    if (w == NULL) { sys_free(n, (cu32)sizeof(Notepad)); return NULL; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    return n;
}

/*
 * Closing a document with unsaved changes ASKS first.
 *
 * It did not. Type into Notepad, click the close box, and the text was gone
 * -- no dialog, no warning, nothing to undo it with. Every editor of this era
 * asked, and the window manager has always sent WM_MSG_CLOSE for exactly this
 * (see wm.h: an app "may handle it (prompt, veto), and only if it does not
 * does the manager destroy it"). Nothing in this system had ever used it.
 *
 * Returning CTRUE vetoes the close; the dialog's answer destroys the window
 * or leaves it alone. wm_destroy() still does NOT ask, which is what the
 * shutdown path and the demo teardown want.
 */
static void np_on_discard(UiDialogResult result, void *user)
{
    WmWindow *win = (WmWindow *)user;
    if (result == UI_DR_YES && win != NULL) { wm_destroy(win); }
}

/* One predicate, asked by the close box AND by the shutdown dialog -- two
 * answers to "is there work here" would eventually disagree, and the one that
 * said no would be the one that lost it. */
static cbool np_has_unsaved(WmWindow *win)
{
    Notepad *n = (win != NULL) ? (Notepad *)wm_user(win) : NULL;
    return (n != NULL && n->modified) ? CTRUE : CFALSE;
}

static cbool np_on_close(WmWindow *win)
{
    Notepad *n = (win != NULL) ? (Notepad *)wm_user(win) : NULL;
    char msg[160];
    if (!np_has_unsaved(win)) { return CFALSE; }   /* let it go */
    sys_snprintf(msg, sizeof(msg),
                 "%s has changes you have not saved.\n\n"
                 "Close it and lose them?",
                 (n->path[0] != '\0') ? ui_path_base(n->path) : "This document");
    ui_msgbox("Notepad", msg, UI_MB_YESNO, np_on_discard, win);
    return CTRUE;
}

/*
 * The scroll bar and the text well, in SCREEN coordinates, for the headless
 * driver: one to grab the thumb by, the other to bound what a scroll is
 * allowed to repaint. A scene that guesses either at a fixed offset stops
 * measuring the thing it names the moment the toolbar grows a button.
 */
cbool app_notepad_scroll_rects(WmWindow *win, CRect *bar, CRect *well)
{
    NpLayout L;
    CRect client;
    CPoint o;
    if (win == NULL || wm_user(win) == NULL) { return CFALSE; }
    client = wm_client_rect(win);
    np_layout(crect_w(&client), crect_h(&client), &L);
    o = wm_client_origin(win);
    if (bar  != NULL) { *bar  = crect_offset(&L.vbar, o.x, o.y); }
    if (well != NULL) { *well = crect_offset(&L.text, o.x, o.y); }
    return CTRUE;
}

CRect app_notepad_btn_rect(WmWindow *win, int idx)
{
    NpLayout L;
    CRect client, none = crect_make(0, 0, 0, 0), r;
    CPoint o;
    if (win == NULL || wm_user(win) == NULL) { return none; }
    if (idx < 0 || idx >= NPB_COUNT) { return none; }
    client = wm_client_rect(win);
    np_layout(crect_w(&client), crect_h(&client), &L);
    o = wm_client_origin(win);
    r = np_btn_rect(&L, idx);
    return crect_offset(&r, o.x, o.y);
}

const char *app_notepad_text(WmWindow *win)
{
    Notepad *n = (Notepad *)wm_user(win);
    if (n == NULL) { return ""; }
    n->buf[n->len] = '\0';
    return n->buf;
}

void app_notepad_open(void)
{
    Notepad *n = np_spawn();
    if (n == NULL) { return; }
    sys_strlcpy(n->status, "New document", sizeof(n->status));
}

void app_notepad_open_file(const char *path)
{
    Notepad *n;
    if (path == NULL || path[0] == '\0') { app_notepad_open(); return; }
    n = np_spawn();
    if (n == NULL) { return; }
    np_load_file(n, path);
}

/*
 * Where a menu-bar word sits, for --menuaccel-demo. Notepad's menu is the
 * only route to several of its commands for anyone who does not already know
 * the shortcuts, so the scene drives it the way a person would: by clicking.
 */
CRect app_notepad_menu_word(WmWindow *win, int idx)
{
    CRect client;
    NpLayout L;
    if (win == NULL) { return crect_make(0, 0, 0, 0); }
    client = wm_client_rect(win);
    np_layout(crect_w(&client), crect_h(&client), &L);
    return office_menu_word(&L.menubar, NP_MENU, idx);
}

/*
 * The menu row under the cursor, for --menuaccel-demo.
 *
 * The scene used to find "Select All" by counting rows -- and its comment
 * called it "the last row of the Edit menu", which it is not: Time/Date sits
 * below it. Counting also has to guess the height of each separator. A scene
 * that measures the menu's current shape breaks the day the menu changes, and
 * that is exactly how cz-demo came to report compression writing a -1 byte
 * file. So the row is found by NAME instead, through this.
 */
const char *app_notepad_menu_item(WmWindow *win)
{
    Notepad *n = (win != NULL) ? (Notepad *)wm_user(win) : NULL;
    if (n == NULL || n->menu_open < 0) { return NULL; }
    if (!ui_menu_selectable(&n->menu, n->menu.highlight)) { return NULL; }
    return n->menu.items[n->menu.highlight].label;
}

/*
 * The status line, for --bigfile-demo. What the editor SAYS about a file it
 * could not fit is the whole question there: a refusal that is not said is
 * indistinguishable from an open, and that is how the tail gets saved away.
 */
void app_notepad_status(WmWindow *win, char *out, cu32 cap)
{
    Notepad *n = (win != NULL) ? (Notepad *)wm_user(win) : NULL;
    if (out == NULL || cap == 0) { return; }
    sys_strlcpy(out, (n != NULL) ? n->status : "", cap);
}

/* How many characters are selected, for --menuaccel-demo: the observable
 * effect of Edit > Select All, and the only one a screenshot could not tell
 * from an empty document. */
int app_notepad_sel_len(WmWindow *win)
{
    Notepad *n = (win != NULL) ? (Notepad *)wm_user(win) : NULL;
    if (n == NULL || !np_has_sel(n)) { return 0; }
    return np_sel_hi(n) - np_sel_lo(n);
}
