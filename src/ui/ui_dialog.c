/*
 * ui_dialog.c - Modal message and prompt dialogs.
 *
 * Dialogs are window-manager windows with WM_STYLE_MODAL, so they capture all
 * input until dismissed, but they do NOT block the cooperative loop: the caller
 * passes a callback that fires with the result when a button is chosen. This
 * keeps the single-threaded frame loop responsive.
 *
 *   ui_msgbox(title, message, buttons, cb, user)   -> OK / OK-Cancel / Yes-No
 *   ui_prompt(title, label, initial, cb, user)     -> a labeled text field
 *
 * Enter picks the default button; Esc picks cancel.
 */
#include "castalia/ui.h"
#include "castalia/wm.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#define DLG_MSG_MAX 256
#define DLG_BTN_W   62
#define DLG_BTN_H   20
#define DLG_MAX_LINES 8

typedef enum { DLG_MSG, DLG_PROMPT } DlgKind;

typedef struct {
    DlgKind     kind;
    char        message[DLG_MSG_MAX];   /* msg text, or prompt label          */
    UiDialogCb  msg_cb;
    UiPromptCb  prompt_cb;
    void       *user;
    UiEdit      edit;                   /* prompt only                        */
    int         nbtn;
    int         btn_result[3];          /* UI_DR_*                            */
    const char *btn_label[3];
    int         def_result;             /* Enter                              */
    int         cancel_result;          /* Esc                               */
    int         def_index;              /* the button Enter starts on         */
    int         focus;                  /* ...and the one it is on now        */
    UiHot       hot;                    /* which button the pointer is on     */
} Dlg;

/* The dialog now up, for the headless driver's accessors at the bottom.
 * Modal, so there is only ever one. */
static WmWindow *g_dlg_win = NULL;

/* Which button carries a given result, or 0 if none does. */
static int dlg_index_of(const Dlg *d, int result)
{
    int i;
    for (i = 0; i < d->nbtn; i++) {
        if (d->btn_result[i] == result) { return i; }
    }
    return 0;
}

/* ---- geometry (client-relative) -------------------------------------- */
static CRect dlg_button_rect(WmWindow *win, Dlg *d, int i)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c);
    int ch = crect_h(&c);
    int total = d->nbtn * DLG_BTN_W + (d->nbtn - 1) * 8;
    int x0 = (cw - total) / 2;
    CRect r;
    r.x0 = x0 + i * (DLG_BTN_W + 8);
    r.y0 = ch - DLG_BTN_H - 8;
    r.x1 = r.x0 + DLG_BTN_W;
    r.y1 = r.y0 + DLG_BTN_H;
    return r;
}

static CRect dlg_edit_rect(WmWindow *win)
{
    CRect c = wm_client_rect(win);
    CRect r;
    r.x0 = 12; r.x1 = crect_w(&c) - 12;
    r.y0 = 34; r.y1 = 34 + 18;
    return r;
}

/* Finish: capture callback data, destroy the window (frees Dlg), then fire. */
static void dlg_finish(WmWindow *win, int result)
{
    Dlg *d = (Dlg *)wm_user(win);
    UiDialogCb mcb;
    UiPromptCb pcb;
    void *u;
    DlgKind kind;
    char text[UI_EDIT_MAX];
    int ok;
    if (d == NULL) { wm_destroy(win); return; }
    mcb = d->msg_cb; pcb = d->prompt_cb; u = d->user; kind = d->kind;
    sys_strlcpy(text, d->edit.text, sizeof(text));
    ok = (result == UI_DR_OK || result == UI_DR_YES) ? 1 : 0;

    wm_destroy(win); /* sends WM_MSG_DESTROY -> frees d */

    if (kind == DLG_MSG) {
        if (mcb != NULL) { mcb((UiDialogResult)result, u); }
    } else {
        if (pcb != NULL) { pcb(ok ? CTRUE : CFALSE, text, u); }
    }
}

