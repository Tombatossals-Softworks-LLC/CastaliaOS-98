/*
 * app_props.c - File / folder Properties dialog.
 *
 * A small XP-style properties window opened from the File Manager's right-click
 * menu. Shows the item's name, type, location and size; for a folder it walks
 * the tree (bounded) to report the contained file/folder counts and total
 * bytes. Read-only and self-contained: owns a heap payload freed on destroy.
 */
#include "apps.h"
#include "assoc.h"
#include "wav.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#include <string.h>

#define PR_SCAN_CAP 200000   /* stop counting past this many entries */

typedef struct {
    char  name[128];
    char  location[CASTALIA_MAX_PATH];
    cbool is_dir;
    cbool is_wav;
    long  size;
    long  total_bytes;
    int   n_files, n_dirs, scanned;
    WavInfo wav;
    UiHot hot;    /* the OK button, under the pointer or held */
} Props;

/* ---- helpers --------------------------------------------------------- */
static void pr_parent(const char *path, char *dst, cu32 dstsz)
{
    int i, last = -1;
    sys_strlcpy(dst, path, dstsz);
    i = (int)sys_strnlen(dst, dstsz);
    if (i > 1 && (dst[i - 1] == '/' || dst[i - 1] == '\\')) { dst[i - 1] = '\0'; }
    for (i = 0; dst[i] != '\0'; i++) {
        if (dst[i] == '/' || dst[i] == '\\') { last = i; }
    }
    if (last <= 0) { sys_strlcpy(dst, (last == 0) ? "/" : ".", dstsz); }
    else { dst[last] = '\0'; }
}

/* Group a non-negative long with thousands separators into 'dst'. */
static void pr_commafy(long v, char *dst, cu32 dstsz)
{
    char tmp[24];
    int n, i, j = 0, c = 0;
    if (v < 0) { v = 0; }
    n = sys_snprintf(tmp, sizeof(tmp), "%ld", v);
    for (i = 0; i < n; i++) {
        if (i > 0 && ((n - i) % 3) == 0 && (cu32)(j + 1) < dstsz) { dst[j++] = ','; }
        if ((cu32)(j + 1) < dstsz) { dst[j++] = tmp[i]; }
        c++;
    }
    dst[j] = '\0';
    (void)c;
}

/* "1,234 bytes (1.21 KB)" style size string. */
static void pr_size_str(long bytes, char *dst, cu32 dstsz)
{
    char grp[32];
    pr_commafy(bytes, grp, sizeof(grp));
    if (bytes < 1024L) {
        sys_snprintf(dst, dstsz, "%s bytes", grp);
    } else if (bytes < 1024L * 1024L) {
        sys_snprintf(dst, dstsz, "%s bytes  (%ld.%02ld KB)", grp,
                     bytes / 1024L, ((bytes % 1024L) * 100L) / 1024L);
    } else {
        long mb = bytes / (1024L * 1024L);
        long frac = ((bytes % (1024L * 1024L)) * 100L) / (1024L * 1024L);
        sys_snprintf(dst, dstsz, "%s bytes  (%ld.%02ld MB)", grp, mb, frac);
    }
}

/* Recursively total the files/folders/bytes under 'dir' (bounded). */
static void pr_scan(Props *pr, const char *dir, int depth)
{
    PlatDir *d;
    PlatDirEntry e;
    if (depth > 24 || pr->scanned >= PR_SCAN_CAP) { return; }
    d = plat_opendir(dir);
    if (d == NULL) { return; }
    while (plat_readdir(d, &e) && pr->scanned < PR_SCAN_CAP) {
        char child[CASTALIA_MAX_PATH];
        cu32 n;
        if (strcmp(e.name, ".") == 0 || strcmp(e.name, "..") == 0) { continue; }
        pr->scanned++;
        sys_strlcpy(child, dir, sizeof(child));
        n = sys_strnlen(child, sizeof(child));
        if (n == 0 || (child[n - 1] != '/' && child[n - 1] != '\\')) {
            sys_strlcat(child, "/", sizeof(child));
        }
        sys_strlcat(child, e.name, sizeof(child));
        if (e.is_dir) { pr->n_dirs++; pr_scan(pr, child, depth + 1); }
        else { pr->n_files++; if (e.size > 0) { pr->total_bytes += e.size; } }
    }
    plat_closedir(d);
}

