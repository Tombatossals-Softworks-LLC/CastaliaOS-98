/*
 * ui_filedlg.c - The Open / Save As dialog.
 *
 * A modal window over the same seam as ui_prompt: it does not block the
 * cooperative loop, it calls back when the user picks something. What it adds
 * is the listing -- folders you can walk into, a filter on the extension the
 * caller cares about, a filename field, and a scroll bar when the folder is
 * bigger than the box.
 *
 * The path arithmetic lives in ui_path.c, which is pure and host-tested; this
 * file is the window around it.
 */
#include "castalia/ui.h"
#include "castalia/wm.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#include <stdlib.h>
#include <string.h>

#define FD_MAX_ENTRIES 128
#define FD_ROW         12
#define FD_BTN_W       62
#define FD_BTN_H       20
#define FD_PAD         8

typedef struct {
    char  name[CASTALIA_MAX_NAME];
    cbool is_dir;
} FdEntry;

typedef struct {
    char      dir[CASTALIA_MAX_PATH];
    char      ext[8];
    cbool     saving;
    UiFileCb  cb;
    void     *user;
    FdEntry   ent[FD_MAX_ENTRIES];
    int       count;
    int       sel;
    int       top;
    int       truncated;         /* entries the folder had beyond our room  */
    UiEdit    edit;
    UiHot     hot;               /* Open/Save and Cancel under the pointer  */
} FileDlg;

/* ---- listing ----------------------------------------------------------- */
static void fd_sort(FileDlg *d)
{
    int i, j;
    /* Insertion sort: a folder listing is small and this keeps it stable. */
    for (i = 1; i < d->count; i++) {
        FdEntry key = d->ent[i];
        j = i - 1;
        while (j >= 0 &&
               ui_path_compare(d->ent[j].name, d->ent[j].is_dir,
                               key.name, key.is_dir) > 0) {
            d->ent[j + 1] = d->ent[j];
            j--;
        }
        d->ent[j + 1] = key;
    }
}

static void fd_scan(FileDlg *d)
{
    PlatDir *dir;
    PlatDirEntry e;

    d->count = 0;
    d->sel = -1;
    d->top = 0;
    d->truncated = 0;

    /* The way up, unless there is nowhere to go. */
    {
        char probe[CASTALIA_MAX_PATH];
        sys_strlcpy(probe, d->dir, sizeof probe);
        if (ui_path_up(probe)) {
            sys_strlcpy(d->ent[0].name, "..", sizeof d->ent[0].name);
            d->ent[0].is_dir = CTRUE;
            d->count = 1;
        }
    }

    dir = plat_opendir(d->dir);
    if (dir == NULL) { return; }
    while (plat_readdir(dir, &e)) {
        if (e.name[0] == '\0') { continue; }
        if (e.name[0] == '.' && e.name[1] == '\0') { continue; }
        if (e.name[0] == '.' && e.name[1] == '.' && e.name[2] == '\0') {
            continue;                       /* we added our own ".." above  */
        }
        if (!e.is_dir && !ui_path_match_ext(e.name, d->ext)) { continue; }
        if (d->count >= FD_MAX_ENTRIES) { d->truncated++; continue; }
        sys_strlcpy(d->ent[d->count].name, e.name,
                    sizeof d->ent[d->count].name);
        d->ent[d->count].is_dir = e.is_dir;
        d->count++;
    }
    plat_closedir(dir);
    fd_sort(d);
}

/* ---- geometry ---------------------------------------------------------- */
static CRect fd_list_rect(WmWindow *win)
{
    CRect c = wm_client_rect(win);
    return crect_make(FD_PAD, 26, crect_w(&c) - 2 * FD_PAD,
                      crect_h(&c) - 26 - FD_BTN_H - 22 - 3 * FD_PAD);
}

static CRect fd_bar_rect(WmWindow *win)
{
    CRect l = fd_list_rect(win);
    int h = crect_h(&l) - 2;
    return crect_make(l.x1 - UI_SB_W - 1, l.y0 + 1, UI_SB_W, h);
}

static CRect fd_edit_rect(WmWindow *win)
{
    CRect c = wm_client_rect(win);
    CRect l = fd_list_rect(win);
    return crect_make(FD_PAD + 52, l.y1 + FD_PAD, crect_w(&c) - 2 * FD_PAD - 52,
                      18);
}

