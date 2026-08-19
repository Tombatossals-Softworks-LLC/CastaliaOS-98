/*
 * app_view.c - CastaliaOS Viewer: images (BMP) and a hex dump of any file.
 *
 * Opens a file, loads a BMP through the portable codec (gfx_bmp_load) to show
 * the picture (centered 1:1, or nearest-neighbor downscaled to fit), and can
 * switch to a classic offset/hex/ASCII dump of the raw bytes -- the two file
 * inspectors Win98 shipped (an image viewer + a hex tool) and this desktop was
 * missing. The raw buffer is bounded; everything frees on close.
 */
#include "apps.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#include <string.h>

/* Column-map table bound: the widest screen this system sets. A wider one still draws, just
 * without the table. */
#define VW_MAX_COLS 1600

#define VW_TOOLBAR 26
#define VW_STATUS  16
#define VW_ROW_H   10
#define VW_HEX_MAX (64UL * 1024UL)   /* bytes shown in the hex dump */

enum { VW_IMAGE, VW_HEX };
enum { VB_OPEN, VB_TOGGLE, VB_COUNT };
static const char *VB_LABEL[VB_COUNT] = { "Open", "Hex/Image" };

typedef struct {
    int   mode;
    GfxSurface *img;      /* decoded image, or NULL if not a BMP */
    unsigned char *raw;   /* raw bytes for the hex view          */
    cu32  raw_len;        /* bytes held (== bytes allocated)     */
    cbool truncated;
    int   scroll;         /* hex row scroll                      */
    int   visible;        /* hex rows that fit                   */
    char  path[CASTALIA_MAX_PATH];
    char  status[80];
    UiHot hot;            /* which toolbar button the pointer is on  */
} Viewer;

/* ---- layout ---------------------------------------------------------- */
typedef struct { CRect toolbar, well, status, btn[VB_COUNT]; } VwLayout;

static void vw_layout(WmWindow *win, VwLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    L->toolbar = crect_make(0, 0, cw, VW_TOOLBAR);
    L->btn[VB_OPEN]   = crect_make(4, 3, 44, VW_TOOLBAR - 6);
    L->btn[VB_TOGGLE] = crect_make(52, 3, 74, VW_TOOLBAR - 6);
    L->well = crect_make(0, VW_TOOLBAR, cw, ch - VW_TOOLBAR - VW_STATUS);
    L->status = crect_make(0, ch - VW_STATUS, cw, VW_STATUS);
}

/* ---- loading --------------------------------------------------------- */
static void vw_free_content(Viewer *v)
{
    if (v->img != NULL) { gfx_surface_free(v->img); v->img = NULL; }
    if (v->raw != NULL) { sys_free(v->raw, v->raw_len); v->raw = NULL; }
    v->raw_len = 0;
    v->truncated = CFALSE;
}

static void vw_load(Viewer *v, const char *path)
{
    long sz;
    PlatFile *f;

    vw_free_content(v);
    v->scroll = 0;
    sys_strlcpy(v->path, path, sizeof(v->path));

    sz = plat_file_size(path);
    if (sz <= 0) {
        sys_snprintf(v->status, sizeof(v->status), "Cannot open %s", path);
        v->mode = VW_HEX;
        return;
    }
    /* Raw bytes for the hex view (bounded). */
    {
        cu32 want = ((cu32)sz > VW_HEX_MAX) ? (cu32)VW_HEX_MAX : (cu32)sz;
        v->raw = (unsigned char *)sys_alloc(want);
        if (v->raw != NULL) {
            f = plat_fopen(path, "rb");
            if (f != NULL) {
                v->raw_len = plat_fread(f, v->raw, want);
                plat_fclose(f);
                v->truncated = ((cu32)sz > v->raw_len) ? CTRUE : CFALSE;
            }
        }
    }
    /* If it looks like a BMP, decode the image too. */
    if (v->raw != NULL && v->raw_len >= 2 && v->raw[0] == 'B' && v->raw[1] == 'M') {
        v->img = gfx_bmp_load(path);
    }
    v->mode = (v->img != NULL) ? VW_IMAGE : VW_HEX;
    if (v->img != NULL) {
        sys_snprintf(v->status, sizeof(v->status), "%s  %dx%d image", path,
                     v->img->w, v->img->h);
    } else {
        sys_snprintf(v->status, sizeof(v->status), "%s  %lu bytes%s", path,
                     (unsigned long)v->raw_len, v->truncated ? " (truncated)" : "");
    }
}

