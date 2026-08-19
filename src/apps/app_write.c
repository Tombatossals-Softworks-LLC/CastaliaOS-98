/*
 * app_write.c - CastaliaWrite: a word processor in the 2001 office idiom.
 *
 * A page-view editor over the pure model in write_core.c, dressed the way the
 * word processors of that era were: a menu bar with real drop-downs, a
 * Standard and a Formatting toolbar whose buttons stay flat until the pointer
 * is over them, a ruler with margin markers, a white sheet with a drop shadow
 * floating on a gray workspace, and a status bar split into sunken panels.
 *
 * Text carries bold / italic / underline per character and alignment per
 * paragraph; documents save to a plain, repairable markup (see write_core.h).
 */
#include "apps.h"
#include "write_core.h"
#include "undo_core.h"
#include "office_ui.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"
#include <stdlib.h>

/* Ctrl-shortcut detection on both backends, the same shape Notepad and Paint
 * use: DOS hands Ctrl+letter over as an ASCII control code in 'key', while a
 * host backend may deliver the letter itself with the CTRL modifier set. */
#define WA_CTRL(key, ch, mods, code, letter) \
    ((key) == (code) || \
     (((mods) & PLAT_MOD_CTRL) && ((ch) == (letter) || (ch) == (letter) - 32)))

#define WA_RULER_H     14
#define WA_PAGE_MARGIN 16
#define WA_GLYPH_W     6
#define WA_LINE_H      11

/* ---- toolbars --------------------------------------------------------- */
enum { TB_NEW = 0, TB_OPEN, TB_SAVE, TB_PRINT, TB_CUT, TB_COPY, TB_PASTE,
       TB_BOLD, TB_ITALIC, TB_UNDER, TB_ALEFT, TB_ACENTER, TB_ARIGHT, TB_N };

/* icon, which toolbar row, and whether a separator precedes it */
static const struct { int icon; int row; int gap; } WA_TB[TB_N] = {
    { OFI_NEW, 0, 0 }, { OFI_OPEN, 0, 0 }, { OFI_SAVE, 0, 0 },
    { OFI_PRINT, 0, 0 },
    { OFI_CUT, 0, 1 }, { OFI_COPY, 0, 0 }, { OFI_PASTE, 0, 0 },
    { OFI_BOLD, 1, 0 }, { OFI_ITALIC, 1, 0 }, { OFI_UNDER, 1, 0 },
    { OFI_ALEFT, 1, 1 }, { OFI_ACENTER, 1, 0 }, { OFI_ARIGHT, 1, 0 }
};
#define WA_FMT_X 128        /* the formatting row starts after the boxes */

/* ---- menus ------------------------------------------------------------ */
static const char *const WA_MENU[] = {
    "File", "Edit", "View", "Insert", "Format", "Tools", "Help"
};
#define WA_MENU_N 7

enum {
    MC_NEW = 200, MC_OPEN, MC_SAVE, MC_SAVEAS, MC_CLOSE,
    MC_CUT, MC_COPY, MC_PASTE, MC_SELALL, MC_DELETE,
    MC_RULER, MC_STATUS,
    MC_DATE, MC_TIME,
    MC_BOLD, MC_ITALIC, MC_UNDER, MC_ALEFT, MC_ACENTER, MC_ARIGHT,
    MC_WORDCOUNT, MC_ABOUT, MC_FIND, MC_FINDNEXT, MC_REPLACE
};

typedef struct {
    WriteDoc doc;
    WrRow    rows[WR_MAX_ROWS];
    int      nrows;
    int      cols;
    int      top;
    unsigned char typing;       /* attributes armed for new text          */
    cbool    dragging;
    int      hover_tb;          /* toolbar button under the pointer, or -1 */
    int      hot_sb;            /* active scroll-bar part (UI_SB_*)        */
    cbool    sb_drag;           /* dragging the scroll thumb               */
    int      menu_open;         /* open menu index, or -1                  */
    int      menu_x, menu_y;
    UiMenu   menu;
    cbool    show_ruler;
    cbool    show_status;
    char     path[CASTALIA_MAX_PATH];
    char     find[64];          /* last search term                       */
    char     repl[64];          /* pending replacement (two-step prompt)  */
    char     status[80];
    cu32     saved_sum;       /* the document as it was when last saved */
    WUndoStack undo;          /* Ctrl+Z, attributes and all            */
} WriteApp;

typedef struct {
    CRect menubar;
    CRect tbar[2];
    CRect btn[TB_N];
    CRect fontbox, sizebox;
    CRect ruler;
    CRect work, page, text;
    CRect vbar;
    CRect status;
    int   vis_rows;
} WaLayout;

static void wa_layout(WmWindow *win, const WriteApp *a, WaLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    int i, x[2], y0, rh = a->show_ruler ? WA_RULER_H : 0;
    int sh = a->show_status ? OF_STATUS_H : 0;

    L->menubar = crect_make(0, 0, cw, OF_MENUBAR_H);
    L->tbar[0] = crect_make(0, OF_MENUBAR_H, cw, OF_TOOLBAR_H);
    L->tbar[1] = crect_make(0, OF_MENUBAR_H + OF_TOOLBAR_H, cw, OF_TOOLBAR_H);
    L->fontbox = crect_make(4, L->tbar[1].y0 + 3, 78, OF_TOOLBAR_H - 7);
    L->sizebox = crect_make(86, L->tbar[1].y0 + 3, 34, OF_TOOLBAR_H - 7);
    x[0] = 4;
    x[1] = WA_FMT_X;
    for (i = 0; i < TB_N; i++) {
        int row = WA_TB[i].row;
        if (WA_TB[i].gap) { x[row] += 7; }
        L->btn[i] = crect_make(x[row], L->tbar[row].y0 + 2, OF_BTN_W, OF_BTN_H);
        x[row] += OF_BTN_W + 1;
    }
    y0 = OF_MENUBAR_H + OF_TOOLBAR_H * 2;
    L->ruler = crect_make(0, y0, cw, rh);
    L->work  = crect_make(0, y0 + rh, cw - UI_SB_W, ch - y0 - rh - sh);
    L->vbar  = crect_make(cw - UI_SB_W, y0 + rh, UI_SB_W,
                          ch - y0 - rh - sh);
    L->page  = crect_make(L->work.x0 + 14, L->work.y0 + 5,
                          crect_w(&L->work) - 28, crect_h(&L->work) - 5);
    L->text  = crect_make(L->page.x0 + WA_PAGE_MARGIN,
                          L->page.y0 + WA_PAGE_MARGIN,
                          crect_w(&L->page) - WA_PAGE_MARGIN * 2,
                          crect_h(&L->page) - WA_PAGE_MARGIN);
    L->status = crect_make(0, ch - sh, cw, sh);
    L->vis_rows = crect_h(&L->text) / WA_LINE_H;
    if (L->vis_rows < 1) { L->vis_rows = 1; }
}