/* Split the message on '\n' and draw each line; return line count. */
static int draw_message(GfxSurface *s, int ox, int oy, const char *msg,
                        CColor color)
{
    char line[DLG_MSG_MAX];
    int li = 0, n = 0, y = oy;
    int i = 0;
    for (;;) {
        char ch = msg[i];
        if (ch == '\n' || ch == '\0') {
            line[li] = '\0';
            gfx_draw_text(s, GFX_FONT_SYSTEM, ox, y, line, color);
            y += 11;
            n++;
            li = 0;
            if (ch == '\0' || n >= DLG_MAX_LINES) { break; }
        } else if (li < (int)sizeof(line) - 1) {
            line[li++] = ch;
        }
        i++;
    }
    return n;
}

static cbool dlg_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    Dlg *d = (Dlg *)wm_user(win);
    const UiPalette *p = ui_palette();
    if (d == NULL) { return CFALSE; }

    switch (msg) {
    case WM_MSG_PAINT: {
        GfxSurface *s = (GfxSurface *)param;
        CPoint o = wm_client_origin(win);
        int i;
        draw_message(s, o.x + 12, o.y + 12, d->message, p->text);
        if (d->kind == DLG_PROMPT) {
            CRect er = dlg_edit_rect(win);
            er = crect_offset(&er, o.x, o.y);
            ui_edit_draw(s, &er, &d->edit, CTRUE);
        }
        for (i = 0; i < d->nbtn; i++) {
            CRect br = dlg_button_rect(win, d, i);
            br = crect_offset(&br, o.x, o.y);
            if (i == d->def_index) { ui_draw_button_default(s, &br); }
            ui_draw_button(s, &br, d->btn_label[i],
                           ui_hot_state(&d->hot, i, UI_BTN_NORMAL));
            /* Only when there is a choice to move BETWEEN. A lone OK with a
             * dotted ring round it is decoration. */
            if (d->nbtn > 1 && i == d->focus) {
                ui_draw_button_focus(s, &br);
            }
        }
        return CTRUE;
    }
    case WM_MSG_LBUTTONDOWN: {
        int i;
        if (d->kind == DLG_PROMPT) {
            CRect er = dlg_edit_rect(win);
            if (crect_contains(&er, (int)a, (int)b)) {
                ui_edit_click(&d->edit, &er, (int)a);
                wm_invalidate(win, NULL);
            }
        }
        for (i = 0; i < d->nbtn; i++) {
            CRect br = dlg_button_rect(win, d, i);
            if (crect_contains(&br, (int)a, (int)b)) {
                d->focus = i;
                dlg_finish(win, d->btn_result[i]);
                return CTRUE;
            }
        }
        return CTRUE;
    }
    /*
     * Yes and No light under the pointer. They did not, and a message box is
     * where it matters most: this is the window that asks whether to throw
     * work away, and the two answers looked identical until one of them was
     * already clicked.
     */
    case WM_MSG_MOUSEMOVE: {
        int i, hit = -1;
        for (i = 0; i < d->nbtn; i++) {
            CRect br = dlg_button_rect(win, d, i);
            if (crect_contains(&br, (int)a, (int)b)) { hit = i; }
        }
        if (ui_hot_move(&d->hot, hit)) {
            CRect br[3];
            CPoint o2 = wm_client_origin(win);
            for (i = 0; i < d->nbtn && i < 3; i++) {
                br[i] = dlg_button_rect(win, d, i);
            }
            (void)o2;
            ui_hot_repaint(win, &d->hot, br, d->nbtn);
        }
        return CTRUE;
    }
    case WM_MSG_MOUSELEAVE:
        if (ui_hot_move(&d->hot, -1)) {
            CRect br[3];
            int k;
            for (k = 0; k < d->nbtn && k < 3; k++) {
                br[k] = dlg_button_rect(win, d, k);
            }
            ui_hot_repaint(win, &d->hot, br, d->nbtn);
        }
        return CTRUE;
    case WM_MSG_KEYDOWN: {
        int key = (int)a, ch = (int)b;
        int mods = (param != NULL) ? *(const int *)param : 0;
        /*
         * Enter presses the FOCUSED button, which starts on the default. Esc
         * still cancels outright -- it is the one answer that must work
         * whatever the focus has been moved to.
         */
        if (key == PLAT_KEY_ENTER) {
            dlg_finish(win, d->btn_result[d->focus]);
            return CTRUE;
        }
        if (key == PLAT_KEY_ESC)   { dlg_finish(win, d->cancel_result); return CTRUE; }
        /*
         * Tab walks the buttons, and so do Left/Right on a message box. On a
         * PROMPT the arrows belong to the text field -- moving the caret is
         * what somebody typing a filename means by them -- so there Tab is
         * the only way across.
         */
        if (d->nbtn > 1 &&
            (key == PLAT_KEY_TAB ||
             ((key == PLAT_KEY_LEFT || key == PLAT_KEY_RIGHT) &&
              d->kind != DLG_PROMPT))) {
            int step = (key == PLAT_KEY_LEFT) ? -1 : 1;
            d->focus = (d->focus + step + d->nbtn) % d->nbtn;
            wm_invalidate(win, NULL);
            return CTRUE;
        }
        if (d->kind == DLG_PROMPT) {
            if (ui_edit_key(&d->edit, key, ch, mods)) { wm_invalidate(win, NULL); }
        }
        return CTRUE;
    }
    case WM_MSG_CLOSE:
        dlg_finish(win, d->cancel_result);
        return CTRUE;
    case WM_MSG_DESTROY:
        if (g_dlg_win == win) { g_dlg_win = NULL; }
        sys_free(d, (cu32)sizeof(Dlg));
        return CTRUE;
    default:
        return CFALSE;
    }
}

