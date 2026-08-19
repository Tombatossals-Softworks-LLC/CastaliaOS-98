/*
 * sh_shutdown.c - The "Shut Down CastaliaOS" dialog.
 *
 * A modal window with a radio group (Shut down / Restart the shell / Exit to
 * DOS) and OK / Cancel, in the spirit of the shutdown dialog on the systems
 * this shell is inspired by. Choosing OK sets the session exit reason, which
 * ends the frame loop; main.c then acts on it. Built on the shared radio and
 * button controls.
 */
#include "sh_internal.h"
#include "castalia/gfx.h"
#include "castalia/sys.h"

enum { SD_SHUTDOWN = 0, SD_RESTART, SD_TODOS, SD_COUNT };
static const char *SD_LABEL[SD_COUNT] = {
    "Shut down", "Restart the shell", "Exit to DOS"
};
static const ShExitReason SD_REASON[SD_COUNT] = {
    SH_EXIT_SHUTDOWN, SH_EXIT_RESTART_SHELL, SH_EXIT_TO_DOS
};

typedef struct {
    int   choice;
    UiHot hot;      /* OK / Cancel under the pointer */
} ShutDlg;

#define SD_ROW_H 20
#define SD_BTN_W 64
#define SD_BTN_H 22

typedef struct { CRect prompt, rows[SD_COUNT], ok, cancel; } SdLayout;

static void sd_layout(WmWindow *win, SdLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c), i;
    int by = ch - SD_BTN_H - 8;
    L->prompt = crect_make(12, 10, cw - 24, 14);
    for (i = 0; i < SD_COUNT; i++) {
        L->rows[i] = crect_make(24, 32 + i * SD_ROW_H, cw - 40, SD_ROW_H);
    }
    L->ok     = crect_make(cw - (SD_BTN_W + 8) * 2, by, SD_BTN_W, SD_BTN_H);
    L->cancel = crect_make(cw - (SD_BTN_W + 8), by, SD_BTN_W, SD_BTN_H);
}

static void sd_paint(WmWindow *win, GfxSurface *s)
{
    ShutDlg *d = (ShutDlg *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    SdLayout L;
    CRect r;
    int i;
    if (d == NULL) { return; }
    sd_layout(win, &L);
    r = crect_offset(&L.prompt, o.x, o.y);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0, r.y0,
                  "What do you want the computer to do?", p->text);
    for (i = 0; i < SD_COUNT; i++) {
        r = crect_offset(&L.rows[i], o.x, o.y);
        ui_draw_radio(s, &r, SD_LABEL[i], (d->choice == i), UI_BTN_NORMAL);
    }
    r = crect_offset(&L.ok, o.x, o.y);
    ui_draw_button(s, &r, "OK",     ui_hot_state(&d->hot, 0, UI_BTN_NORMAL));
    r = crect_offset(&L.cancel, o.x, o.y);
    ui_draw_button(s, &r, "Cancel", ui_hot_state(&d->hot, 1, UI_BTN_NORMAL));
}

static void sd_commit(WmWindow *win, ShutDlg *d)
{
    ShExitReason reason = SD_REASON[d->choice];
    wm_destroy(win);              /* close the dialog first */
    sh_end_session(reason);       /* ...which asks about unsaved work */
}

static cbool sd_click(WmWindow *win, ShutDlg *d, int x, int y)
{
    SdLayout L;
    int i;
    sd_layout(win, &L);
    for (i = 0; i < SD_COUNT; i++) {
        if (crect_contains(&L.rows[i], x, y)) {
            d->choice = i; wm_invalidate(win, NULL); return CTRUE;
        }
    }
    if (crect_contains(&L.ok, x, y))     { sd_commit(win, d); return CTRUE; }
    if (crect_contains(&L.cancel, x, y)) { wm_destroy(win);   return CTRUE; }
    return CTRUE; /* modal: swallow */
}

static cbool sd_key(WmWindow *win, ShutDlg *d, int key)
{
    switch (key) {
    case PLAT_KEY_UP:    if (d->choice > 0) { d->choice--; wm_invalidate(win, NULL); } return CTRUE;
    case PLAT_KEY_DOWN:  if (d->choice < SD_COUNT - 1) { d->choice++; wm_invalidate(win, NULL); } return CTRUE;
    case PLAT_KEY_ENTER: sd_commit(win, d); return CTRUE;
    case PLAT_KEY_ESC:   wm_destroy(win); return CTRUE;
    default: return CTRUE;
    }
}

static cbool sd_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    ShutDlg *d = (ShutDlg *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:       sd_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: return sd_click(win, d, (int)a, (int)b);
    case WM_MSG_MOUSEMOVE: {
        SdLayout L;
        int hit = -1;
        if (d == NULL) { return CFALSE; }
        sd_layout(win, &L);
        if (crect_contains(&L.ok, (int)a, (int)b))          { hit = 0; }
        else if (crect_contains(&L.cancel, (int)a, (int)b)) { hit = 1; }
        if (ui_hot_move(&d->hot, hit)) {
            CRect br[2];
            br[0] = L.ok; br[1] = L.cancel;
            ui_hot_repaint(win, &d->hot, br, 2);
        }
        return CTRUE;
    }
    case WM_MSG_MOUSELEAVE:
        if (d != NULL && ui_hot_move(&d->hot, -1)) {
            SdLayout L;
            CRect br[2];
            sd_layout(win, &L);
            br[0] = L.ok; br[1] = L.cancel;
            ui_hot_repaint(win, &d->hot, br, 2);
        }
        return CTRUE;
    case WM_MSG_KEYDOWN:     return sd_key(win, d, (int)a);
    case WM_MSG_DESTROY:     if (d != NULL) { sys_free(d, (cu32)sizeof(ShutDlg)); } return CTRUE;
    default: return CFALSE;
    }
}

void sh_shutdown_dialog(void)
{
    ShutDlg *d;
    WmWindow *w;
    CRect frame;
    int cw = 280, ch = 150;
    int fx = (g_sh.screen_w - cw) / 2, fy = (g_sh.screen_h - ch) / 2;

    d = (ShutDlg *)sys_calloc(1, (cu32)sizeof(ShutDlg));
    if (d == NULL) { return; }
    d->choice = SD_SHUTDOWN;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw, ch);
    w = wm_create("Shut Down CastaliaOS", &frame,
                  WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_BORDER | WM_STYLE_MODAL,
                  sd_proc, d);
    if (w == NULL) { sys_free(d, (cu32)sizeof(ShutDlg)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
}