static void wa_reflow(WriteApp *a, const WaLayout *L)
{
    a->cols = crect_w(&L->text) / WA_GLYPH_W;
    if (a->cols < 4) { a->cols = 4; }
    a->nrows = wr_layout(&a->doc, a->cols, a->rows, WR_MAX_ROWS);
}

static void wa_scroll_to_caret(WriteApp *a, const WaLayout *L)
{
    int r = wr_row_of(a->rows, a->nrows, a->doc.caret);
    if (r < a->top) { a->top = r; }
    if (r >= a->top + L->vis_rows) { a->top = r - L->vis_rows + 1; }
    if (a->top > a->nrows - 1) { a->top = a->nrows - 1; }
    if (a->top < 0) { a->top = 0; }
}

static int wa_row_x(const WriteApp *a, const WaLayout *L, int r)
{
    int w = a->rows[r].len * WA_GLYPH_W;
    int avail = crect_w(&L->text);
    if (a->rows[r].align == WR_CENTER) { return L->text.x0 + (avail - w) / 2; }
    if (a->rows[r].align == WR_RIGHT)  { return L->text.x0 + avail - w; }
    return L->text.x0;
}

static int wa_index_at(const WriteApp *a, const WaLayout *L, int px, int py)
{
    int r = a->top + (py - L->text.y0) / WA_LINE_H;
    int col, rx;
    if (r < 0) { r = 0; }
    if (r >= a->nrows) { r = a->nrows - 1; }
    rx = wa_row_x(a, L, r);
    col = (px - rx + WA_GLYPH_W / 2) / WA_GLYPH_W;
    if (col < 0) { col = 0; }
    if (col > a->rows[r].len) { col = a->rows[r].len; }
    return a->rows[r].start + col;
}

/* The face a character's attributes select. */
static GfxFontId wa_face(unsigned char at)
{
    if ((at & WR_BOLD) && (at & WR_ITALIC)) { return GFX_FONT_BOLDITALIC; }
    if (at & WR_BOLD)   { return GFX_FONT_BOLD; }
    if (at & WR_ITALIC) { return GFX_FONT_ITALIC; }
    return GFX_FONT_SYSTEM;
}

/* ---- files ------------------------------------------------------------ */
/*
 * A checksum of the document, DERIVED rather than tracked -- see the note in
 * app_sheet.c. Text, attributes and paragraph alignment all count: making a
 * word bold without typing a character is still a change somebody would be
 * upset to lose.
 */
static cu32 wa_doc_sum(const WriteApp *a)
{
    cu32 h = 2166136261u;
    int i, paras;
    for (i = 0; i < a->doc.len; i++) {
        h = (h ^ (cu32)(unsigned char)a->doc.text[i]) * 16777619u;
        h = (h ^ (cu32)a->doc.attr[i]) * 16777619u;
    }
    paras = wr_para_count(&a->doc);
    if (paras > WR_MAX_PARAS) { paras = WR_MAX_PARAS; }
    for (i = 0; i < paras; i++) {
        h = (h ^ (cu32)a->doc.align[i]) * 16777619u;
    }
    return h;
}

static void wa_on_discard(UiDialogResult result, void *user)
{
    WmWindow *win = (WmWindow *)user;
    if (result == UI_DR_YES && win != NULL) { wm_destroy(win); }
}

/* Closing a document with unsaved changes asks first -- see app_notepad.c. */
/* One predicate, asked by the close box and by the shutdown dialog. */
static cbool wa_has_unsaved(WmWindow *win)
{
    WriteApp *a = (win != NULL) ? (WriteApp *)wm_user(win) : NULL;
    return (a != NULL && wa_doc_sum(a) != a->saved_sum) ? CTRUE : CFALSE;
}

static cbool wa_on_close(WmWindow *win)
{
    WriteApp *a = (win != NULL) ? (WriteApp *)wm_user(win) : NULL;
    char msg[160];
    if (!wa_has_unsaved(win)) { return CFALSE; }
    sys_snprintf(msg, sizeof(msg),
                 "%s has changes you have not saved.\n\n"
                 "Close it and lose them?",
                 (a->path[0] != '\0') ? a->path : "This document");
    ui_msgbox("CastaliaWrite", msg, UI_MB_YESNO, wa_on_discard, win);
    return CTRUE;
}

/* Where documents live, created on demand so the file dialog opens on a real
 * folder rather than on nothing. */
static void wa_save(WriteApp *a, const char *name)
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

    buf = (char *)sys_alloc((cu32)WR_SERIAL_MAX);
    if (buf == NULL) {
        sys_strlcpy(a->status, "Out of memory -- not saved", sizeof a->status);
        return;
    }
    n = wr_serialize(&a->doc, buf, WR_SERIAL_MAX);
    /* The buffer is sized for the worst case, so this cannot normally fire --
     * but never hand plat_fwrite a length the buffer does not hold. */
    if (n < 0 || n >= WR_SERIAL_MAX) {
        sys_strlcpy(a->status, "Document too large to save intact",
                    sizeof a->status);
        sys_free(buf, (cu32)WR_SERIAL_MAX);
        return;
    }
    f = plat_fopen(path, "wb");
    if (f == NULL) {
        sys_snprintf(a->status, sizeof a->status, "Could not save '%s'", name);
        sys_free(buf, (cu32)WR_SERIAL_MAX);
        return;
    }
    plat_fwrite(f, buf, (cu32)n);
    plat_fclose(f);
    sys_free(buf, (cu32)WR_SERIAL_MAX);
    sys_strlcpy(a->path, name, sizeof a->path);
    a->saved_sum = wa_doc_sum(a);
    sys_snprintf(a->status, sizeof a->status, "Saved '%s'", name);
}

static void wa_open(WriteApp *a, const char *name)
{
    char path[CASTALIA_MAX_PATH];
    PlatFile *f;
    char *buf;
    cu32 got;
    int dropped = 0;
    cbool whole;
    office_doc_path(path, sizeof path, name);
    f = plat_fopen(path, "rb");
    if (f == NULL) {
        sys_snprintf(a->status, sizeof a->status, "Could not open '%s'", name);
        return;
    }
    buf = (char *)sys_alloc((cu32)WR_SERIAL_MAX);
    if (buf == NULL) {
        plat_fclose(f);
        sys_strlcpy(a->status, "Out of memory -- not opened", sizeof a->status);
        return;
    }
    got = plat_fread(f, buf, (cu32)WR_SERIAL_MAX - 1);
    plat_fclose(f);
    buf[got] = '\0';
    whole = wr_parse(&a->doc, buf, &dropped);
    sys_free(buf, (cu32)WR_SERIAL_MAX);
    a->top = 0;
    /*
     * A document too long for the model is SHOWN but not adopted: Ctrl+S
     * offers a new name rather than writing the first 4095 characters back
     * over the file they came from, which is what "Opened" used to mean here.
     */
    if (!whole) {
        a->path[0] = '\0';
        sys_snprintf(a->status, sizeof a->status,
                     "'%s' is too long -- %d character%s not shown. Save will "
                     "ask for a new name.", name, dropped,
                     (dropped == 1) ? "" : "s");
        return;
    }
    sys_strlcpy(a->path, name, sizeof a->path);
    a->saved_sum = wa_doc_sum(a);
    sys_snprintf(a->status, sizeof a->status, "Opened '%s'", name);
}