static CRect fd_btn_rect(WmWindow *win, int i)
{
    CRect c = wm_client_rect(win);
    int ch = crect_h(&c);
    int x = crect_w(&c) - FD_PAD - (2 - i) * (FD_BTN_W + 8) + 8;
    return crect_make(x, ch - FD_BTN_H - FD_PAD, FD_BTN_W, FD_BTN_H);
}

static int fd_rows(WmWindow *win)
{
    CRect l = fd_list_rect(win);
    int n = (crect_h(&l) - 4) / FD_ROW;
    return (n < 1) ? 1 : n;
}

/* ---- result ------------------------------------------------------------ */
/*
 * Save As did not ask before replacing a file.
 *
 * Every app in the system saves through this one dialog, so "type the name of
 * something that is already there and it is gone" was true of Notepad, Paint,
 * CastaliaWrite, CastaliaSheet and the Theme Editor at once -- the most
 * ordinary overwrite path a desktop has, and the only one none of this
 * session's other guards covered.
 *
 * The question is asked HERE rather than in each app for the same reason the
 * batch rename is one module: five copies of "does this file exist" would
 * eventually disagree, and the one that said no would be the one that
 * overwrote something.
 *
 * A single pending slot is enough because the dialog is modal -- there is
 * never a second one waiting on an answer.
 */
static WmWindow *g_fd_replace_win;
static char      g_fd_replace_path[CASTALIA_MAX_PATH];

static void fd_hand_back(WmWindow *win, FileDlg *d, const char *path)
{
    char keep[CASTALIA_MAX_PATH];
    UiFileCb cb = d->cb;
    void *user = d->user;
    keep[0] = '\0';
    if (path != NULL) { sys_strlcpy(keep, path, sizeof keep); }
    wm_destroy(win);                     /* frees d via WM_MSG_DESTROY */
    if (cb != NULL) { cb((path != NULL) ? CTRUE : CFALSE,
                         (path != NULL) ? keep : NULL, user); }
}

static void fd_replace_answer(UiDialogResult result, void *user)
{
    WmWindow *win = g_fd_replace_win;
    FileDlg *d = (win != NULL) ? (FileDlg *)wm_user(win) : NULL;
    (void)user;
    g_fd_replace_win = NULL;
    /* Anything but yes leaves the file dialog standing, which is where
     * somebody who did not mean to replace it wants to be. */
    if (result != UI_DR_YES || win == NULL || d == NULL) { return; }
    fd_hand_back(win, d, g_fd_replace_path);
}

static void fd_finish(WmWindow *win, FileDlg *d, cbool ok)
{
    char path[CASTALIA_MAX_PATH];
    path[0] = '\0';
    if (!ok) { fd_hand_back(win, d, NULL); return; }
    ui_path_join(path, sizeof path, d->dir, ui_edit_text(&d->edit));
    if (d->saving && plat_file_size(path) >= 0) {
        char msg[192];
        sys_snprintf(msg, sizeof msg,
                     "%s already exists.\n\nReplace it?",
                     ui_path_base(path));
        g_fd_replace_win = win;
        sys_strlcpy(g_fd_replace_path, path, sizeof g_fd_replace_path);
        ui_msgbox("Save As", msg, UI_MB_YESNO, fd_replace_answer, NULL);
        return;
    }
    fd_hand_back(win, d, path);
}

/* Act on the selected row: walk into a folder, or accept a file. */
static void fd_activate(WmWindow *win, FileDlg *d)
{
    char next[CASTALIA_MAX_PATH];
    if (d->sel < 0 || d->sel >= d->count) {
        if (ui_edit_text(&d->edit)[0] != '\0') { fd_finish(win, d, CTRUE); }
        return;
    }
    if (!d->ent[d->sel].is_dir) {
        ui_edit_init(&d->edit, d->ent[d->sel].name);
        fd_finish(win, d, CTRUE);
        return;
    }
    if (d->ent[d->sel].name[0] == '.' && d->ent[d->sel].name[1] == '.') {
        ui_path_up(d->dir);
    } else {
        ui_path_join(next, sizeof next, d->dir, d->ent[d->sel].name);
        sys_strlcpy(d->dir, next, sizeof d->dir);
    }
    fd_scan(d);
    wm_invalidate(win, NULL);
}