/* ---- paint ----------------------------------------------------------- */
/* Draw the image into 'well' (surface coords): 1:1 centered, or nearest-
 * neighbor downscaled when larger than the well. */
static void vw_paint_image(GfxSurface *s, const CRect *well, GfxSurface *img)
{
    int ww = crect_w(well) - 2, wh = crect_h(well) - 2;
    if (ww <= 0 || wh <= 0) { return; }
    if (img->w <= ww && img->h <= wh) {
        CRect src = crect_make(0, 0, img->w, img->h);
        gfx_blit(s, well->x0 + 1 + (ww - img->w) / 2,
                 well->y0 + 1 + (wh - img->h) / 2, img, &src, GFX_BLIT_COPY);
    } else {
        /* Fit while preserving aspect; nearest-neighbor sample. */
        int dw, dh, sx0, sy0, dy, dx;
        if (img->w * wh >= img->h * ww) { dw = ww; dh = (img->h * ww) / img->w; }
        else { dh = wh; dw = (img->w * wh) / img->h; }
        if (dw < 1) { dw = 1; } if (dh < 1) { dh = 1; }
        sx0 = well->x0 + 1 + (ww - dw) / 2;
        sy0 = well->y0 + 1 + (wh - dh) / 2;
        /* One divide per output column instead of one per pixel; a viewed
         * image repaints often and can be the size of the screen. */
        {
            static int cols[VW_MAX_COLS];
            int ncols = (dw > VW_MAX_COLS) ? VW_MAX_COLS : dw;
            gfx_scale_map(cols, ncols, img->w);
            for (dy = 0; dy < dh; dy++) {
                int syi = (dy * img->h) / dh;
                const CColor *srow = img->pixels + (long)syi * img->pitch;
                for (dx = 0; dx < ncols; dx++) {
                    gfx_put_pixel(s, sx0 + dx, sy0 + dy, srow[cols[dx]]);
                }
                for (dx = ncols; dx < dw; dx++) {
                    gfx_put_pixel(s, sx0 + dx, sy0 + dy,
                                  srow[(dx * img->w) / dw]);
                }
            }
        }
    }
}

static char vw_hexd(int v) { return "0123456789ABCDEF"[v & 0xF]; }

static void vw_paint_hex(GfxSurface *s, const CRect *well, Viewer *v)
{
    const UiPalette *p = ui_palette();
    int i, rows = (int)((v->raw_len + 15) / 16);
    int tx = well->x0 + 5, ty = well->y0 + 2;
    for (i = 0; i < v->visible; i++) {
        int r = v->scroll + i;
        cu32 off = (cu32)r * 16u;
        char line[96];
        int k = 0, j;
        if (r < 0 || r >= rows) { break; }
        /* 6-digit offset + two spaces. */
        line[k++] = vw_hexd((int)(off >> 20)); line[k++] = vw_hexd((int)(off >> 16));
        line[k++] = vw_hexd((int)(off >> 12)); line[k++] = vw_hexd((int)(off >> 8));
        line[k++] = vw_hexd((int)(off >> 4));  line[k++] = vw_hexd((int)off);
        line[k++] = ' '; line[k++] = ' ';
        for (j = 0; j < 16; j++) {
            if (off + (cu32)j < v->raw_len) {
                unsigned char c = v->raw[off + j];
                line[k++] = vw_hexd(c >> 4); line[k++] = vw_hexd(c); line[k++] = ' ';
            } else { line[k++] = ' '; line[k++] = ' '; line[k++] = ' '; }
        }
        line[k++] = ' ';
        for (j = 0; j < 16 && off + (cu32)j < v->raw_len; j++) {
            unsigned char c = v->raw[off + j];
            line[k++] = (c >= 32 && c < 127) ? (char)c : '.';
        }
        line[k] = '\0';
        gfx_draw_text(s, GFX_FONT_SYSTEM, tx, ty + i * VW_ROW_H, line, p->text);
    }
}