static void wa_on_save(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    WriteApp *a = (win != NULL) ? (WriteApp *)wm_user(win) : NULL;
    if (!ok || a == NULL || text == NULL || text[0] == '\0') { return; }
    wa_save(a, text);
    wm_invalidate(win, NULL);
}
static void wa_on_open(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    WriteApp *a = (win != NULL) ? (WriteApp *)wm_user(win) : NULL;
    if (!ok || a == NULL || text == NULL || text[0] == '\0') { return; }
    wa_open(a, text);
    wm_invalidate(win, NULL);
}

static void wa_sample(WriteApp *a)
{
    static const char *doc =
        "CWRITE1\n"
        ".P 1\n"
        "{b}CastaliaWrite{/b}\n"
        ".P 1\n"
        "{i}The CastaliaOS word processor{/i}\n"
        ".P 0\n"
        "\n"
        ".P 0\n"
        "This is a real document model, not a text box. Type and the text "
        "re-wraps to the page as you go; every character carries its own "
        "{b}bold{/b}, {i}italic{/i} and {u}underline{/u} attributes, and every "
        "paragraph its own alignment.\n"
        ".P 0\n"
        "\n"
        ".P 0\n"
        "Select a run with the mouse, then use the Formatting toolbar or the "
        "Format menu. Save writes a plain, repairable file you can still fix "
        "with any text editor.\n";
    wr_parse(&a->doc, doc, NULL);
    a->saved_sum = wa_doc_sum(a);   /* the welcome text is not unsaved work */
    sys_strlcpy(a->status, "Ready", sizeof a->status);
    sys_strlcpy(a->path, "WELCOME.CWD", sizeof a->path);
}

/* ---- menus ------------------------------------------------------------ */
static void wa_build_menu(WriteApp *a, int idx)
{
    UiMenu *m = &a->menu;
    ui_menu_clear(m);
    switch (idx) {
    case 0:
        ui_menu_add(m, MC_NEW, "New", CTRUE);
        ui_menu_add(m, MC_OPEN, "Open...\tCtrl+O", CTRUE);
        ui_menu_add(m, MC_SAVE, "Save\tCtrl+S", CTRUE);
        ui_menu_add(m, MC_SAVEAS, "Save As...", CTRUE);
        ui_menu_add_separator(m);
        ui_menu_add(m, MC_CLOSE, "Close", CTRUE);
        break;
    case 1:
        ui_menu_add(m, MC_CUT, "Cut", CTRUE);
        ui_menu_add(m, MC_COPY, "Copy", CTRUE);
        ui_menu_add(m, MC_PASTE, "Paste", CTRUE);
        ui_menu_add_separator(m);
        ui_menu_add(m, MC_SELALL, "Select All\tCtrl+A", CTRUE);
        ui_menu_add(m, MC_DELETE, "Delete", CTRUE);
        ui_menu_add_separator(m);
        ui_menu_add(m, MC_FIND, "Find...\tCtrl+F", CTRUE);
        ui_menu_add(m, MC_FINDNEXT, "Find Next",
                    (a->find[0] != '\0') ? CTRUE : CFALSE);
        ui_menu_add(m, MC_REPLACE, "Replace...", CTRUE);
        break;
    case 2:
        ui_menu_add(m, MC_RULER, a->show_ruler ? "Hide Ruler" : "Ruler", CTRUE);
        ui_menu_add(m, MC_STATUS,
                    a->show_status ? "Hide Status Bar" : "Status Bar", CTRUE);
        break;
    case 3:
        ui_menu_add(m, MC_DATE, "Date", CTRUE);
        ui_menu_add(m, MC_TIME, "Time", CTRUE);
        break;
    case 4:
        ui_menu_add(m, MC_BOLD, "Bold\tCtrl+B", CTRUE);
        ui_menu_add(m, MC_ITALIC, "Italic", CTRUE);
        ui_menu_add(m, MC_UNDER, "Underline\tCtrl+U", CTRUE);
        ui_menu_add_separator(m);
        ui_menu_add(m, MC_ALEFT, "Align Left", CTRUE);
        ui_menu_add(m, MC_ACENTER, "Centre", CTRUE);
        ui_menu_add(m, MC_ARIGHT, "Align Right", CTRUE);
        break;
    case 5:
        ui_menu_add(m, MC_WORDCOUNT, "Word Count...", CTRUE);
        break;
    default:
        ui_menu_add(m, MC_ABOUT, "About CastaliaWrite...", CTRUE);
        break;
    }
    m->highlight = -1;
}

/* office_menu_key rebuilds the list as it moves along the bar. */
static void wa_menu_build_cb(void *user, int idx)
{
    wa_build_menu((WriteApp *)user, idx);
}

static void wa_open_menu(WmWindow *win, WriteApp *a, int idx)
{
    WaLayout L;
    CRect w;
    wa_layout(win, a, &L);
    w = office_menu_word(&L.menubar, WA_MENU, idx);
    a->menu_open = idx;
    a->menu_x = w.x0;
    a->menu_y = L.menubar.y1;
    wa_build_menu(a, idx);
}

/* ---- formatting ------------------------------------------------------- */
static void wa_format(WriteApp *a, unsigned char mask)
{
    static const char *nm[8] = { "", "Bold", "Underline", "", "Italic",
                                 "", "", "" };
    const char *label = nm[mask & 7];
    if (wr_has_selection(&a->doc)) {
        int from = wr_sel_start(&a->doc), to = wr_sel_end(&a->doc), i;
        cbool all = CTRUE;
        for (i = from; i < to; i++) {
            if (!(a->doc.attr[i] & mask)) { all = CFALSE; break; }
        }
        wr_apply_attr(&a->doc, from, to, mask, all ? CFALSE : CTRUE);
        sys_snprintf(a->status, sizeof a->status, "%s %s", label,
                     all ? "off" : "on");
    } else {
        a->typing = (unsigned char)(a->typing ^ mask);
        sys_snprintf(a->status, sizeof a->status, "%s %s for new text", label,
                     (a->typing & mask) ? "on" : "off");
    }
}

static void wa_insert_str(WriteApp *a, const char *t)
{
    int i;
    for (i = 0; t[i] != '\0'; i++) { wr_insert(&a->doc, t[i], a->typing); }
}

/* Select the hit so it is visible, and report where it was. */
static void wa_show_hit(WriteApp *a, int hit, int len)
{
    if (hit < 0) {
        sys_snprintf(a->status, sizeof a->status, "'%s' not found", a->find);
        return;
    }
    a->doc.sel = hit;
    a->doc.caret = hit + len;
    sys_snprintf(a->status, sizeof a->status, "Found '%s'", a->find);
}

