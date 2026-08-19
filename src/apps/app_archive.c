/*
 * app_archive.c - Looking inside a .CAR before letting it out.
 *
 * Backing a folder up into an archive already worked. Getting it back did too.
 * What you could not do was LOOK: opening a .CAR from the File Manager
 * immediately unpacked every member into whatever folder you happened to be
 * standing in, and since members are written with "wb", any file already there
 * under the same name was gone. No listing, no count, no warning -- the first
 * thing you learned about an archive's contents was what it had replaced.
 *
 * So this window reads the archive's DIRECTORY and nothing else. That is
 * cheap by construction: car_core.c stores fixed 32-byte records at the front
 * of the file precisely so a reader can list an archive without decompressing
 * a byte of it, and this is the code that finally takes that offer. A 700 KB
 * archive is listed by reading its first eight kilobytes.
 *
 * Everything the list says comes from car_core.c, which is pure and tested
 * (tests/test_car.c) and refuses a member whose name could escape the folder
 * it is extracted to. A directory this file will not display is a directory
 * car_unpack() will not extract, because both of them ask the same function.
 *
 * The overwrite count is the part that earns the window. Before you press
 * Extract, it tells you how many of the members already exist where they are
 * about to be written. It is a plain statement of fact rather than a modal
 * dialog, because the archives this ships with are backups of the folder you
 * are standing in and replacing those files is usually the whole point --
 * but "usually" is not "always", and the difference should be visible.
 */
#include "apps.h"
#include "car_core.h"
#include "cz_file.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#include <string.h>
#include <stdlib.h>

#define AR_ROW_H      13
#define AR_TOOLBAR_H  25
#define AR_HEAD_H     14          /* the column headings strip              */
#define AR_STATUS_H   32          /* two lines: totals, then the warning    */

typedef struct {
    char  name[CAR_NAME_MAX];
    cu32  original;
    cu32  stored;
    int   method;
    cbool exists;                 /* a file of this name is already there   */
} ArRow;

/*
 * A window's user pointer is whatever its app put there, so "is this an
 * Archive window?" cannot be answered by casting and hoping. The headless
 * driver asks exactly that question, and a wrong answer would have it reading
 * a Notepad's buffer as an archive listing, so the struct says what it is.
 */
#define AR_MAGIC 0x43415231UL          /* 'CAR1' */

typedef struct {
    cu32  magic;
    char  path[CASTALIA_MAX_PATH];   /* the archive                         */
    char  dir[CASTALIA_MAX_PATH];    /* the folder it would extract into    */
    char  title[CASTALIA_MAX_NAME];
    ArRow row[CAR_MAX_FILES];
    int   count;
    int   sel, top;
    long  total_original, total_stored;
    int   clashes;
    cbool damaged;                /* the directory did not survive reading  */
    char  status[128];
} Archive;

typedef struct {
    CRect toolbar, extract, head, list, status;
    int   visible;
} ArLayout;

/*
 * The three number columns, as right-hand edges measured in from the list's
 * right side. Taken once and used by both the headings and the rows, because
 * a header that does not sit over its column is worse than no header.
 */
typedef struct { int orig, stored, saved, flag; } ArCols;

static void ar_cols(const CRect *list_in, ArCols *c)
{
    c->saved  = list_in->x1 - 8;
    c->stored = c->saved - 46;
    c->orig   = c->stored - 74;
    c->flag   = list_in->x0 + 122;    /* where "replaces" is written */
}

static void ar_layout(WmWindow *win, ArLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    int body = ch - AR_TOOLBAR_H - AR_HEAD_H - AR_STATUS_H;
    if (body < AR_ROW_H) { body = AR_ROW_H; }
    L->toolbar = crect_make(0, 0, cw, AR_TOOLBAR_H);
    L->extract = crect_make(4, 3, 78, AR_TOOLBAR_H - 7);
    L->head    = crect_make(3, AR_TOOLBAR_H, cw - 6, AR_HEAD_H);
    L->list    = crect_make(3, AR_TOOLBAR_H + AR_HEAD_H, cw - 6, body - 3);
    L->status  = crect_make(0, ch - AR_STATUS_H, cw, AR_STATUS_H);
    L->visible = (crect_h(&L->list) - 4) / AR_ROW_H;
    if (L->visible < 1) { L->visible = 1; }
}