static void vw_paint(WmWindow *win, GfxSurface *s)
{
    Viewer *v = (Viewer *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    VwLayout L;
    CRect r;
    int i;
    if (v == NULL) { return; }
    vw_layout(win, &L);
    v->visible = (crect_h(&L.well) - 4) / VW_ROW_H;

    r = crect_offset(&L.toolbar, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_RAISED_THIN, p->light, p->dark, p->face);
    for (i = 0; i < VB_COUNT; i++) {
        CRect b = crect_offset(&L.btn[i], o.x, o.y);
        cbool dis = (i == VB_TOGGLE && v->img == NULL);
        ui_draw_button(s, &b, VB_LABEL[i],
                       ui_hot_state(&v->hot, i,
                                    dis ? UI_BTN_DISABLED : UI_BTN_NORMAL));
    }

    r = crect_offset(&L.well, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark,
              (v->mode == VW_IMAGE) ? GFX_RGB(0x40,0x40,0x40)
                                    : GFX_RGB(0xFF,0xFF,0xFF));
    if (v->mode == VW_IMAGE && v->img != NULL) {
        vw_paint_image(s, &r, v->img);
    } else if (v->raw != NULL) {
        vw_paint_hex(s, &r, v);
    } else {
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 6, r.y0 + 4,
                      "Open a file to view it.", p->text_disabled);
    }

    r = crect_offset(&L.status, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, p->face);
    gfx_draw_text_rect(s, GFX_FONT_SYSTEM, &r, v->status, p->text,
                       GFX_ALIGN_LEFT | GFX_ALIGN_VCENTER);
}

/* ---- input ----------------------------------------------------------- */
static void vw_on_open(cbool ok, const char *text, void *user)
{
    WmWindow *win = (WmWindow *)user;
    Viewer *v = (Viewer *)wm_user(win);
    if (ok && v != NULL && text[0] != '\0') {
        vw_load(v, text);
        SYS_LOGI("app", "viewer opened '%s' (%s)", text,
                 (v->img != NULL) ? "image" : "hex");
        wm_invalidate(win, NULL);
    }
}

static void vw_clamp(Viewer *v)
{
    int rows = (int)((v->raw_len + 15) / 16);
    int max = rows - v->visible;
    if (max < 0) { max = 0; }
    if (v->scroll > max) { v->scroll = max; }
    if (v->scroll < 0) { v->scroll = 0; }
}

/* The toolbar button under (x,y), or -1. */
static int vw_btn_at(WmWindow *win, int x, int y)
{
    VwLayout L;
    int i;
    vw_layout(win, &L);
    for (i = 0; i < VB_COUNT; i++) {
        if (crect_contains(&L.btn[i], x, y)) { return i; }
    }
    return -1;
}

static cbool vw_click(WmWindow *win, Viewer *v, int x, int y)
{
    VwLayout L;
    vw_layout(win, &L);
    (void)ui_hot_press(&v->hot, vw_btn_at(win, x, y));
    if (crect_contains(&L.btn[VB_OPEN], x, y)) {
        ui_file_dialog("Open File", NULL, NULL, CFALSE, v->path,
                       vw_on_open, win);
        return CTRUE;
    }
    if (crect_contains(&L.btn[VB_TOGGLE], x, y) && v->img != NULL) {
        v->mode = (v->mode == VW_IMAGE) ? VW_HEX : VW_IMAGE;
        wm_invalidate(win, NULL);
        return CTRUE;
    }
    if (crect_contains(&L.well, x, y) && v->mode == VW_HEX) {
        int mid = L.well.y0 + crect_h(&L.well) / 2;
        v->scroll += (y < mid) ? -v->visible : v->visible;
        vw_clamp(v);
        wm_invalidate(win, NULL);
        return CTRUE;
    }
    return CFALSE;
}