static void wa_find_from(WriteApp *a, int from)
{
    int len = (int)sys_strnlen(a->find, (cu32)sizeof a->find);
    if (len == 0) { return; }
    wa_show_hit(a, wr_find(&a->doc, a->find, from, CTRUE), len);
}

static void wa_on_find(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    WriteApp *a = (win != NULL) ? (WriteApp *)wm_user(win) : NULL;
    if (!ok || a == NULL || text == NULL || text[0] == '\0') { return; }
    sys_strlcpy(a->find, text, sizeof a->find);
    wa_find_from(a, a->doc.caret);
    wm_invalidate(win, NULL);
}

static void wa_on_replace_with(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    WriteApp *a = (win != NULL) ? (WriteApp *)wm_user(win) : NULL;
    int n;
    if (!ok || a == NULL) { return; }
    sys_strlcpy(a->repl, (text != NULL) ? text : "", sizeof a->repl);
    n = wr_replace_all(&a->doc, a->find, a->repl);
    a->doc.sel = -1;
    a->doc.caret = 0;
    sys_snprintf(a->status, sizeof a->status,
                 (n == 1) ? "Replaced %d occurrence of '%s'"
                          : "Replaced %d occurrences of '%s'", n, a->find);
    wm_invalidate(win, NULL);
}

static void wa_on_replace_what(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    WriteApp *a = (win != NULL) ? (WriteApp *)wm_user(win) : NULL;
    if (!ok || a == NULL || text == NULL || text[0] == '\0') { return; }
    sys_strlcpy(a->find, text, sizeof a->find);
    ui_prompt("Replace", "Replace with:", a->repl, wa_on_replace_with, win);
}

static void wa_command(WmWindow *win, WriteApp *a, int cmd)
{
    char buf[64];
    switch (cmd) {
    case MC_NEW:
        wr_clear(&a->doc);
        a->top = 0; a->typing = 0;
        sys_strlcpy(a->path, "UNTITLED.CWD", sizeof a->path);
        a->saved_sum = wa_doc_sum(a);   /* File > New starts clean */
        sys_strlcpy(a->status, "New document", sizeof a->status);
        break;
    case MC_OPEN:
        ui_file_dialog("Open", office_docs_dir(), "DOC", CFALSE, a->path,
                       wa_on_open, win);
        break;
    case MC_SAVE:
    case MC_SAVEAS:
        ui_file_dialog("Save As", office_docs_dir(), "DOC", CTRUE,
                       (a->path[0] != '\0') ? a->path : "UNTITLED.DOC",
                       wa_on_save, win);
        break;
    case MC_CLOSE:  wm_destroy(win); return;   /* the app is gone; do not touch it */
    case MC_CUT:
    case MC_DELETE: wr_delete_selection(&a->doc); break;
    case MC_COPY:
    case MC_PASTE:
        sys_strlcpy(a->status, "Clipboard: use the Edit keys for now",
                    sizeof a->status);
        break;
    case MC_SELALL:
        a->doc.sel = 0;
        a->doc.caret = a->doc.len;
        break;
    case MC_RULER:  a->show_ruler = !a->show_ruler; break;
    case MC_STATUS: a->show_status = !a->show_status; break;
    case MC_DATE: {
        int y, mo, d, wd;
        plat_wall_date(&y, &mo, &d, &wd);
        sys_snprintf(buf, sizeof buf, "%d-%02d-%02d", y, mo, d);
        wa_insert_str(a, buf);
        break;
    }
    case MC_TIME: {
        int h, mi, se;
        plat_wall_clock(&h, &mi, &se);
        sys_snprintf(buf, sizeof buf, "%02d:%02d", h, mi);
        wa_insert_str(a, buf);
        break;
    }
    case MC_BOLD:   wa_format(a, WR_BOLD);   break;
    case MC_ITALIC: wa_format(a, WR_ITALIC); break;
    case MC_UNDER:  wa_format(a, WR_UNDER);  break;
    case MC_ALEFT:
    case MC_ACENTER:
    case MC_ARIGHT:
        wr_set_align(&a->doc, a->doc.caret, cmd - MC_ALEFT);
        sys_strlcpy(a->status, "Paragraph alignment changed", sizeof a->status);
        break;
    case MC_FIND:
        ui_prompt("Find", "Find what:", a->find, wa_on_find, win);
        break;
    case MC_FINDNEXT:
        wa_find_from(a, a->doc.caret);
        break;
    case MC_REPLACE:
        ui_prompt("Replace", "Find what:", a->find, wa_on_replace_what, win);
        break;
    case MC_WORDCOUNT:
        sys_snprintf(buf, sizeof buf, "%d words, %d characters,\n%d paragraphs.",
                     wr_word_count(&a->doc), wr_char_count(&a->doc),
                     wr_para_count(&a->doc));
        ui_msgbox("Word Count", buf, UI_MB_OK, NULL, NULL);
        break;
    case MC_ABOUT:
        ui_msgbox("About CastaliaWrite",
                  "CastaliaWrite\nThe CastaliaOS word processor.\n\n"
                  "Bold, italic and underline per character;\n"
                  "alignment per paragraph.", UI_MB_OK, NULL, NULL);
        break;
    default: break;
    }
}

static void wa_toolbar_cmd(WmWindow *win, WriteApp *a, int i)
{
    static const int map[TB_N] = {
        MC_NEW, MC_OPEN, MC_SAVE, MC_ABOUT,      /* Print -> honest notice  */
        MC_CUT, MC_COPY, MC_PASTE,
        MC_BOLD, MC_ITALIC, MC_UNDER,
        MC_ALEFT, MC_ACENTER, MC_ARIGHT
    };
    if (i == TB_PRINT) {
        ui_msgbox("Print", "No printer driver in this build yet.\n"
                  "Save the document and print it from DOS.",
                  UI_MB_OK, NULL, NULL);
        return;
    }
    wa_command(win, a, map[i]);
}