/* The folder an archive sits in: where Extract puts its members. */
static void ar_parent_dir(const char *path, char *out, cu32 cap)
{
    cu32 i, cut = 0;
    sys_strlcpy(out, path, cap);
    for (i = 0; out[i] != '\0'; i++) {
        if (out[i] == '/' || out[i] == '\\') { cut = i; }
    }
    if (cut > 0) { out[cut] = '\0'; } else { sys_strlcpy(out, ".", cap); }
}

/*
 * Read the directory, and only the directory (car_list, in cz_file.c -- the
 * same reader the restore path counts clashes with, so the window and the
 * thing that acts on it cannot disagree about what is in an archive).
 *
 * The window then adds what only it needs: which members already exist where
 * they would land, and what the whole thing adds up to.
 */
static void ar_read(Archive *ar)
{
    static CarEntry ent[CAR_MAX_FILES];
    int n, i;

    ar->count = 0;
    ar->damaged = CFALSE;
    ar->total_original = 0;
    ar->total_stored = 0;
    ar->clashes = 0;

    n = car_list(ar->path, ent, CAR_MAX_FILES);
    if (n < 0) {
        ar->damaged = CTRUE;
        sys_strlcpy(ar->status, "Not a .CAR archive, or a damaged one",
                    sizeof(ar->status));
        return;
    }
    for (i = 0; i < n; i++) {
        ArRow *r = &ar->row[ar->count++];
        char probe[CASTALIA_MAX_PATH];
        sys_strlcpy(r->name, ent[i].name, sizeof(r->name));
        r->original = ent[i].original;
        r->stored = ent[i].stored;
        r->method = ent[i].method;
        sys_snprintf(probe, sizeof(probe), "%s/%s", ar->dir, r->name);
        r->exists = plat_file_exists(probe);
        if (r->exists) { ar->clashes++; }
        ar->total_original += (long)ent[i].original;
        ar->total_stored += (long)ent[i].stored;
    }
    ar->status[0] = '\0';
}

static Archive *ar_of(WmWindow *win);

static void ar_do_extract(WmWindow *win, Archive *ar)
{
    int files = 0;
    CResult rc = car_unpack(ar->path, ar->dir, &files);
    if (rc == CE_OK) {
        sys_snprintf(ar->status, sizeof(ar->status),
                     "Extracted %d file(s) into this folder", files);
    } else if (rc == CE_INVALID) {
        sys_strlcpy(ar->status, "Refused: the archive is damaged, or names a "
                    "file it may not write", sizeof(ar->status));
    } else if (rc == CE_NOMEM) {
        sys_strlcpy(ar->status, "Not enough memory to extract that archive",
                    sizeof(ar->status));
    } else {
        sys_snprintf(ar->status, sizeof(ar->status),
                     "Extracted %d file(s), then could not continue", files);
    }
    /* Whatever happened, the clash count is now stale. */
    {
        int i;
        ar->clashes = 0;
        for (i = 0; i < ar->count; i++) {
            char probe[CASTALIA_MAX_PATH];
            sys_snprintf(probe, sizeof(probe), "%s/%s", ar->dir,
                         ar->row[i].name);
            ar->row[i].exists = (plat_file_size(probe) >= 0) ? CTRUE : CFALSE;
            if (ar->row[i].exists) { ar->clashes++; }
        }
    }
    wm_invalidate(win, NULL);
}

static void ar_on_confirm(UiDialogResult result, void *user)
{
    WmWindow *win = (WmWindow *)user;
    Archive *ar = ar_of(win);
    if (result == UI_DR_YES && ar != NULL) { ar_do_extract(win, ar); }
}

/*
 * Extract, asking first only if there is something to lose.
 *
 * Nothing in the way means no question -- a backup restored into an empty
 * folder should not need a decision. Files in the way means a decision, of the
 * same shape the File Manager already uses before overwriting a single pasted
 * file, because restoring an archive over them is the same act performed forty
 * times and should not be quieter for being bigger.
 */
static void ar_extract(WmWindow *win, Archive *ar)
{
    char msg[160];
    if (ar == NULL || ar->count < 1) { return; }
    if (ar->clashes < 1) { ar_do_extract(win, ar); return; }
    sys_snprintf(msg, sizeof(msg),
                 "%d of the %d file(s) in this archive already exist here\n"
                 "and will be replaced.\n\nRestore anyway?",
                 ar->clashes, ar->count);
    ui_msgbox("Extract", msg, UI_MB_YESNO, ar_on_confirm, win);
}