/* ---- painting ---------------------------------------------------------- */
static void fd_paint(WmWindow *win, GfxSurface *s)
{
    FileDlg *d = (FileDlg *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    CRect c, r, well, saved;
    char buf[CASTALIA_MAX_PATH + 16];
    int rows, i;

    if (d == NULL) { return; }
    /* wm_client_rect already reports screen coordinates; only the layout
     * helpers below are client-relative and need the origin added. */
    c = wm_client_rect(win);
    r = c;
    gfx_fill_rect(s, &r, p->face);

    /* Where we are. */
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + FD_PAD, r.y0 + 8, "Look in:",
                  p->text);
    {
        CRect box = crect_make(r.x0 + FD_PAD + 52, r.y0 + 5,
                               crect_w(&c) - 2 * FD_PAD - 52, 16);
        CRect in;
        gfx_bevel(s, &box, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark,
                  GFX_RGB(0xFF, 0xFF, 0xFF));
        in = crect_inset(&box, 2);
        saved = gfx_clip_narrow(s, &in);
        gfx_draw_text(s, GFX_FONT_SYSTEM, in.x0 + 2, in.y0 + 2, d->dir,
                      p->text);
        gfx_set_clip(s, &saved);
    }

    /* The listing. */
    { CRect lr = fd_list_rect(win); well = crect_offset(&lr, o.x, o.y); }
    gfx_bevel(s, &well, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark,
              GFX_RGB(0xFF, 0xFF, 0xFF));
    rows = fd_rows(win);
    if (d->top > d->count - rows) { d->top = d->count - rows; }
    if (d->top < 0) { d->top = 0; }
    {
        CRect in = crect_inset(&well, 2);
        int roww = crect_w(&in) - ((d->count > rows) ? UI_SB_W + 1 : 0);
        saved = gfx_clip_narrow(s, &in);
        if (d->count == 0) {
            gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &in, "(empty)",
                               p->text_disabled,
                               GFX_ALIGN_HCENTER | GFX_ALIGN_VCENTER);
        }
        for (i = 0; i < rows && d->top + i < d->count; i++) {
            const FdEntry *e = &d->ent[d->top + i];
            CRect row = crect_make(in.x0, in.y0 + i * FD_ROW, roww, FD_ROW);
            CColor tc = p->text;
            if (d->top + i == d->sel) {
                gfx_fill_rect(s, &row, p->accent);
                tc = p->accent_text;
            }
            /* Folders read as [NAME], the way a text-mode listing always
             * marked them -- no icon needed to tell them apart. */
            if (e->is_dir) {
                sys_snprintf(buf, sizeof buf, "[%s]", e->name);
            } else {
                sys_strlcpy(buf, e->name, sizeof buf);
            }
            gfx_draw_text(s, GFX_FONT_SYSTEM, row.x0 + 4, row.y0 + 2, buf, tc);
        }
        gfx_set_clip(s, &saved);
    }
    if (d->count > rows) {
        CRect br = fd_bar_rect(win);
        CRect bar = crect_offset(&br, o.x, o.y);
        ui_scrollbar_draw(s, &bar, CTRUE, d->count, rows, d->top, UI_SB_NONE);
    }

    /* Filename field. */
    {
        CRect ed = fd_edit_rect(win);
        CRect er = crect_offset(&ed, o.x, o.y);
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + FD_PAD, er.y0 + 5,
                      d->saving ? "Save as:" : "File:", p->text);
        ui_edit_draw(s, &er, &d->edit, CTRUE);
    }

    /* Buttons, and what the filter is showing. */
    for (i = 0; i < 2; i++) {
        CRect br = fd_btn_rect(win, i);
        CRect b = crect_offset(&br, o.x, o.y);
        /* Enter accepts here, whatever the keyboard is on, so Open/Save
         * carries the ring that says so. */
        if (i == 0) { ui_draw_button_default(s, &b); }
        ui_draw_button(s, &b, (i == 0) ? (d->saving ? "Save" : "Open") : "Cancel",
                       ui_hot_state(&d->hot, i, UI_BTN_NORMAL));
    }
    if (d->ext[0] != '\0') {
        sys_snprintf(buf, sizeof buf, "*.%s", d->ext);
    } else {
        sys_strlcpy(buf, "All files", sizeof buf);
    }
    if (d->truncated > 0) {
        char more[32];
        sys_snprintf(more, sizeof more, "  (+%d more)", d->truncated);
        sys_strlcpy(buf + sys_strnlen(buf, sizeof buf), more,
                    sizeof buf - sys_strnlen(buf, sizeof buf));
    }
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + FD_PAD,
                  r.y1 - FD_PAD - FD_BTN_H + 6, buf, p->text_disabled);
}