/* ---- paint ------------------------------------------------------------ */
static void wa_paint(WmWindow *win, GfxSurface *s)
{
    WriteApp *a = (WriteApp *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    WaLayout L;
    CRect r;
    CColor white = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor workc = GFX_RGB(0x80, 0x84, 0x90);
    char buf[96];
    int i, sel0, sel1, caret_r, align;
    CColor sel_band, sel_ink;

    if (a == NULL) { return; }
    wa_layout(win, a, &L);
    wa_reflow(a, &L);
    /* Painting must NOT chase the caret -- that would undo any scrolling the
     * user just did with the bar. Only keep the view inside the document. */
    if (a->top > a->nrows - L.vis_rows) { a->top = a->nrows - L.vis_rows; }
    if (a->top < 0) { a->top = 0; }
    align = wr_get_align(&a->doc, a->doc.caret);

    /* menu bar */
    r = crect_offset(&L.menubar, o.x, o.y);
    office_menubar(s, &r, WA_MENU, WA_MENU_N, a->menu_open);

    /* the two toolbars */
    for (i = 0; i < 2; i++) {
        r = crect_offset(&L.tbar[i], o.x, o.y);
        office_band(s, &r);
    }
    /* font + size boxes (one face in this build, shown honestly) */
    r = crect_offset(&L.fontbox, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
    {
        CRect tr = r; tr.x0 += 4;
        gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &tr, "Castalia", p->text,
                           GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
    }
    r = crect_offset(&L.sizebox, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, white);
    gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &r, "8", p->text,
                       GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
    for (i = 0; i < TB_N; i++) {
        CRect b = crect_offset(&L.btn[i], o.x, o.y);
        int st = OFB_NORMAL;
        if (i == TB_BOLD   && (a->typing & WR_BOLD))   { st = OFB_CHECKED; }
        if (i == TB_ITALIC && (a->typing & WR_ITALIC)) { st = OFB_CHECKED; }
        if (i == TB_UNDER  && (a->typing & WR_UNDER))  { st = OFB_CHECKED; }
        if (i >= TB_ALEFT && (i - TB_ALEFT) == align)  { st = OFB_CHECKED; }
        if (i == a->hover_tb && st == OFB_NORMAL)      { st = OFB_HOVER; }
        if (WA_TB[i].gap) {
            office_sep(s, b.x0 - 4, L.tbar[WA_TB[i].row].y0 + o.y + 2,
                       OF_TOOLBAR_H - 4);
        }
        office_button(s, &b, WA_TB[i].icon, st);
    }

    /* ruler: a white band spanning the page's text column with tick marks
     * and the indent markers at either margin */
    if (a->show_ruler) {
        CRect pg = crect_offset(&L.page, o.x, o.y);
        int x, k = 0, x0, x1, my;
        r = crect_offset(&L.ruler, o.x, o.y);
        gfx_fill_rect(s, &r, p->face);
        gfx_hline(s, r.x0, r.y1 - 1, crect_w(&r), gfx_tint(p->face, 0x000000, 60));
        x0 = pg.x0 + WA_PAGE_MARGIN;
        x1 = pg.x1 - WA_PAGE_MARGIN;
        {
            CRect band = crect_make(x0, r.y0 + 2, x1 - x0, crect_h(&r) - 5);
            gfx_fill_rect(s, &band, white);
            gfx_frame_rect(s, &band, gfx_tint(p->face, 0x000000, 70));
        }
        for (x = x0 + WA_GLYPH_W * 5; x < x1; x += WA_GLYPH_W * 5) {
            int h = ((k % 2) == 0) ? 4 : 2;
            gfx_vline(s, x, r.y1 - 4 - h, h, p->text_disabled);
            k++;
        }
        my = r.y0 + 2;                       /* indent markers */
        for (k = 0; k < 4; k++) {
            gfx_hline(s, x0 - k, my + k, k * 2 + 1, p->text);
            gfx_hline(s, x1 - k, my + k, k * 2 + 1, p->text);
        }
    }

    /* workspace + page */
    r = crect_offset(&L.work, o.x, o.y);
    gfx_fill_rect(s, &r, workc);
    r = crect_offset(&L.page, o.x, o.y);
    gfx_drop_shadow(s, &r, 4, 110);
    gfx_fill_rect(s, &r, white);
    gfx_frame_rect(s, &r, GFX_RGB(0x50, 0x54, 0x60));

    /* text. A selection in a window nobody is typing into goes quiet -- the
     * same rule the list control and the File Manager already use. */
    ui_sel_colors(wm_has_focus(win), &sel_band, &sel_ink);
    sel0 = wr_sel_start(&a->doc);
    sel1 = wr_sel_end(&a->doc);
    caret_r = wr_row_of(a->rows, a->nrows, a->doc.caret);
    for (i = 0; i < L.vis_rows; i++) {
        int row = a->top + i, rx, ry, k;
        if (row >= a->nrows) { break; }
        rx = wa_row_x(a, &L, row) + o.x;
        ry = L.text.y0 + i * WA_LINE_H + o.y;
        for (k = 0; k < a->rows[row].len; k++) {
            int idx = a->rows[row].start + k;
            int gx = rx + k * WA_GLYPH_W;
            unsigned char at = a->doc.attr[idx];
            cbool selected = (wr_has_selection(&a->doc) &&
                              idx >= sel0 && idx < sel1) ? CTRUE : CFALSE;
            CColor ink = selected ? sel_ink : GFX_RGB(0x10, 0x14, 0x1C);
            char cbuf[2];
            if (selected) {
                CRect hl = crect_make(gx, ry - 1, WA_GLYPH_W, WA_LINE_H);
                gfx_fill_rect(s, &hl, sel_band);
            }
            cbuf[0] = a->doc.text[idx];
            cbuf[1] = '\0';
            gfx_draw_text(s, wa_face(at), gx, ry, cbuf, ink);
            if (at & WR_UNDER) { gfx_hline(s, gx, ry + 8, WA_GLYPH_W, ink); }
        }
        if (row == caret_r && wm_has_focus(win)) {
            int col = a->doc.caret - a->rows[row].start;
            if (col >= 0 && col <= a->rows[row].len) {
                gfx_vline(s, rx + col * WA_GLYPH_W, ry - 1, WA_LINE_H - 1,
                          GFX_RGB(0x10, 0x14, 0x1C));
            }
        }
    }

    /* the document scroll bar */
    r = crect_offset(&L.vbar, o.x, o.y);
    ui_scrollbar_draw(s, &r, CTRUE, a->nrows, L.vis_rows, a->top, a->hot_sb);

    /* status bar: Office-style panels */
    if (a->show_status) {
        int px, pw;
        r = crect_offset(&L.status, o.x, o.y);
        office_statusbar(s, &r);
        px = r.x0 + 2;
        pw = crect_w(&r) - 4 - 230;
        if (pw < 60) { pw = 60; }
        {
            CRect pn = crect_make(px, r.y0 + 2, pw, crect_h(&r) - 4);
            office_status_panel(s, &pn, a->status, GFX_ALIGN_LEFT);
            px += pw + 2;
        }
        sys_snprintf(buf, sizeof buf, "Page 1");
        { CRect pn = crect_make(px, r.y0 + 2, 54, crect_h(&r) - 4);
          office_status_panel(s, &pn, buf, GFX_ALIGN_HCENTER); px += 56; }
        sys_snprintf(buf, sizeof buf, "Ln %d", caret_r + 1);
        { CRect pn = crect_make(px, r.y0 + 2, 52, crect_h(&r) - 4);
          office_status_panel(s, &pn, buf, GFX_ALIGN_HCENTER); px += 54; }
        sys_snprintf(buf, sizeof buf, "Col %d",
                     a->doc.caret - a->rows[caret_r].start + 1);
        { CRect pn = crect_make(px, r.y0 + 2, 56, crect_h(&r) - 4);
          office_status_panel(s, &pn, buf, GFX_ALIGN_HCENTER); px += 58; }
        sys_snprintf(buf, sizeof buf, "%d words", wr_word_count(&a->doc));
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
static void wa_click(WmWindow *win, int px, int py)
{
    WriteApp *a = (WriteApp *)wm_user(win);
    WaLayout L;
    int i;
    if (a == NULL) { return; }
    wa_layout(win, a, &L);
    wa_reflow(a, &L);

    /* an open menu takes the click first */
    if (a->menu_open >= 0) {
        int hit = ui_menu_hit(&a->menu, a->menu_x, a->menu_y,
                              GFX_FONT_SYSTEM, px, py);
        if (hit >= 0) {
            int cmd = a->menu.items[hit].id;
            a->menu_open = -1;
            wa_command(win, a, cmd);
            wm_invalidate(win, NULL);
            return;
        }
        a->menu_open = -1;                  /* click elsewhere dismisses */
    }
    if (crect_contains(&L.menubar, px, py)) {
        for (i = 0; i < WA_MENU_N; i++) {
            CRect w = office_menu_word(&L.menubar, WA_MENU, i);
            if (crect_contains(&w, px, py)) {
                wa_open_menu(win, a, i);
                wm_invalidate(win, NULL);
                return;
            }
        }
        wm_invalidate(win, NULL);
        return;
    }
    for (i = 0; i < TB_N; i++) {
        if (crect_contains(&L.btn[i], px, py)) {
            wa_toolbar_cmd(win, a, i);
            wm_invalidate(win, NULL);
            return;
        }
    }
    if (crect_contains(&L.vbar, px, py)) {
        int part = ui_scrollbar_hit(&L.vbar, CTRUE, a->nrows, L.vis_rows,
                                    a->top, px, py);
        a->hot_sb = part;
        switch (part) {
        case UI_SB_LINE_UP:   a->top--; break;
        case UI_SB_LINE_DOWN: a->top++; break;
        case UI_SB_PAGE_UP:   a->top -= L.vis_rows; break;
        case UI_SB_PAGE_DOWN: a->top += L.vis_rows; break;
        case UI_SB_THUMB:     a->sb_drag = CTRUE; break;
        default: break;
        }
        if (a->top > a->nrows - L.vis_rows) { a->top = a->nrows - L.vis_rows; }
        if (a->top < 0) { a->top = 0; }
        {   /* the page and the bar; the toolbars, ruler and status did not
             * move -- the same region the thumb drag uses */
            CPoint o = wm_client_origin(win);
            CRect rr = crect_offset(&L.work, o.x, o.y);
            wm_invalidate(win, &rr);
            rr = crect_offset(&L.vbar, o.x, o.y);
            wm_invalidate(win, &rr);
        }
        return;
    }
    if (crect_contains(&L.text, px, py) || crect_contains(&L.page, px, py)) {
        a->doc.caret = wa_index_at(a, &L, px, py);
        a->doc.sel = a->doc.caret;
        a->dragging = CTRUE;
        wm_invalidate(win, NULL);
    }
}

static void wa_motion(WmWindow *win, int px, int py)
{
    WriteApp *a = (WriteApp *)wm_user(win);
    WaLayout L;
    int i, hov = -1;
    if (a == NULL) { return; }
    wa_layout(win, a, &L);

    if (a->sb_drag) {
        int was_top = a->top;
        wa_reflow(a, &L);
        a->top = ui_scroll_pos_from_coord(crect_h(&L.vbar), a->nrows,
                                          L.vis_rows, py - L.vbar.y0);
        /* The page and the thumb moved; the two toolbars, the ruler and the
         * status line did not. On the transition only. */
        if (a->top != was_top) {
            CPoint o = wm_client_origin(win);
            CRect r = crect_offset(&L.work, o.x, o.y);
            wm_invalidate(win, &r);
            r = crect_offset(&L.vbar, o.x, o.y);
            wm_invalidate(win, &r);
        }
        return;
    }
    if (a->dragging) {
        wa_reflow(a, &L);
        a->doc.caret = wa_index_at(a, &L, px, py);
        wm_invalidate(win, NULL);
        return;
    }
    /* hovering the menu bar with a menu open slides to that menu */
    if (a->menu_open >= 0 && crect_contains(&L.menubar, px, py)) {
        for (i = 0; i < WA_MENU_N; i++) {
            CRect w = office_menu_word(&L.menubar, WA_MENU, i);
            if (crect_contains(&w, px, py) && i != a->menu_open) {
                wa_open_menu(win, a, i);
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
    for (i = 0; i < TB_N; i++) {
        if (crect_contains(&L.btn[i], px, py)) { hov = i; break; }
    }
    if (hov != a->hover_tb) {
        a->hover_tb = hov;
        wm_invalidate(win, NULL);
    }
}

static void wa_up(WmWindow *win)
{
    WriteApp *a = (WriteApp *)wm_user(win);
    if (a == NULL) { return; }
    /*
     * Letting go of the scroll bar puts it back. ui_scrollbar_draw takes
     * hot_sb and draws that part active, so a thumb released with nothing
     * repainting the bar stayed drawn as though it were still held. It was
     * hidden for as long as every motion event during the drag repainted the
     * window: the release landed on a bar the last of those had drawn hot.
     */
    {
        cbool was_sb = (a->sb_drag || a->hot_sb != UI_SB_NONE) ? CTRUE : CFALSE;
        a->dragging = CFALSE;
        a->sb_drag = CFALSE;
        a->hot_sb = UI_SB_NONE;
        if (was_sb) {
            WaLayout L;
            CPoint o = wm_client_origin(win);
            CRect r;
            wa_layout(win, a, &L);
            r = crect_offset(&L.vbar, o.x, o.y);
            wm_invalidate(win, &r);
        }
    }
    if (a->doc.sel == a->doc.caret) { a->doc.sel = -1; }
}

/*
 * Every user edit goes through these two so it can be taken back WITH its
 * formatting. wr_insert / wr_backspace / wr_delete are still called directly
 * where a document is being LOADED, which is not an edit the user made.
 */
static void wa_insert(WriteApp *a, char ch)
{
    char c = ch;
    unsigned char at = a->typing;
    if (!wr_insert(&a->doc, ch, a->typing)) { return; }
    /* Recorded at the position it landed, which is one back from the caret
     * wr_insert has just advanced past it. */
    wundo_record(&a->undo, UNDO_INSERT, a->doc.caret - 1, &c, &at, 1);
}

static void wa_remove(WriteApp *a, cbool before)
{
    int at_pos = before ? a->doc.caret - 1 : a->doc.caret;
    char c;
    unsigned char attr;
    if (at_pos < 0 || at_pos >= a->doc.len) { return; }
    /* Captured BEFORE the removal: the character and its emphasis are what an
     * undo has to put back, and neither survives the delete. */
    c = a->doc.text[at_pos];
    attr = a->doc.attr[at_pos];
    if (before) { wr_backspace(&a->doc); } else { wr_delete(&a->doc); }
    wundo_record(&a->undo, UNDO_DELETE, at_pos, &c, &attr, 1);
}

/* Apply one step, in whichever direction. */
static cbool wa_undo_apply(WriteApp *a, const WUndoRec *r, cbool reverse)
{
    int kind, i;
    if (r == NULL) { return CFALSE; }
    /* Undo reverses the record; redo replays it. */
    kind = r->kind;
    if (reverse) {
        kind = (kind == UNDO_INSERT) ? UNDO_DELETE : UNDO_INSERT;
    }
    if (kind == UNDO_DELETE) {
        a->doc.caret = r->pos;
        for (i = 0; i < r->len; i++) { wr_delete(&a->doc); }
    } else {
        a->doc.caret = r->pos;
        for (i = 0; i < r->len; i++) {
            /* The attribute goes back with the character -- restoring the
             * words without their emphasis is the partial undo this stack
             * exists to avoid. */
            wr_insert(&a->doc, r->text[i], r->attr[i]);
        }
        a->doc.caret = r->pos + r->len;
    }
    return CTRUE;
}

/*
 * Typing repainted the WHOLE window: 325,725 pixels per character, 107% of
 * the client. Measured by --cost-demo off the shell's own frame counter.
 *
 * Every key reflows, so the line breaks UNDER the caret can move and the
 * rows below it really may all be different -- but the rows ABOVE it cannot
 * be, and neither can the ruler, the toolbars or the page margin. The band
 * runs from the caret's row to the bottom of the text area, which is the
 * whole page when the caret is on the first line and almost nothing when it
 * is on the last, where documents are usually being typed.
 *
 * A scroll or a selection at either end still repaints whole: working out
 * the minimum there is harder than it is worth and neither is the key
 * pressed thirty times in a row.
 */
typedef struct { int top, row, nrows, sel; } WaMark;

static void wa_mark(WriteApp *a, WaMark *m)
{
    m->top = a->top;
    m->nrows = a->nrows;
    m->sel = a->doc.sel;
    m->row = wr_row_of(a->rows, a->nrows, a->doc.caret);
}

static void wa_invalidate_edit(WmWindow *win, WriteApp *a, const WaLayout *L,
                               const WaMark *was)
{
    CPoint o = wm_client_origin(win);
    WaMark now;
    CRect r;
    int r0;
    wa_mark(a, &now);

    r = crect_offset(&L->status, o.x, o.y);
    wm_invalidate(win, &r);

    if (now.top != was->top || now.sel != was->sel ||
        a->doc.sel >= 0 || was->sel >= 0) {
        r = crect_offset(&L->work, o.x, o.y);
        wm_invalidate(win, &r);
        r = crect_offset(&L->vbar, o.x, o.y);
        wm_invalidate(win, &r);
        return;
    }

    /*
     * From the caret's row to the foot of the page, and TWO PIXELS ABOVE it.
     *
     * The two pixels are the whole story of a bug this took a while to
     * find. Started exactly at the row's top edge, every Enter left one
     * stale pixel behind -- one per row, all at the same x, ink on a white
     * page. The caret is drawn a little taller than the row it sits on, so
     * its top edge lives in the row ABOVE, and a band that began at the row
     * boundary erased all of it but that.
     *
     * One pixel is not something anybody would see in a screenshot, and it
     * is exactly what "a partial repaint must leave the window looking as a
     * full one would" is for. The clock's tick region carries the same
     * allowance, for the same reason and in the same words.
     */
    r0 = (now.row < was->row) ? now.row : was->row;
    {
        int y0 = L->text.y0 + (r0 - now.top) * WA_LINE_H - 2;
        CRect band;
        /*
         * Clamped to the PAGE, not to the text. Clamping to the text throws
         * the two pixels away on row zero -- the one row where the caret's
         * overhang has nowhere above it to live -- and that is the last
         * stale pixel this took to find.
         */
        if (y0 < L->page.y0) { y0 = L->page.y0; }
        band = crect_make(L->page.x0, y0, crect_w(&L->page), L->page.y1 - y0);
        band = crect_offset(&band, o.x, o.y);
        wm_invalidate(win, &band);
    }
    if (now.nrows != was->nrows) {
        r = crect_offset(&L->vbar, o.x, o.y);
        wm_invalidate(win, &r);
    }
}

static cbool wa_key(WmWindow *win, int key, int ch, int mods)
{
    WriteApp *a = (WriteApp *)wm_user(win);
    WaLayout L;
    WaMark before;
    int row, col;
    if (a == NULL) { return CFALSE; }
    /*
     * The menu bar, from the keyboard: F10 opens it, the arrows walk it,
     * Enter chooses, Esc closes. These menus were mouse-only, and on the
     * target the mouse is a driver somebody may not have loaded.
     */
    {
        OfficeMenuNav nav;
        WaLayout ML;
        int mcmd = -1;
        wa_layout(win, a, &ML);
        nav.open = &a->menu_open; nav.x = &a->menu_x; nav.y = &a->menu_y;
        nav.menu = &a->menu; nav.names = WA_MENU; nav.count = WA_MENU_N;
        nav.bar = ML.menubar; nav.build = wa_menu_build_cb; nav.user = a;
        if (office_menu_key_win(win, &nav, key, &mcmd)) {
            if (mcmd >= 0) { wa_command(win, a, mcmd); wm_invalidate(win, NULL); }
            return CTRUE;
        }
    }
    wa_layout(win, a, &L);
    wa_reflow(a, &L);
    /* Captured AFTER the reflow that precedes the key, so it describes the
     * page as it is on screen right now. */
    wa_mark(a, &before);
    row = wr_row_of(a->rows, a->nrows, a->doc.caret);
    col = a->doc.caret - a->rows[row].start;

    if (WA_CTRL(key, ch, mods, 26, 'z')) {          /* Ctrl+Z */
        if (wa_undo_apply(a, wundo_undo_step(&a->undo), CTRUE)) {
            a->doc.sel = -1;
            sys_strlcpy(a->status, "Undo", sizeof a->status);
        } else {
            sys_strlcpy(a->status, "Nothing to undo", sizeof a->status);
        }
        wm_invalidate(win, NULL);
        return CTRUE;
    }
    if (WA_CTRL(key, ch, mods, 25, 'y')) {          /* Ctrl+Y */
        if (wa_undo_apply(a, wundo_redo_step(&a->undo), CFALSE)) {
            a->doc.sel = -1;
            sys_strlcpy(a->status, "Redo", sizeof a->status);
        } else {
            sys_strlcpy(a->status, "Nothing to redo", sizeof a->status);
        }
        wm_invalidate(win, NULL);
        return CTRUE;
    }
    /*
     * The formatting and file shortcuts. Help has promised these since the
     * word processor was written and not one of them existed: the menu was
     * the only way in. A Help page that lists a keystroke the program ignores
     * is worse than no Help page, because the reader concludes the keyboard
     * is broken rather than the documentation.
     *
     * Ctrl+I is the exception, and it stays absent on purpose: it arrives as
     * ASCII 9, which is indistinguishable from Tab, so mapping it would turn
     * every Tab in the document into italics. Help now says so.
     */
    if (WA_CTRL(key, ch, mods, 2, 'b')) {  wa_command(win, a, MC_BOLD);  goto handled; }
    if (WA_CTRL(key, ch, mods, 21, 'u')) { wa_command(win, a, MC_UNDER); goto handled; }
    if (WA_CTRL(key, ch, mods, 19, 's')) { wa_command(win, a, MC_SAVE);  goto handled; }
    if (WA_CTRL(key, ch, mods, 15, 'o')) { wa_command(win, a, MC_OPEN);  goto handled; }
    if (WA_CTRL(key, ch, mods, 1, 'a')) {  wa_command(win, a, MC_SELALL); goto handled; }
    if (WA_CTRL(key, ch, mods, 6, 'f')) {  wa_command(win, a, MC_FIND);  goto handled; }

    switch (key) {
    case PLAT_KEY_LEFT:
        if (a->doc.caret > 0) { a->doc.caret--; }
        a->doc.sel = -1;
        break;
    case PLAT_KEY_RIGHT:
        if (a->doc.caret < a->doc.len) { a->doc.caret++; }
        a->doc.sel = -1;
        break;
    case PLAT_KEY_UP:
        if (row > 0) {
            int c = (col < a->rows[row - 1].len) ? col : a->rows[row - 1].len;
            a->doc.caret = a->rows[row - 1].start + c;
        }
        a->doc.sel = -1;
        break;
    case PLAT_KEY_DOWN:
        if (row + 1 < a->nrows) {
            int c = (col < a->rows[row + 1].len) ? col : a->rows[row + 1].len;
            a->doc.caret = a->rows[row + 1].start + c;
        }
        a->doc.sel = -1;
        break;
    case PLAT_KEY_HOME: a->doc.caret = a->rows[row].start; a->doc.sel = -1; break;
    case PLAT_KEY_END:
        a->doc.caret = a->rows[row].start + a->rows[row].len;
        a->doc.sel = -1;
        break;
    case PLAT_KEY_PGUP:
        a->top -= L.vis_rows;
        if (a->top < 0) { a->top = 0; }
        a->doc.caret = a->rows[a->top].start;
        a->doc.sel = -1;
        break;
    case PLAT_KEY_PGDN:
        a->top += L.vis_rows;
        if (a->top > a->nrows - 1) { a->top = a->nrows - 1; }
        if (a->top < 0) { a->top = 0; }
        a->doc.caret = a->rows[a->top].start;
        a->doc.sel = -1;
        break;
    case PLAT_KEY_ENTER:  wa_insert(a, '\n'); break;
    case PLAT_KEY_BACKSP: wa_remove(a, CTRUE); break;
    case PLAT_KEY_DELETE: wa_remove(a, CFALSE); break;
    case PLAT_KEY_TAB:
        wa_insert(a, ' ');
        wa_insert(a, ' ');
        break;
    default:
        if (ch >= 32 && ch < 127) {
            if (wr_has_selection(&a->doc)) { wr_delete_selection(&a->doc); }
            wa_insert(a, (char)ch);
            break;
        }
        return CFALSE;
    }
handled:
    wa_reflow(a, &L);
    wa_scroll_to_caret(a, &L);
    wa_invalidate_edit(win, a, &L, &before);
    return CTRUE;
}

static cbool write_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    switch (msg) {
    case WM_MSG_CLOSE:       return wa_on_close(win);
    case WM_MSG_QUERY_UNSAVED: return wa_has_unsaved(win);
    case WM_MSG_PAINT:       wa_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: wa_click(win, (int)a, (int)b);      return CTRUE;
    case WM_MSG_MOUSEMOVE:   wa_motion(win, (int)a, (int)b);     return CTRUE;
    case WM_MSG_LBUTTONUP:   wa_up(win);                         return CTRUE;
    case WM_MSG_MOUSEWHEEL: {
        WriteApp *wa = (WriteApp *)wm_user(win);
        WaLayout L;
        if (wa == NULL) { return CFALSE; }
        wa_layout(win, wa, &L);
        wa_reflow(wa, &L);
        if (ui_scroll_wheel(&wa->top, (int)a, wa->nrows, L.vis_rows)) {
            CPoint o = wm_client_origin(win);
            CRect r = crect_offset(&L.work, o.x, o.y);
            wm_invalidate(win, &r);
            r = crect_offset(&L.vbar, o.x, o.y);
            wm_invalidate(win, &r);
        }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:
        return wa_key(win, (int)a, (int)b,
                      (param != NULL) ? *(const int *)param : 0);
    case WM_MSG_DESTROY: {
        WriteApp *wa = (WriteApp *)wm_user(win);
        if (wa != NULL) { sys_free(wa, (cu32)sizeof(WriteApp)); }
        return CTRUE;
    }
    default: return CFALSE;
    }
}

static WmWindow *wa_open_window(void)
{
    WriteApp *a;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw = 640, ch = 500;

    a = (WriteApp *)sys_calloc(1, (cu32)sizeof(WriteApp));
    if (a == NULL) { SYS_LOGE("app", "write: OOM"); return NULL; }
    wr_clear(&a->doc);
    a->hover_tb = -1;
    a->menu_open = -1;
    a->show_ruler = CTRUE;
    a->show_status = CTRUE;
    wa_sample(a);
    a->doc.sel = -1;

    plat_video_info(&vi);
    /* The work area, not the screen -- 46 pixels of this window were under
     * the taskbar at 640x480. */
    frame = wm_place_centered(cw, ch);

    w = wm_create("Document1 - CastaliaWrite", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_MAXIMIZE | WM_STYLE_BORDER, write_proc, a);
    if (w == NULL) { sys_free(a, (cu32)sizeof(WriteApp)); return NULL; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened CastaliaWrite");
    return w;
}

const char *app_write_text(WmWindow *win)
{
    WriteApp *a = (WriteApp *)wm_user(win);
    if (a == NULL) { return ""; }
    a->doc.text[a->doc.len] = '\0';
    return a->doc.text;
}

int app_write_attr_at(WmWindow *win, int index)
{
    WriteApp *a = (WriteApp *)wm_user(win);
    if (a == NULL || index < 0 || index >= a->doc.len) { return -1; }
    return (int)a->doc.attr[index];
}

void app_write_open(void) { (void)wa_open_window(); }

/* Open the word processor on a document -- what a double-click on one does. */
void app_write_open_file(const char *path)
{
    WmWindow *w = wa_open_window();
    WriteApp *a = (w != NULL) ? (WriteApp *)wm_user(w) : NULL;
    if (a == NULL || path == NULL || path[0] == '\0') { return; }
    wa_open(a, path);
    wm_invalidate(w, NULL);
}