/* "1,234" -- thousands separated, because these are byte counts and a bare
 * seven-digit number is unreadable at 8x8. */
static void ar_bytes(char *out, cu32 cap, long v)
{
    char raw[24];
    int n, i, j = 0;
    sys_snprintf(raw, sizeof(raw), "%ld", v);
    n = (int)sys_strnlen(raw, sizeof(raw));
    for (i = 0; i < n && (cu32)j + 2u < cap; i++) {
        if (i > 0 && ((n - i) % 3) == 0) { out[j++] = ','; }
        out[j++] = raw[i];
    }
    out[j] = '\0';
}

static void ar_paint(WmWindow *win, GfxSurface *s)
{
    Archive *ar = (Archive *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    ArLayout L;
    CRect r, save;
    int i;
    char buf[128], num[24];

    if (ar == NULL) { return; }
    ar_layout(win, &L);

    /* toolbar */
    r = crect_offset(&L.toolbar, o.x, o.y);
    gfx_fill_rect(s, &r, p->face);
    gfx_hline(s, r.x0, r.y1 - 1, crect_w(&r), p->dark);
    r = crect_offset(&L.extract, o.x, o.y);
    ui_draw_button(s, &r, "Extract All",
                   (ar->count > 0) ? UI_BTN_NORMAL : UI_BTN_DISABLED);
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x1 + 10, r.y0 + 3, ar->title, p->text);

    /* the list */
    r = crect_offset(&L.list, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, GFX_NO_FILL);
    {
        CRect in = crect_inset(&r, 2);
        ArCols cols;
        int col_orig, col_stored, col_saved;
        cbool bar = (ar->count > L.visible) ? CTRUE : CFALSE;
        if (bar) { in.x1 -= UI_SB_W; }     /* the rows stop short of the bar */
        ar_cols(&in, &cols);
        col_orig = cols.orig; col_stored = cols.stored; col_saved = cols.saved;

        /* Column headings, over the columns they name. */
        {
            CRect hr = crect_offset(&L.head, o.x, o.y);
            gfx_fill_rect(s, &hr, p->face);
            gfx_hline(s, hr.x0, hr.y1 - 1, crect_w(&hr), p->dark);
            gfx_draw_text(s, GFX_FONT_SYSTEM, in.x0 + 4, hr.y0 + 3, "Name",
                          p->text);
            gfx_draw_text(s, GFX_FONT_SYSTEM,
                          col_orig - gfx_text_width(GFX_FONT_SYSTEM, "Original"),
                          hr.y0 + 3, "Original", p->text);
            gfx_draw_text(s, GFX_FONT_SYSTEM,
                          col_stored - gfx_text_width(GFX_FONT_SYSTEM, "Stored"),
                          hr.y0 + 3, "Stored", p->text);
            gfx_draw_text(s, GFX_FONT_SYSTEM,
                          col_saved - gfx_text_width(GFX_FONT_SYSTEM, "Saved"),
                          hr.y0 + 3, "Saved", p->text);
        }
        if (bar) {
            CRect sb = crect_make(in.x1, in.y0, UI_SB_W, crect_h(&in));
            ui_scrollbar_draw(s, &sb, CTRUE, ar->count, L.visible, ar->top,
                              UI_SB_NONE);
        }
        gfx_fill_rect(s, &in, GFX_RGB(0xFF, 0xFF, 0xFF));
        save = gfx_clip_narrow(s, &in);
        for (i = 0; i < L.visible; i++) {
            int idx = ar->top + i;
            CRect row_r;
            CColor ink;
            const ArRow *m;
            if (idx >= ar->count) { break; }
            m = &ar->row[idx];
            row_r = crect_make(in.x0, in.y0 + i * AR_ROW_H, crect_w(&in),
                               AR_ROW_H);
            if (idx == ar->sel) { gfx_fill_rect(s, &row_r, p->accent); }
            ink = (idx == ar->sel) ? p->accent_text : p->text;
            gfx_draw_text(s, GFX_FONT_SYSTEM, row_r.x0 + 4, row_r.y0 + 3,
                          m->name, ink);
            /* A member that would replace something gets said so on its own
             * row, not only in the total at the bottom. */
            if (m->exists) {
                gfx_draw_text(s, GFX_FONT_SYSTEM, row_r.x0 + 122, row_r.y0 + 3,
                              "replaces",
                              (idx == ar->sel) ? p->accent_text
                                               : GFX_RGB(0xA0, 0x40, 0x28));
            }
            ar_bytes(num, sizeof(num), (long)m->original);
            gfx_draw_text(s, GFX_FONT_SYSTEM,
                          col_orig - gfx_text_width(GFX_FONT_SYSTEM, num),
                          row_r.y0 + 3, num, ink);
            ar_bytes(num, sizeof(num), (long)m->stored);
            gfx_draw_text(s, GFX_FONT_SYSTEM,
                          col_stored - gfx_text_width(GFX_FONT_SYSTEM, num),
                          row_r.y0 + 3, num, ink);
            if (m->original > 0u) {
                /* Per member, and a single member is exactly the thing
                 * that gets large -- a video or a disk image. Same 20.5 MB
                 * wrap as the total below. */
                sys_snprintf(num, sizeof(num), "%ld%%",
                             100L - (long)((ui_meter_fill((cs32)m->stored,
                                                          (cs32)m->original,
                                                          1000) + 5) / 10));
            } else {
                sys_strlcpy(num, "-", sizeof(num));
            }
            gfx_draw_text(s, GFX_FONT_SYSTEM,
                          col_saved - gfx_text_width(GFX_FONT_SYSTEM, num),
                          row_r.y0 + 3, num, ink);
        }
        if (ar->count == 0) {
            gfx_draw_text(s, GFX_FONT_SYSTEM, in.x0 + 6, in.y0 + 6,
                          ar->damaged ? "Nothing readable in this archive"
                                      : "This archive is empty",
                          GFX_RGB(0x80, 0x80, 0x88));
        }
        gfx_set_clip(s, &save);
    }

    /* status: what is in it, and what extracting would cost */
    r = crect_offset(&L.status, o.x, o.y);
    gfx_fill_rect(s, &r, p->face);
    gfx_hline(s, r.x0, r.y0, crect_w(&r), p->light);
    if (ar->status[0] != '\0') {
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 6, r.y0 + 3, ar->status,
                      p->text);
    } else {
        char a[24], b[24];
        ar_bytes(a, sizeof(a), ar->total_original);
        ar_bytes(b, sizeof(b), ar->total_stored);
        if (ar->total_original > 0) {
            sys_snprintf(buf, sizeof(buf),
                         "%d member(s), %s bytes stored as %s (%ld%% saved)",
                         ar->count, a, b,
                         /* (stored * 100) wraps at 20.5 MB; an archive is
                          * exactly the thing that gets that big. */
                         100L - (long)((ui_meter_fill((cs32)ar->total_stored,
                                                      (cs32)ar->total_original,
                                                      1000) + 5) / 10));
        } else {
            sys_snprintf(buf, sizeof(buf), "%d member(s)", ar->count);
        }
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 6, r.y0 + 3, buf, p->text);
    }
    if (ar->clashes > 0) {
        sys_snprintf(buf, sizeof(buf),
                     "%d of them already exist here and would be replaced",
                     ar->clashes);
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 6, r.y0 + 16, buf,
                      GFX_RGB(0xA0, 0x40, 0x28));
    } else if (ar->count > 0) {
        gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 6, r.y0 + 16,
                      "None of them exist here yet", p->text_disabled);
    }
}