static cbool vw_key(WmWindow *win, Viewer *v, int key)
{
    int before = v->scroll;
    if (v->mode != VW_HEX) { return CFALSE; }
    switch (key) {
    case PLAT_KEY_UP:   v->scroll -= 1; break;
    case PLAT_KEY_DOWN: v->scroll += 1; break;
    case PLAT_KEY_PGUP: v->scroll -= v->visible; break;
    case PLAT_KEY_PGDN: v->scroll += v->visible; break;
    case PLAT_KEY_HOME: v->scroll = 0; break;
    case PLAT_KEY_END:  v->scroll = (int)((v->raw_len + 15) / 16); break;
    default: return CFALSE;
    }
    vw_clamp(v);
    if (v->scroll != before) { wm_invalidate(win, NULL); }
    return CTRUE;
}

static cbool view_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    Viewer *v = (Viewer *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:       vw_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: return vw_click(win, v, (int)a, (int)b);
    case WM_MSG_MOUSEMOVE:
        if (v != NULL && ui_hot_move(&v->hot, vw_btn_at(win, (int)a, (int)b))) {
            VwLayout L;
            vw_layout(win, &L);
            ui_hot_repaint(win, &v->hot, L.btn, VB_COUNT);
        }
        return CTRUE;
    case WM_MSG_LBUTTONUP:
    case WM_MSG_MOUSELEAVE: {
        cbool redraw;
        if (v == NULL) { return CFALSE; }
        redraw = ui_hot_release(&v->hot);
        if (msg == WM_MSG_MOUSELEAVE && ui_hot_move(&v->hot, -1)) {
            redraw = CTRUE;
        }
        if (redraw) {
            VwLayout L;
            vw_layout(win, &L);
            ui_hot_repaint(win, &v->hot, L.btn, VB_COUNT);
        }
        return CTRUE;
    }
    case WM_MSG_MOUSEWHEEL: {
        int rows;
        if (v == NULL || v->mode != VW_HEX) { return CTRUE; }
        rows = (int)((v->raw_len + 15) / 16);
        if (ui_scroll_wheel(&v->scroll, (int)a, rows, v->visible)) {
            VwLayout L;
            CPoint o;
            CRect r;
            vw_layout(win, &L);
            o = wm_client_origin(win);
            r = crect_offset(&L.well, o.x, o.y);
            wm_invalidate(win, &r);
        }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:     return vw_key(win, v, (int)a);
    case WM_MSG_DESTROY:
        if (v != NULL) { vw_free_content(v); sys_free(v, (cu32)sizeof(Viewer)); }
        return CTRUE;
    default: return CFALSE;
    }
}

static Viewer *vw_spawn(void)
{
    Viewer *v;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int fw = 460, fh = 320, fx, fy;

    v = (Viewer *)sys_calloc(1, (cu32)sizeof(Viewer));
    if (v == NULL) { SYS_LOGE("app", "viewer: OOM"); return NULL; }
    v->mode = VW_HEX;
    sys_strlcpy(v->status, "Viewer: Open a BMP image or any file", sizeof(v->status));

    plat_video_info(&vi);
    fx = (vi.width - fw) / 2 + 10; fy = (vi.height - fh) / 2;
    if (fx < 0) { fx = 0; } if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, fw, fh);
    w = wm_create("Viewer", &frame, WM_STYLE_APP, view_proc, v);
    if (w == NULL) { sys_free(v, (cu32)sizeof(Viewer)); return NULL; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    return v;
}

void app_view_open(void)
{
    Viewer *v = vw_spawn();
    if (v == NULL) { return; }
    SYS_LOGI("app", "opened Viewer");
}

void app_view_open_file(const char *path)
{
    Viewer *v;
    if (path == NULL || path[0] == '\0') { app_view_open(); return; }
    v = vw_spawn();
    if (v == NULL) { return; }
    /* vw_spawn already invalidated the fresh window; it first paints after this
     * load, so the content shows on the first frame. */
    vw_load(v, path);
}