/* ---- construction ---------------------------------------------------- */
static WmWindow *dlg_create(Dlg *d, const char *title, int cw, int ch)
{
    PlatVideoInfo vi;
    CRect frame;
    int fx, fy, fw, fh;
    WmWindow *w;
    /*
     * Placed a third of the way down rather than centred -- where a dialog
     * goes -- but bounded by the WORK area, not the screen. A message box
     * sizes itself to its text, so a long enough message would otherwise put
     * its own buttons under the taskbar, and a modal dialog whose OK is
     * unreachable is a desktop nobody can use.
     */
    {
        CRect wa = wm_work_area();
        int aw = crect_w(&wa), ah = crect_h(&wa);
        fw = cw + 8;               /* + border */
        fh = ch + 28;              /* + title + border */
        if (fw > aw) { fw = aw; }
        if (fh > ah) { fh = ah; }
        fx = wa.x0 + (aw - fw) / 2;
        fy = wa.y0 + (ah - fh) / 3;
        if (fx < wa.x0) { fx = wa.x0; }
        if (fy < wa.y0) { fy = wa.y0; }
        frame = crect_make(fx, fy, fw, fh);
    }
    CASTALIA_UNUSED(vi);
    w = wm_create(title, &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_BORDER |
                  WM_STYLE_MODAL, dlg_proc, d);
    if (w != NULL) {
        g_dlg_win = w;
        wm_show(w, CTRUE);
        wm_focus(w);
        wm_invalidate(w, NULL);
    }
    return w;
}

/* ---- headless driver accessors (see ui.h) ---------------------------- */
static Dlg *dlg_now(void)
{
    return (g_dlg_win != NULL) ? (Dlg *)wm_user(g_dlg_win) : (Dlg *)0;
}

cbool ui_dialog_btn_rect(int i, CRect *out)
{
    Dlg *d = dlg_now();
    CPoint o;
    CRect r;
    if (d == NULL || out == NULL || i < 0 || i >= d->nbtn) { return CFALSE; }
    o = wm_client_origin(g_dlg_win);
    r = dlg_button_rect(g_dlg_win, d, i);
    *out = crect_offset(&r, o.x, o.y);
    return CTRUE;
}

int ui_dialog_btn_count(void)
{
    Dlg *d = dlg_now();
    return (d != NULL) ? d->nbtn : 0;
}

int ui_dialog_focus(void)
{
    Dlg *d = dlg_now();
    return (d != NULL) ? d->focus : -1;
}

static int text_px(const char *s) { return gfx_text_width(GFX_FONT_SYSTEM, s); }