/* ---- big icon -------------------------------------------------------- */
static void pr_icon(GfxSurface *s, int x, int y, cbool is_dir, cbool is_wav)
{
    if (is_dir) {
        CColor top = GFX_RGB(0xFF, 0xE7, 0x9B), bot = GFX_RGB(0xEE, 0xBB, 0x3E);
        CColor edge = GFX_RGB(0xA8, 0x7C, 0x18);
        CRect tab = crect_make(x + 3, y + 4, 16, 6);
        CRect body = crect_make(x, y + 8, 36, 24);
        gfx_fill_rect(s, &tab, bot); gfx_frame_rect(s, &tab, edge);
        gfx_vgradient(s, &body, top, bot); gfx_frame_rect(s, &body, edge);
        gfx_hline(s, x + 1, y + 10, 34, GFX_RGB(0xFF, 0xF6, 0xD0));
    } else {
        CColor paper = GFX_RGB(0xFF, 0xFF, 0xFF), edge = GFX_RGB(0x78, 0x82, 0x92);
        CRect body = crect_make(x + 4, y, 28, 34);
        int i;
        gfx_fill_rect(s, &body, paper); gfx_frame_rect(s, &body, edge);
        {
            CRect dog = crect_make(x + 25, y, 7, 7);
            gfx_fill_rect(s, &dog, GFX_RGB(0xCC, 0xD6, 0xE6));
            gfx_line(s, x + 25, y, x + 31, y + 6, edge);
        }
        if (is_wav) {
            /* a little green note to mark an audio file */
            gfx_vline(s, x + 20, y + 10, 12, GFX_RGB(0x18, 0xA0, 0x30));
            gfx_fill_circle(s, x + 18, y + 22, 3, GFX_RGB(0x18, 0xA0, 0x30));
        } else {
            for (i = 0; i < 5; i++) {
                gfx_hline(s, x + 8, y + 8 + i * 4, (i == 4) ? 9 : 18,
                          GFX_RGB(0xAC, 0xB7, 0xC9));
            }
        }
    }
}

/* ---- paint ----------------------------------------------------------- */
static void pr_line(GfxSurface *s, int x, int *y, const char *label,
                    const char *value, const UiPalette *p)
{
    gfx_draw_text(s, GFX_FONT_SYSTEM, x, *y, label, p->text_disabled);
    gfx_draw_text(s, GFX_FONT_SYSTEM, x + 70, *y, value, p->text);
    *y += 16;
}