static void ar_scroll_to(Archive *ar, int visible)
{
    if (ar->sel < ar->top) { ar->top = ar->sel; }
    if (ar->sel >= ar->top + visible) { ar->top = ar->sel - visible + 1; }
    if (ar->top < 0) { ar->top = 0; }
}

static cbool ar_click(WmWindow *win, Archive *ar, int x, int y)
{
    ArLayout L;
    ar_layout(win, &L);
    if (crect_contains(&L.extract, x, y) && ar->count > 0) {
        ar_extract(win, ar);
        return CTRUE;
    }
    if (crect_contains(&L.list, x, y)) {
        int idx = ar->top + (y - L.list.y0 - 2) / AR_ROW_H;
        if (idx >= 0 && idx < ar->count) {
            ar->sel = idx;
            wm_invalidate(win, NULL);
        }
        return CTRUE;
    }
    return CFALSE;
}

static cbool ar_key(WmWindow *win, Archive *ar, int key)
{
    ArLayout L;
    ar_layout(win, &L);
    switch (key) {
    case PLAT_KEY_UP:    if (ar->sel > 0) { ar->sel--; } break;
    case PLAT_KEY_DOWN:  if (ar->sel + 1 < ar->count) { ar->sel++; } break;
    case PLAT_KEY_HOME:  ar->sel = 0; break;
    case PLAT_KEY_END:   ar->sel = ar->count - 1; break;
    case PLAT_KEY_PGUP:  ar->sel -= L.visible; break;
    case PLAT_KEY_PGDN:  ar->sel += L.visible; break;
    case PLAT_KEY_ENTER:
        if (ar->count > 0) { ar_extract(win, ar); }
        return CTRUE;
    default: return CFALSE;
    }
    if (ar->sel < 0) { ar->sel = 0; }
    if (ar->sel >= ar->count) { ar->sel = ar->count - 1; }
    if (ar->sel < 0) { ar->sel = 0; }
    ar_scroll_to(ar, L.visible);
    wm_invalidate(win, NULL);
    return CTRUE;
}