/* ---- input ------------------------------------------------------------- */
static cbool fd_click(WmWindow *win, FileDlg *d, int px, int py)
{
    CRect list = fd_list_rect(win);
    int rows = fd_rows(win);
    int i;

    for (i = 0; i < 2; i++) {
        CRect b = fd_btn_rect(win, i);
        if (crect_contains(&b, px, py)) {
            (void)ui_hot_press(&d->hot, i);
            wm_invalidate(win, NULL);
            return CTRUE;
        }
    }
    if (d->count > rows) {
        CRect bar = fd_bar_rect(win);
        if (crect_contains(&bar, px, py)) {
            int part = ui_scrollbar_hit(&bar, CTRUE, d->count, rows, d->top,
                                        px, py);
            if (part == UI_SB_LINE_UP)        { d->top--; }
            else if (part == UI_SB_LINE_DOWN) { d->top++; }
            else if (part == UI_SB_PAGE_UP)   { d->top -= rows; }
            else if (part == UI_SB_PAGE_DOWN) { d->top += rows; }
            else if (part == UI_SB_THUMB) {
                d->top = ui_scroll_pos_from_coord(crect_h(&bar), d->count,
                                                  rows, py - bar.y0);
            }
            /* The rows and the bar; the path strip, the name field and the
             * two buttons did not move. Offset to SCREEN coordinates --
             * fd_list_rect and fd_bar_rect answer in client ones, which is
             * what the hit test above wants and not what wm_invalidate
             * does. */
            {
                CPoint fo = wm_client_origin(win);
                CRect r = crect_union(&list, &bar);
                r = crect_offset(&r, fo.x, fo.y);
                wm_invalidate(win, &r);
            }
            return CTRUE;
        }
    }
    if (crect_contains(&list, px, py)) {
        int row = (py - list.y0 - 2) / FD_ROW;
        if (row >= 0 && row < rows && d->top + row < d->count) {
            int hit = d->top + row;
            if (hit == d->sel) {              /* second click: go / accept  */
                fd_activate(win, d);
                return CTRUE;
            }
            d->sel = hit;
            if (!d->ent[hit].is_dir) {
                ui_edit_init(&d->edit, d->ent[hit].name);
            }
        }
        wm_invalidate(win, NULL);
        return CTRUE;
    }
    return CFALSE;
}

/* Which of the two buttons is at (px,py), or -1. */
static int fd_btn_at(WmWindow *win, int px, int py)
{
    int i;
    for (i = 0; i < 2; i++) {
        CRect b = fd_btn_rect(win, i);
        if (crect_contains(&b, px, py)) { return i; }
    }
    return -1;
}

static cbool fd_up(WmWindow *win, FileDlg *d, int px, int py)
{
    int was = ui_hot_pressed(&d->hot);
    (void)ui_hot_release(&d->hot);
    if (was < 0) { return CFALSE; }
    {
        CRect b = fd_btn_rect(win, was);
        if (crect_contains(&b, px, py)) {
            if (was == 0) {
                if (d->sel >= 0 && d->sel < d->count && d->ent[d->sel].is_dir) {
                    fd_activate(win, d);     /* Open on a folder walks in   */
                } else if (ui_edit_text(&d->edit)[0] != '\0') {
                    fd_finish(win, d, CTRUE);
                }
            } else {
                fd_finish(win, d, CFALSE);
            }
            return CTRUE;
        }
    }
    wm_invalidate(win, NULL);
    return CTRUE;
}

static cbool fd_key(WmWindow *win, FileDlg *d, int key, int ch, int mods)
{
    int rows = fd_rows(win);
    switch (key) {
    case PLAT_KEY_ESC:
        fd_finish(win, d, CFALSE);
        return CTRUE;
    case PLAT_KEY_ENTER:
        fd_activate(win, d);
        return CTRUE;
    case PLAT_KEY_UP:
        if (d->sel > 0) { d->sel--; }
        else if (d->count > 0) { d->sel = 0; }
        break;
    case PLAT_KEY_DOWN:
        if (d->sel < d->count - 1) { d->sel++; }
        break;
    case PLAT_KEY_PGUP:
        d->sel -= rows;
        if (d->sel < 0) { d->sel = 0; }
        break;
    case PLAT_KEY_PGDN:
        d->sel += rows;
        if (d->sel > d->count - 1) { d->sel = d->count - 1; }
        break;
    default:
        /* Anything else belongs to the filename field. */
    {
        char before[CASTALIA_MAX_NAME];
        sys_strlcpy(before, ui_edit_text(&d->edit), sizeof before);
        if (ui_edit_key(&d->edit, key, ch, mods)) {
            /*
             * Typing a name DESELECTS the list.
             *
             * Enter accepts the highlighted row, replacing whatever is in the
             * field with that row's name -- so without this you could open
             * Save As, type a new filename, press Enter, and write over the
             * file that happened to be highlighted instead. For a Save As
             * that opens with the current document selected, that file is the
             * one you were editing.
             */
            if (strcmp(before, ui_edit_text(&d->edit)) != 0) { d->sel = -1; }
            wm_invalidate(win, NULL);
            return CTRUE;
        }
    }
        return CFALSE;
    }
    if (d->sel >= 0) {
        if (d->sel < d->top) { d->top = d->sel; }
        if (d->sel >= d->top + rows) { d->top = d->sel - rows + 1; }
        if (d->sel < d->count && !d->ent[d->sel].is_dir) {
            ui_edit_init(&d->edit, d->ent[d->sel].name);
        }
    }
    wm_invalidate(win, NULL);
    return CTRUE;
}

