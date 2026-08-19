/*
 * capp_host_wm.c - Window-manager backend for plugin windows.
 *
 * The capp loader keeps its windowing abstract (CappWindowOps) so it never
 * depends on the window manager. This module supplies the concrete ops on the
 * host and DOS backends: each plugin window owns a RETAINED client surface the
 * plugin draws into whenever it likes; a small WmProc blits that surface into
 * the window's client area every time the compositor repaints it. That matches
 * the ABI's model (the plugin owns its pixels; the shell composites them) and
 * keeps plugin drawing decoupled from the shell's immediate-mode paint pass.
 */
#include "castalia/capp_loader.h"
#include "castalia/wm.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#include <string.h>

/* A retained plugin window: the WM window + its client bitmap. */
typedef struct {
    cbool       used;
    WmWindow   *win;
    GfxSurface *surf;
} CappWinRec;

#define CAPP_HOSTWM_MAX 8
static CappWinRec g_recs[CAPP_HOSTWM_MAX];

static CappWinRec *rec_alloc(void)
{
    int i;
    for (i = 0; i < CAPP_HOSTWM_MAX; i++) {
        if (!g_recs[i].used) { return &g_recs[i]; }
    }
    return NULL;
}

static void rec_release(CappWinRec *rec)
{
    if (rec == NULL) { return; }
    if (rec->surf != NULL) { gfx_surface_free(rec->surf); rec->surf = NULL; }
    rec->win = NULL;
    rec->used = CFALSE;
}

/* The WmProc for every plugin window: blit the retained surface on paint. */
static cbool plugin_win_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    CappWinRec *rec = (CappWinRec *)wm_user(win);
    CASTALIA_UNUSED(a); CASTALIA_UNUSED(b);
    switch (msg) {
    case WM_MSG_PAINT: {
        GfxSurface *s = (GfxSurface *)param;
        CPoint o;
        CRect  src;
        if (rec == NULL || rec->surf == NULL) { return CTRUE; }
        o = wm_client_origin(win);
        src = crect_make(0, 0, rec->surf->w, rec->surf->h);
        gfx_blit(s, o.x, o.y, rec->surf, &src, GFX_BLIT_COPY);
        return CTRUE;
    }
    case WM_MSG_DESTROY:
        rec_release(rec);
        return CTRUE;
    default:
        return CFALSE;
    }
}

/* ---- CappWindowOps implementations ----------------------------------- */
static void *hostwm_open(const char *title, int w, int h)
{
    CappWinRec  *rec;
    WmWindow    *win;
    PlatVideoInfo vi;
    CRect        frame;
    int          fw, fh, fx, fy;

    if (w < 16) { w = 16; }
    if (h < 16) { h = 16; }
    rec = rec_alloc();
    if (rec == NULL) { SYS_LOGW("capp", "plugin window pool exhausted"); return NULL; }

    rec->surf = gfx_surface_new(w, h);
    if (rec->surf == NULL) { SYS_LOGE("capp", "plugin window surface OOM"); return NULL; }
    gfx_clear(rec->surf, GFX_RGB(0x20, 0x20, 0x20));

    /* Frame is padded so the client area comfortably holds the w*h surface;
     * the blit clips to the real client rect regardless of exact NC metrics. */
    fw = w + 10; fh = h + 32;
    plat_video_info(&vi);
    fx = (vi.width  - fw) / 2 + 24;
    fy = (vi.height - fh) / 2 - 12;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, fw, fh);

    win = wm_create((title != NULL) ? title : "Add-on",
                    &frame, WM_STYLE_APP, plugin_win_proc, rec);
    if (win == NULL) { rec_release(rec); return NULL; }
    rec->win = win;
    rec->used = CTRUE;
    wm_show(win, CTRUE);
    wm_invalidate(win, NULL);
    return win;
}

static void hostwm_close(void *win)
{
    if (win != NULL) { wm_destroy((WmWindow *)win); }  /* frees rec via DESTROY */
}

static GfxSurface *hostwm_surface(void *win)
{
    CappWinRec *rec;
    if (win == NULL) { return NULL; }
    rec = (CappWinRec *)wm_user((WmWindow *)win);
    return (rec != NULL) ? rec->surf : NULL;
}

static void hostwm_invalidate(void *win)
{
    if (win != NULL) { wm_invalidate((WmWindow *)win, NULL); }
}

/* ---- install / shutdown ---------------------------------------------- */
void capp_host_wm_install(void)
{
    static const CappWindowOps ops = {
        hostwm_open, hostwm_close, hostwm_surface, hostwm_invalidate
    };
    capp_loader_set_window_ops(&ops);
}

void capp_host_wm_shutdown(void)
{
    int i;
    for (i = 0; i < CAPP_HOSTWM_MAX; i++) {
        if (g_recs[i].used) {
            if (g_recs[i].win != NULL) { wm_destroy(g_recs[i].win); }
            rec_release(&g_recs[i]);
        }
    }
    capp_loader_set_window_ops(NULL);
}