void ui_msgbox(const char *title, const char *message, int buttons,
               UiDialogCb cb, void *user)
{
    Dlg *d;
    int cw, ch, lines, maxw = 120;
    d = (Dlg *)sys_calloc(1, (cu32)sizeof(Dlg));
    if (d == NULL) { return; }
    d->kind = DLG_MSG;
    d->msg_cb = cb;
    d->user = user;
    ui_hot_init(&d->hot);
    sys_strlcpy(d->message, (message ? message : ""), sizeof(d->message));

    if (buttons == UI_MB_YESNO) {
        d->nbtn = 2;
        d->btn_result[0] = UI_DR_YES; d->btn_label[0] = "Yes";
        d->btn_result[1] = UI_DR_NO;  d->btn_label[1] = "No";
        d->def_result = UI_DR_YES; d->cancel_result = UI_DR_NO;
    } else if (buttons == UI_MB_OKCANCEL) {
        d->nbtn = 2;
        d->btn_result[0] = UI_DR_OK;     d->btn_label[0] = "OK";
        d->btn_result[1] = UI_DR_CANCEL; d->btn_label[1] = "Cancel";
        d->def_result = UI_DR_OK; d->cancel_result = UI_DR_CANCEL;
    } else {
        d->nbtn = 1;
        d->btn_result[0] = UI_DR_OK; d->btn_label[0] = "OK";
        d->def_result = UI_DR_OK; d->cancel_result = UI_DR_OK;
    }
    d->def_index = dlg_index_of(d, d->def_result);
    d->focus = d->def_index;

    /* Size to content. */
    {
        char tmp[DLG_MSG_MAX];
        int li = 0, k = 0;
        lines = 0;
        for (;;) {
            char c = d->message[k];
            if (c == '\n' || c == '\0') {
                tmp[li] = '\0';
                if (text_px(tmp) > maxw) { maxw = text_px(tmp); }
                lines++; li = 0;
                if (c == '\0') { break; }
            } else if (li < (int)sizeof(tmp) - 1) { tmp[li++] = c; }
            k++;
        }
    }
    cw = maxw + 24;
    if (cw < d->nbtn * (DLG_BTN_W + 8) + 16) { cw = d->nbtn * (DLG_BTN_W + 8) + 16; }
    ch = 12 + lines * 11 + 12 + DLG_BTN_H + 8;
    /*
     * A Dlg is freed by its window, on WM_MSG_DESTROY. If the window is never
     * created that message never arrives and this leaks -- and the leak is
     * worst exactly where it is most likely, because the thing that makes
     * wm_create fail is running out of memory or window slots, which is also
     * when something tries to put a message box on the screen to say so.
     *
     * This used to read `i = dlg_create(...) ? 0 : 0; CASTALIA_UNUSED(i);`,
     * which discards the answer in a shape elaborate enough to look
     * deliberate. -Wduplicated-branches found it: both arms of that ternary
     * are 0.
     */
    if (dlg_create(d, (title ? title : "Message"), cw, ch) == NULL) {
        sys_free(d, (cu32)sizeof(Dlg));
    }
}

void ui_prompt(const char *title, const char *label, const char *initial,
               UiPromptCb cb, void *user)
{
    Dlg *d;
    int cw, ch;
    d = (Dlg *)sys_calloc(1, (cu32)sizeof(Dlg));
    if (d == NULL) { return; }
    d->kind = DLG_PROMPT;
    d->prompt_cb = cb;
    d->user = user;
    ui_hot_init(&d->hot);
    sys_strlcpy(d->message, (label ? label : ""), sizeof(d->message));
    ui_edit_init(&d->edit, initial);
    d->nbtn = 2;
    d->btn_result[0] = UI_DR_OK;     d->btn_label[0] = "OK";
    d->btn_result[1] = UI_DR_CANCEL; d->btn_label[1] = "Cancel";
    d->def_result = UI_DR_OK; d->cancel_result = UI_DR_CANCEL;
    d->def_index = dlg_index_of(d, d->def_result);
    d->focus = d->def_index;

    cw = 260;
    if (text_px(d->message) + 24 > cw) { cw = text_px(d->message) + 24; }
    ch = 12 + 11 + 10 + 18 + 12 + DLG_BTN_H + 8; /* label + edit + buttons */
    /* Same ownership rule as ui_msgbox: no window, no WM_MSG_DESTROY, so
     * nothing would ever free this. */
    if (dlg_create(d, (title ? title : "Input"), cw, ch) == NULL) {
        sys_free(d, (cu32)sizeof(Dlg));
    }
}