static cbool fd_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    FileDlg *d = (FileDlg *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:       fd_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: return fd_click(win, d, (int)a, (int)b);
    case WM_MSG_LBUTTONUP:   return fd_up(win, d, (int)a, (int)b);
    case WM_MSG_MOUSEMOVE:
        if (ui_hot_move(&d->hot, fd_btn_at(win, (int)a, (int)b))) {
            CRect br[2];
            br[0] = fd_btn_rect(win, 0);
            br[1] = fd_btn_rect(win, 1);
            ui_hot_repaint(win, &d->hot, br, 2);
        }
        return CTRUE;
    case WM_MSG_MOUSELEAVE:
        if (ui_hot_move(&d->hot, -1)) {
            CRect br[2];
            br[0] = fd_btn_rect(win, 0);
            br[1] = fd_btn_rect(win, 1);
            ui_hot_repaint(win, &d->hot, br, 2);
        }
        return CTRUE;
    case WM_MSG_MOUSEWHEEL:
        if (d != NULL &&
            ui_scroll_wheel(&d->top, (int)a, d->count, fd_rows(win))) {
            CPoint o = wm_client_origin(win);
            CRect list = fd_list_rect(win);
            CRect bar  = fd_bar_rect(win);
            CRect r = crect_union(&list, &bar);
            r = crect_offset(&r, o.x, o.y);
            wm_invalidate(win, &r);
        }
        return CTRUE;
    case WM_MSG_KEYDOWN:
        return fd_key(win, d, (int)a, (int)b,
                      (param != NULL) ? *(const int *)param : 0);
    case WM_MSG_CLOSE:
        fd_finish(win, d, CFALSE);
        return CTRUE;
    case WM_MSG_DESTROY:
        if (d != NULL) { sys_free(d, (cu32)sizeof(FileDlg)); }
        return CTRUE;
    default:
        return CFALSE;
    }
}

void ui_file_dialog(const char *title, const char *dir, const char *ext,
                    cbool saving, const char *initial_name,
                    UiFileCb cb, void *user)
{
    FileDlg *d;
    WmWindow *w;
    CRect frame;
    int fw = 320, fh = 240;

    d = (FileDlg *)sys_calloc(1, (cu32)sizeof(FileDlg));
    if (d == NULL) {
        if (cb != NULL) { cb(CFALSE, NULL, user); }
        return;
    }
    if (dir == NULL || dir[0] == '\0') {
        sys_strlcpy(d->dir, sys_home(), sizeof d->dir);
    } else {
        sys_strlcpy(d->dir, dir, sizeof d->dir);
    }
    sys_strlcpy(d->ext, (ext != NULL) ? ext : "", sizeof d->ext);
    d->saving = saving;
    d->cb = cb;
    d->user = user;
    ui_hot_init(&d->hot);
    ui_edit_init(&d->edit, (initial_name != NULL) ? initial_name : "");
    fd_scan(d);

    /* The work area, not the screen: clamping to the screen would let a tall
     * dialog put its Open and Cancel under the taskbar. */
    frame = wm_place_centered(fw, fh);

    w = wm_create((title != NULL) ? title : (saving ? "Save As" : "Open"),
                  &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_BORDER |
                  WM_STYLE_MODAL, fd_proc, d);
    if (w == NULL) {
        sys_free(d, (cu32)sizeof(FileDlg));
        if (cb != NULL) { cb(CFALSE, NULL, user); }
        return;
    }
    wm_show(w, CTRUE);
    wm_focus(w);
    wm_invalidate(w, NULL);
}