static cbool archive_proc(WmWindow *win, WmMessage msg, long a, long b,
                          void *param)
{
    Archive *ar = (Archive *)wm_user(win);
    CASTALIA_UNUSED(b);
    switch (msg) {
    case WM_MSG_PAINT:       ar_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: return ar_click(win, ar, (int)a, (int)b);
    case WM_MSG_MOUSEWHEEL: {
        ArLayout L;
        if (ar == NULL) { return CFALSE; }
        ar_layout(win, &L);
        if (ui_scroll_wheel(&ar->top, (int)a, ar->count, L.visible)) {
            CPoint o = wm_client_origin(win);
            CRect r = crect_offset(&L.list, o.x, o.y);
            wm_invalidate(win, &r);
        }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:     return ar_key(win, ar, (int)a);
    case WM_MSG_DESTROY:
        if (ar != NULL) { sys_free(ar, (cu32)sizeof(Archive)); }
        return CTRUE;
    default: return CFALSE;
    }
}

/* The window's contents, for the headless driver. NULL for any window that is
 * not an Archive view, so a scene cannot mistake one window for another. */
static Archive *ar_of(WmWindow *win)
{
    Archive *a = (win != NULL) ? (Archive *)wm_user(win) : NULL;
    return (a != NULL && a->magic == AR_MAGIC) ? a : NULL;
}

int app_archive_count(WmWindow *win)
{
    Archive *a = ar_of(win);
    return (a != NULL) ? a->count : -1;
}

int app_archive_clashes(WmWindow *win)
{
    Archive *a = ar_of(win);
    return (a != NULL) ? a->clashes : -1;
}

long app_archive_original(WmWindow *win)
{
    Archive *a = ar_of(win);
    return (a != NULL) ? a->total_original : -1L;
}

long app_archive_stored(WmWindow *win)
{
    Archive *a = ar_of(win);
    return (a != NULL) ? a->total_stored : -1L;
}

void app_archive_open(const char *path)
{
    Archive *ar;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw = 470, ch = 300, fx, fy;
    const char *base;

    if (path == NULL || path[0] == '\0') { return; }
    ar = (Archive *)sys_calloc(1, (cu32)sizeof(Archive));
    if (ar == NULL) { SYS_LOGE("app", "archive: OOM"); return; }
    ar->magic = AR_MAGIC;
    sys_strlcpy(ar->path, path, sizeof(ar->path));
    ar_parent_dir(path, ar->dir, sizeof(ar->dir));
    base = path;
    {
        const char *q = path;
        while (*q != '\0') {
            if (*q == '/' || *q == '\\') { base = q + 1; }
            q++;
        }
    }
    sys_strlcpy(ar->title, base, sizeof(ar->title));
    ar->sel = 0;
    ar_read(ar);
    SYS_LOGI("app", "archive: %s holds %d member(s), %d already here",
             ar->title, ar->count, ar->clashes);

    plat_video_info(&vi);
    if (cw > vi.width - 16) { cw = vi.width - 16; }
    if (ch > vi.height - 60) { ch = vi.height - 60; }
    fx = (vi.width - cw) / 2;
    fy = (vi.height - ch) / 2 - 12;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw, ch);

    w = wm_create("Archive", &frame, WM_STYLE_APP, archive_proc, ar);
    if (w == NULL) { sys_free(ar, (cu32)sizeof(Archive)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
}