static void pr_paint(WmWindow *win, GfxSurface *s)
{
    Props *pr = (Props *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    int x = o.x + 14, y;
    CRect panel, sep, btn;
    char buf[96];
    if (pr == NULL) { return; }

    panel = crect_make(o.x, o.y, cw, ch);
    gfx_fill_rect(s, &panel, p->face);

    /* Header: big icon + name. */
    pr_icon(s, x, o.y + 12, pr->is_dir, pr->is_wav);
    gfx_draw_text(s, GFX_FONT_BOLD, x + 48, o.y + 20, pr->name, p->text);
    sep = crect_make(o.x + 10, o.y + 54, cw - 20, 2);
    gfx_bevel(s, &sep, GFX_BEVEL_ETCHED, p->light, p->dark, GFX_NO_FILL);

    y = o.y + 64;
    /* The same table the File Manager's Type column and its double-click use,
     * so all three agree on what a file is. */
    pr_line(s, x, &y, "Type:", pr->is_dir ? "File Folder"
                            : pr->is_wav ? "Wave Sound (WAV)"
                            : assoc_type_name(pr->name), p);
    pr_line(s, x, &y, "Location:", pr->location, p);
    if (pr->is_dir) {
        pr_size_str(pr->total_bytes, buf, sizeof(buf));
        pr_line(s, x, &y, "Size:", buf, p);
        sys_snprintf(buf, sizeof(buf), "%d file(s), %d folder(s)%s",
                     pr->n_files, pr->n_dirs,
                     (pr->scanned >= PR_SCAN_CAP) ? " (capped)" : "");
        pr_line(s, x, &y, "Contains:", buf, p);
    } else {
        pr_size_str(pr->size, buf, sizeof(buf));
        pr_line(s, x, &y, "Size:", buf, p);
        if (pr->is_wav && pr->wav.valid) {
            sys_snprintf(buf, sizeof(buf), "%d Hz, %d-bit, %s",
                         pr->wav.rate, pr->wav.bits,
                         pr->wav.channels == 2 ? "stereo" : "mono");
            pr_line(s, x, &y, "Format:", buf, p);
            sys_snprintf(buf, sizeof(buf), "%lu.%01lu s",
                         (unsigned long)(pr->wav.ms / 1000u),
                         (unsigned long)((pr->wav.ms % 1000u) / 100u));
            pr_line(s, x, &y, "Length:", buf, p);
        }
    }

    /* Close button, bottom-right. */
    btn = crect_make(o.x + cw - 76, o.y + ch - 30, 64, 22);
    ui_draw_button(s, &btn, "OK", ui_hot_state(&pr->hot, 0, UI_BTN_NORMAL));
}

/* The OK button in CLIENT coordinates -- which is what a mouse message
 * carries. The paint above builds the same rectangle offset to the screen. */
static CRect pr_ok_rect(WmWindow *win)
{
    CRect c = wm_client_rect(win);
    return crect_make(crect_w(&c) - 76, crect_h(&c) - 30, 64, 22);
}

static cbool pr_click(WmWindow *win, int px, int py)
{
    CRect btn = pr_ok_rect(win);
    Props *pr = (Props *)wm_user(win);
    if (crect_contains(&btn, px, py)) {
        if (pr != NULL) { (void)ui_hot_press(&pr->hot, 0); }
        wm_destroy(win);
        return CTRUE;
    }
    return CFALSE;
}

static cbool props_proc(WmWindow *win, WmMessage msg, long a, long b, void *param)
{
    Props *pr = (Props *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:       pr_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: return pr_click(win, (int)a, (int)b);
    case WM_MSG_MOUSEMOVE: {
        CRect btn = pr_ok_rect(win);
        if (pr == NULL) { return CFALSE; }
        if (ui_hot_move(&pr->hot,
                        crect_contains(&btn, (int)a, (int)b) ? 0 : -1)) {
            ui_hot_repaint(win, &pr->hot, &btn, 1);
        }
        return CTRUE;
    }
    case WM_MSG_MOUSELEAVE:
        if (pr != NULL && ui_hot_move(&pr->hot, -1)) {
            CRect ok = pr_ok_rect(win);
            ui_hot_repaint(win, &pr->hot, &ok, 1);
        }
        return CTRUE;
    case WM_MSG_KEYDOWN:
        if ((int)a == PLAT_KEY_ESC || (int)a == PLAT_KEY_ENTER) {
            wm_destroy(win); return CTRUE;
        }
        return CFALSE;
    case WM_MSG_DESTROY:
        if (pr != NULL) { sys_free(pr, (cu32)sizeof(Props)); }
        return CTRUE;
    default: return CFALSE;
    }
}

void app_props_open(const char *path, cbool is_dir, long size)
{
    Props *pr;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    char title[160];
    int cw = 320, ch = 240, fx, fy;

    if (path == NULL || path[0] == '\0') { return; }
    pr = (Props *)sys_calloc(1, (cu32)sizeof(Props));
    if (pr == NULL) { SYS_LOGE("app", "props: OOM"); return; }
    sys_strlcpy(pr->name, ui_path_base(path), sizeof(pr->name));
    pr_parent(path, pr->location, sizeof(pr->location));
    pr->is_dir = is_dir;
    pr->size = size;
    if (!is_dir) {
        pr->is_wav = wav_is_wav_name(path);
        if (pr->is_wav) { wav_probe(path, &pr->wav); }
        if (size < 0) { pr->size = plat_file_size(path); }
    } else {
        pr_scan(pr, path, 0);
        if (pr->wav.valid) { pr->wav.valid = CFALSE; }
        ch = 226;
    }
    if (!is_dir && pr->is_wav) { ch = 260; }

    sys_snprintf(title, sizeof(title), "%s Properties", pr->name);

    plat_video_info(&vi);
    fx = (vi.width - cw) / 2; fy = (vi.height - ch) / 2;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw, ch);

    w = wm_create(title, &frame, WM_STYLE_TITLE | WM_STYLE_CLOSE | WM_STYLE_MINIMIZE |
                  WM_STYLE_BORDER,
                  props_proc, pr);
    if (w == NULL) { sys_free(pr, (cu32)sizeof(Props)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
    SYS_LOGI("app", "opened Properties for '%s'", pr->name);
}
