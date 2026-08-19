/*
 * app_diskuse.c - Disk Usage: where the space actually went.
 *
 * A folder listing sorted by size answers "which of these files is big". It
 * does not answer "which of these FOLDERS is big", and on a machine with a
 * few megabytes free that is the only question worth asking. This window
 * answers it two ways at once: a treemap where every rectangle's area is its
 * share of the total, and a sorted list beside it with the exact bytes.
 *
 * The treemap geometry is map_core.c -- pure, integer, hermetically tested --
 * so this file is only the skin: walk the tree, aggregate, colour by kind
 * (through assoc.c, so a .WAV is the same colour here as its icon is in the
 * File Manager), and route clicks.
 *
 * The scan is deliberately bounded and deliberately honest about it. A walk
 * that wanders into a deep tree and never returns would freeze a cooperative
 * single-threaded shell, so it stops at a depth and an entry count and SAYS
 * SO in the status line rather than quietly reporting a total that is missing
 * half the disk. An incomplete number presented as complete is worse than no
 * number.
 */
#include "apps.h"
#include "assoc.h"
#include "map_core.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#include <string.h>
#include <stdlib.h>

#define DU_MAX_ROWS   MAP_MAX_ITEMS  /* children shown; the tail is grouped */
#define DU_SCAN_CAP   40000          /* entries visited per scan            */
#define DU_MAX_DEPTH  16
#define DU_LIST_W     186
#define DU_ROW_H      13
#define DU_TOOLBAR_H  25
#define DU_STATUS_H   19
#define DU_DBLCLICK_MS 400           /* matches the File Manager's feel     */
/* One level of nesting: the biggest folders show what is inside them. That
 * is the difference between a chart and a tool -- a treemap of eight folders
 * all drawn in folder-yellow says nothing the list beside it did not already
 * say. Bounded on purpose: only the folders large enough for the detail to
 * be legible get it, and only their largest children. */
#define DU_SUB_PARENTS 8             /* folders that get an inside view     */
#define DU_SUB_ROWS    10            /* children shown inside one of them   */
#define DU_SUB_MAX     (DU_SUB_PARENTS * DU_SUB_ROWS)
#define DU_SUB_MIN_W   58            /* below this a nested block is mush   */
#define DU_SUB_MIN_H   40

typedef struct {
    char  name[CASTALIA_MAX_NAME];
    long  bytes;
    long  files;                 /* files underneath (1 for a plain file)  */
    cbool is_dir;
    cbool is_other;              /* the grouped tail, not a real entry     */
    AssocKind kind;
    int   sub_first;             /* index into DiskUse.sub, or -1          */
    int   sub_count;
} DuRow;

typedef struct {
    char   root[CASTALIA_MAX_PATH];   /* where the window was opened       */
    char   path[CASTALIA_MAX_PATH];   /* what is displayed now             */
    DuRow  row[DU_MAX_ROWS];
    int    count;
    long   total;
    long   scanned;
    cbool  truncated;
    int    sel;                       /* -1 = nothing picked               */
    int    hot;                       /* under the cursor, or -1           */
    int    top;                       /* first visible list row            */
    cu32   last_click_ms;
    int    last_click_row;
    DuRow  sub[DU_SUB_MAX];           /* children of the biggest folders   */
    int    sub_used;
    CRect  cell[DU_MAX_ROWS];         /* treemap rects, CLIENT coords      */
    char   status[96];
    UiHot hot_tb;  /* Up / Rescan under the pointer (du->hot is the map) */
} DiskUse;

/* ---- formatting ------------------------------------------------------- */
/* Bytes as the shortest honest string: exact under 1 KB, one decimal above.
 * Integer only -- the tenth is a remainder, not a float. */
static void du_size(long b, char *dst, cu32 dstsz)
{
    if (b < 0) { b = 0; }
    if (b < 1024L) {
        sys_snprintf(dst, dstsz, "%ld B", b);
    } else if (b < 1024L * 1024L) {
        sys_snprintf(dst, dstsz, "%ld.%ld KB", b / 1024L,
                     ((b % 1024L) * 10L) / 1024L);
    } else {
        long mb = b / (1024L * 1024L);
        long tenth = ((b % (1024L * 1024L)) * 10L) / (1024L * 1024L);
        sys_snprintf(dst, dstsz, "%ld.%ld MB", mb, tenth);
    }
}

static void du_join(const char *dir, const char *name, char *dst, cu32 dstsz)
{
    cu32 n;
    sys_strlcpy(dst, dir, dstsz);
    n = sys_strnlen(dst, dstsz);
    if (n > 0 && dst[n - 1] != '/' && dst[n - 1] != '\\') {
        sys_strlcat(dst, "/", dstsz);
    }
    sys_strlcat(dst, name, dstsz);
}

static const char *du_base(const char *p)
{
    const char *b = p;
    while (*p != '\0') { if (*p == '/' || *p == '\\') { b = p + 1; } p++; }
    return (*b != '\0') ? b : p;
}

/* ---- the scan --------------------------------------------------------- */
/* Total the bytes under 'dir'. Returns the sum; *files counts plain files.
 * Bounded by depth and by the shared entry budget in 'du'. */
static long du_walk(DiskUse *du, const char *dir, int depth, long *files)
{
    PlatDir *d;
    PlatDirEntry e;
    long sum = 0;
    if (depth > DU_MAX_DEPTH) { du->truncated = CTRUE; return 0; }
    d = plat_opendir(dir);
    if (d == NULL) { return 0; }
    while (plat_readdir(d, &e)) {
        char child[CASTALIA_MAX_PATH];
        if (strcmp(e.name, ".") == 0 || strcmp(e.name, "..") == 0) { continue; }
        if (du->scanned >= DU_SCAN_CAP) { du->truncated = CTRUE; break; }
        du->scanned++;
        if (e.is_dir) {
            du_join(dir, e.name, child, sizeof(child));
            sum += du_walk(du, child, depth + 1, files);
        } else {
            if (e.size > 0) { sum += e.size; }
            (*files)++;
        }
    }
    plat_closedir(d);
    return sum;
}

/* Sort 'n' rows by descending size. Insertion sort: the counts here are small
 * and stability on ties is what keeps a rescan from reshuffling equal-sized
 * siblings out from under the cursor. */
static void du_sort(DuRow *row, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        DuRow key = row[i];
        j = i - 1;
        while (j >= 0 && row[j].bytes < key.bytes) {
            row[j + 1] = row[j];
            j--;
        }
        row[j + 1] = key;
    }
}

/* Read one folder into 'row': each child folder totalled by walking it, each
 * file as itself. Returns how many rows were written and adds up '*total'.
 *
 * Everything past 'max' is folded into one "(N more items)" row rather than
 * dropped, so the rectangles still add up to the total the window claims to
 * be showing. A treemap missing its tail is a treemap lying about its area.
 */
static int du_read(DiskUse *du, const char *dir, DuRow *row, int max,
                   long *total)
{
    PlatDir *d;
    PlatDirEntry e;
    long other_bytes = 0, other_files = 0;
    int  other_count = 0, n = 0;

    *total = 0;
    if (max < 1) { return 0; }
    d = plat_opendir(dir);
    if (d == NULL) { return 0; }
    while (plat_readdir(d, &e)) {
        DuRow r;
        char child[CASTALIA_MAX_PATH];
        if (strcmp(e.name, ".") == 0 || strcmp(e.name, "..") == 0) { continue; }
        if (du->scanned >= DU_SCAN_CAP) { du->truncated = CTRUE; break; }
        du->scanned++;

        memset(&r, 0, sizeof(r));
        sys_strlcpy(r.name, e.name, sizeof(r.name));
        r.is_dir = e.is_dir;
        r.sub_first = -1;
        if (e.is_dir) {
            du_join(dir, e.name, child, sizeof(child));
            r.bytes = du_walk(du, child, 1, &r.files);
            r.kind = ASSOC_UNKNOWN;
        } else {
            r.bytes = (e.size > 0) ? e.size : 0;
            r.files = 1;
            r.kind = assoc_for(e.name);
        }
        *total += r.bytes;
        if (n < max - 1) {
            row[n++] = r;
        } else {
            other_bytes += r.bytes;
            other_files += r.files;
            other_count++;
        }
    }
    plat_closedir(d);

    if (other_count > 0) {
        DuRow *r = &row[n++];
        memset(r, 0, sizeof(*r));
        sys_snprintf(r->name, sizeof(r->name), "(%d more items)", other_count);
        r->bytes = other_bytes;
        r->files = other_files;
        r->is_other = CTRUE;
        r->kind = ASSOC_UNKNOWN;
        r->sub_first = -1;
    }
    du_sort(row, n);
    return n;
}

static void du_scan(DiskUse *du, const char *dir)
{
    int i;

    du->count = 0;
    du->total = 0;
    du->scanned = 0;
    du->sub_used = 0;
    du->truncated = CFALSE;
    du->sel = -1;
    du->hot = -1;
    du->top = 0;
    du->last_click_row = -1;
    sys_strlcpy(du->path, dir, sizeof(du->path));

    du->count = du_read(du, dir, du->row, DU_MAX_ROWS, &du->total);
    if (du->count == 0 && plat_opendir(dir) == NULL) {
        sys_snprintf(du->status, sizeof(du->status), "Cannot read %s",
                     du_base(dir));
        return;
    }

    /* One level down, for the biggest folders only. They are the ones whose
     * blocks are large enough for the detail to read, and stopping there is
     * what keeps the scan bounded on a deep tree. */
    for (i = 0; i < du->count && i < DU_SUB_PARENTS; i++) {
        char child[CASTALIA_MAX_PATH];
        long sub_total = 0;
        int  room = DU_SUB_MAX - du->sub_used;
        if (!du->row[i].is_dir || du->row[i].bytes <= 0) { continue; }
        if (room > DU_SUB_ROWS) { room = DU_SUB_ROWS; }
        if (room < 2) { break; }
        du_join(dir, du->row[i].name, child, sizeof(child));
        du->row[i].sub_first = du->sub_used;
        du->row[i].sub_count = du_read(du, child, &du->sub[du->sub_used],
                                       room, &sub_total);
        du->sub_used += du->row[i].sub_count;
        if (du->row[i].sub_count == 0) { du->row[i].sub_first = -1; }
    }

    {
        char sz[32];
        du_size(du->total, sz, sizeof(sz));
        if (du->truncated) {
            sys_snprintf(du->status, sizeof(du->status),
                         "%s in %d items (partial: scan limit reached)",
                         sz, du->count);
        } else {
            sys_snprintf(du->status, sizeof(du->status), "%s in %d items",
                         sz, du->count);
        }
    }
    SYS_LOGI("app", "diskuse: %s -> %ld bytes, %d rows%s", du->path,
             du->total, du->count, du->truncated ? " (partial)" : "");
}

/* ---- colours ---------------------------------------------------------- */
/* One colour per kind, folders distinct from everything. These are the same
 * families the File Manager's icons use, so the map reads as the same
 * filesystem rather than as an unrelated chart.
 *
 * 'shade' walks a small ramp so that adjacent folders -- which would all be
 * the same yellow -- are still told apart. It steps lightness, never hue: a
 * folder must not be mistakable for a bitmap because it happened to sort
 * third. */
static CColor du_shift(CColor c, int delta)
{
    int r = GFX_R(c) + delta, g = GFX_G(c) + delta, b = GFX_B(c) + delta;
    if (r < 0) { r = 0; } if (r > 255) { r = 255; }
    if (g < 0) { g = 0; } if (g > 255) { g = 255; }
    if (b < 0) { b = 0; } if (b > 255) { b = 255; }
    return GFX_RGB(r, g, b);
}

static CColor du_color(const DuRow *r, int shade, cbool hot)
{
    static const int RAMP[4] = { 0, -26, 16, -12 };
    CColor c;
    if (r->is_other)     { c = GFX_RGB(0x8C, 0x8C, 0x94); }
    else if (r->is_dir)  { c = GFX_RGB(0xE0, 0xAE, 0x3A); }
    else {
        switch (r->kind) {
        case ASSOC_IMAGE:   c = GFX_RGB(0x6E, 0xB2, 0x62); break;
        case ASSOC_AUDIO:   c = GFX_RGB(0xB9, 0x74, 0xCE); break;
        case ASSOC_TEXT:    c = GFX_RGB(0x74, 0xA6, 0xDE); break;
        case ASSOC_WRITE:   c = GFX_RGB(0x52, 0x86, 0xC4); break;
        case ASSOC_SHEET:   c = GFX_RGB(0x4E, 0xA8, 0x92); break;
        case ASSOC_PROGRAM: c = GFX_RGB(0xC9, 0x6B, 0x5A); break;
        case ASSOC_PACKAGE: c = GFX_RGB(0xD0, 0x93, 0x44); break;
        case ASSOC_THEME:   c = GFX_RGB(0xA0, 0x9A, 0x60); break;
        default:            c = GFX_RGB(0x9A, 0x9E, 0xA8); break;
        }
    }
    if (shade > 0) { c = du_shift(c, RAMP[shade & 3]); }
    if (hot) {
        /* Lift toward white rather than switching hue: the block stays
         * identifiable as the same file while it is under the cursor. */
        c = du_shift(c, 50);
    }
    return c;
}

/* ---- layout ----------------------------------------------------------- */
typedef struct {
    CRect toolbar, up, rescan, list, map, status;
    int   visible;
} DuLayout;

static void du_layout(WmWindow *win, DuLayout *L)
{
    CRect c = wm_client_rect(win);
    int cw = crect_w(&c), ch = crect_h(&c);
    int body = ch - DU_TOOLBAR_H - DU_STATUS_H;
    if (body < DU_ROW_H) { body = DU_ROW_H; }
    L->toolbar = crect_make(0, 0, cw, DU_TOOLBAR_H);
    L->up      = crect_make(4, 3, 54, DU_TOOLBAR_H - 7);
    L->rescan  = crect_make(61, 3, 62, DU_TOOLBAR_H - 7);
    L->list    = crect_make(3, DU_TOOLBAR_H, DU_LIST_W, body - 3);
    L->map     = crect_make(DU_LIST_W + 7, DU_TOOLBAR_H,
                            cw - DU_LIST_W - 10, body - 3);
    L->status  = crect_make(0, ch - DU_STATUS_H, cw, DU_STATUS_H);
    L->visible = (crect_h(&L->list) - 4) / DU_ROW_H;
    if (L->visible < 1) { L->visible = 1; }
}

/* Lay the treemap out into 'map'. Client coordinates, because that is what
 * mouse messages arrive in -- converting once here beats converting on every
 * hit test. */
static void du_map(DiskUse *du, const CRect *map)
{
    long w[DU_MAX_ROWS];
    int  i;
    for (i = 0; i < du->count; i++) { w[i] = du->row[i].bytes; }
    for (i = du->count; i < DU_MAX_ROWS; i++) { du->cell[i] = crect_make(0, 0, 0, 0); }
    map_layout(w, du->count, map, du->cell);
}

/* ---- paint ------------------------------------------------------------ */
/* Draw one block: fill, a dark separating edge, a lit top line, and the name
 * when there is room for it. Returns the interior left for nesting. */
static CRect du_paint_block(GfxSurface *s, const CRect *r, const DuRow *row,
                            int shade, cbool hot, cbool picked)
{
    CColor fill = du_color(row, shade, hot);
    CColor edge = GFX_RGB(0x20, 0x20, 0x24);
    int rw = crect_w(r), rh = crect_h(r);
    CRect inner = crect_make(0, 0, 0, 0);
    if (rw <= 0 || rh <= 0) { return inner; }
    gfx_fill_rect(s, r, fill);
    /* A one-pixel dark edge is what separates one share from the next; the
     * treemap is unreadable without it once blocks share a colour. */
    if (rw > 2 && rh > 2) {
        gfx_frame_rect(s, r, edge);
        gfx_hline(s, r->x0 + 1, r->y0 + 1, rw - 2, du_shift(fill, 42));
    }
    if (picked && rw > 4 && rh > 4) {
        CRect f = crect_inset(r, 1);
        gfx_frame_rect(s, &f, GFX_RGB(0xFF, 0xFF, 0xFF));
        f = crect_inset(r, 2);
        gfx_frame_rect(s, &f, GFX_RGB(0x00, 0x00, 0x00));
    }
    /* The name only when it fits: a clipped label on a small block is noise,
     * and the list beside the map already names everything. */
    if (rw > 42 && rh > 14) {
        int tw = gfx_text_width(GFX_FONT_SYSTEM, row->name);
        if (tw <= rw - 6) {
            CColor ink = (GFX_R(fill) + GFX_G(fill) + GFX_B(fill) > 420)
                       ? GFX_RGB(0x10, 0x10, 0x14) : GFX_RGB(0xFF, 0xFF, 0xFF);
            gfx_draw_text(s, GFX_FONT_SYSTEM, r->x0 + 3, r->y0 + 3,
                          row->name, ink);
            inner = crect_make_xyxy(r->x0 + 3, r->y0 + 13, r->x1 - 3, r->y1 - 3);
        }
    }
    return inner;
}

/* Draw a folder's children inside its own block. The nested rectangles are
 * shares of the PARENT, not of the whole window, which is exactly what makes
 * the picture readable: a big folder's contents get the space to show
 * themselves instead of being crushed against the global scale. */
static void du_paint_nested(GfxSurface *s, const CRect *inner,
                            const DuRow *sub, int n)
{
    long w[DU_SUB_ROWS];
    CRect cell[DU_SUB_ROWS];
    int i;
    if (n < 1 || crect_w(inner) < DU_SUB_MIN_W - 8 ||
        crect_h(inner) < DU_SUB_MIN_H - 16) { return; }
    if (n > DU_SUB_ROWS) { n = DU_SUB_ROWS; }
    for (i = 0; i < n; i++) { w[i] = sub[i].bytes; }
    if (map_layout(w, n, inner, cell) < 1) { return; }
    for (i = 0; i < n; i++) {
        CColor fill;
        int cw = crect_w(&cell[i]), chh = crect_h(&cell[i]);
        if (cw < 2 || chh < 2) { continue; }
        fill = du_color(&sub[i], (i % 3) + 1, CFALSE);
        gfx_fill_rect(s, &cell[i], fill);
        gfx_frame_rect(s, &cell[i], GFX_RGB(0x2A, 0x2A, 0x30));
        if (cw > 40 && chh > 12 &&
            gfx_text_width(GFX_FONT_SYSTEM, sub[i].name) <= cw - 6) {
            CColor ink = (GFX_R(fill) + GFX_G(fill) + GFX_B(fill) > 420)
                       ? GFX_RGB(0x10, 0x10, 0x14) : GFX_RGB(0xFF, 0xFF, 0xFF);
            gfx_draw_text(s, GFX_FONT_SYSTEM, cell[i].x0 + 2, cell[i].y0 + 2,
                          sub[i].name, ink);
        }
    }
}

static void du_paint(WmWindow *win, GfxSurface *s)
{
    DiskUse *du = (DiskUse *)wm_user(win);
    const UiPalette *p = ui_palette();
    CPoint o = wm_client_origin(win);
    DuLayout L;
    CRect r, save;
    int i;
    char buf[96];

    if (du == NULL) { return; }
    du_layout(win, &L);

    /* toolbar */
    r = L.toolbar; r = crect_offset(&r, o.x, o.y);
    gfx_fill_rect(s, &r, p->face);
    gfx_hline(s, r.x0, r.y1 - 1, crect_w(&r), p->dark);
    r = L.up; r = crect_offset(&r, o.x, o.y);
    ui_draw_button(s, &r, "Up", ui_hot_state(&du->hot_tb, 0, UI_BTN_NORMAL));
    r = L.rescan; r = crect_offset(&r, o.x, o.y);
    ui_draw_button(s, &r, "Rescan",
                   ui_hot_state(&du->hot_tb, 1, UI_BTN_NORMAL));
    {
        /* The path, cut from the LEFT when it is too long: the tail is the
         * part that says where you are, so a deep path loses its root rather
         * than its folder. */
        int tx = L.rescan.x1 + 10 + o.x;
        int avail = crect_w(&L.toolbar) - L.rescan.x1 - 14;
        ui_path_fit(buf, sizeof(buf), du->path, avail, GFX_FONT_SYSTEM);
        gfx_draw_text(s, GFX_FONT_SYSTEM, tx, r.y0 + 3, buf, p->text);
    }

    /* the list */
    r = L.list; r = crect_offset(&r, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, GFX_NO_FILL);
    {
        CRect in = crect_inset(&r, 2);
        gfx_fill_rect(s, &in, GFX_RGB(0xFF, 0xFF, 0xFF));
        save = gfx_clip_narrow(s, &in);
        for (i = 0; i < L.visible; i++) {
            int idx = du->top + i;
            CRect row_r, sw;
            if (idx >= du->count) { break; }
            row_r = crect_make(in.x0, in.y0 + i * DU_ROW_H,
                               crect_w(&in), DU_ROW_H);
            if (idx == du->sel) {
                gfx_fill_rect(s, &row_r, p->accent);
            } else if (idx == du->hot) {
                gfx_fill_rect(s, &row_r, GFX_RGB(0xE4, 0xEC, 0xF6));
            }
            sw = crect_make(row_r.x0 + 3, row_r.y0 + 3, 8, 8);
            gfx_fill_rect(s, &sw, du_color(&du->row[idx], (idx % 3) + 1, CFALSE));
            gfx_frame_rect(s, &sw, GFX_RGB(0x30, 0x30, 0x38));
            {
                CColor ink = (idx == du->sel) ? p->accent_text : p->text;
                char nm[40];
                sys_strlcpy(nm, du->row[idx].name, sizeof(nm));
                /* The size column is fixed-width on the right; the name gets
                 * whatever is left and is cut, not overlapped. */
                {
                    int avail = crect_w(&row_r) - 16 - 58;
                    while (nm[0] != '\0' &&
                           gfx_text_width(GFX_FONT_SYSTEM, nm) > avail) {
                        nm[sys_strnlen(nm, sizeof(nm)) - 1] = '\0';
                    }
                }
                gfx_draw_text(s, GFX_FONT_SYSTEM, row_r.x0 + 15, row_r.y0 + 2,
                              nm, ink);
                du_size(du->row[idx].bytes, buf, sizeof(buf));
                gfx_draw_text(s, GFX_FONT_SYSTEM,
                              row_r.x1 - 4 - gfx_text_width(GFX_FONT_SYSTEM, buf),
                              row_r.y0 + 2, buf, ink);
            }
        }
        gfx_set_clip(s, &save);
    }

    /* the map */
    r = L.map; r = crect_offset(&r, o.x, o.y);
    gfx_bevel(s, &r, GFX_BEVEL_SUNKEN_THIN, p->light, p->dark, GFX_NO_FILL);
    {
        CRect well = crect_inset(&L.map, 2);      /* client coords          */
        CRect in = crect_offset(&well, o.x, o.y); /* screen coords          */
        gfx_fill_rect(s, &in, GFX_RGB(0x36, 0x38, 0x40));
        du_map(du, &well);
        save = gfx_clip_narrow(s, &in);
        for (i = du->count - 1; i >= 0; i--) {
            CRect blk = crect_offset(&du->cell[i], o.x, o.y);
            CRect inner = du_paint_block(s, &blk, &du->row[i], (i % 3) + 1,
                                         (i == du->hot), (i == du->sel));
            if (du->row[i].sub_first >= 0 &&
                crect_w(&blk) >= DU_SUB_MIN_W && crect_h(&blk) >= DU_SUB_MIN_H) {
                du_paint_nested(s, &inner, &du->sub[du->row[i].sub_first],
                                du->row[i].sub_count);
            }
        }
        gfx_set_clip(s, &save);
        if (du->count == 0) {
            gfx_draw_text(s, GFX_FONT_SYSTEM, in.x0 + 8, in.y0 + 8,
                          "This folder is empty.", GFX_RGB(0xC0, 0xC4, 0xCC));
        }
    }

    /* status */
    r = L.status; r = crect_offset(&r, o.x, o.y);
    gfx_fill_rect(s, &r, p->face);
    gfx_hline(s, r.x0, r.y0, crect_w(&r), p->light);
    if (du->sel >= 0 && du->sel < du->count) {
        const DuRow *sr = &du->row[du->sel];
        char sz[32];
        /* (bytes * 100) / total wraps at 20.5 MB on a 32-bit long, and a
         * folder that size is ordinary. Scaled to 1000 by a helper that
         * cannot overflow, then rounded to the nearest percent as before. */
        long pct = (long)((ui_meter_fill((cs32)sr->bytes, (cs32)du->total,
                                         1000) + 5) / 10);
        du_size(sr->bytes, sz, sizeof(sz));
        if (sr->is_dir) {
            sys_snprintf(buf, sizeof(buf), "%s  --  %s in %ld files (%ld%%)",
                         sr->name, sz, sr->files, pct);
        } else {
            sys_snprintf(buf, sizeof(buf), "%s  --  %s (%ld%%)  %s",
                         sr->name, sz, pct, assoc_type_name(sr->name));
        }
    } else {
        sys_strlcpy(buf, du->status, sizeof(buf));
    }
    gfx_draw_text(s, GFX_FONT_SYSTEM, r.x0 + 5, r.y0 + 4, buf, p->text);
}

/* ---- navigation ------------------------------------------------------- */
static void du_scroll_to_sel(DiskUse *du, int visible)
{
    if (du->sel < 0) { return; }
    if (du->sel < du->top) { du->top = du->sel; }
    if (du->sel >= du->top + visible) { du->top = du->sel - visible + 1; }
    if (du->top < 0) { du->top = 0; }
}

/* Descend into the selected row if it is a folder. */
static cbool du_enter(WmWindow *win, DiskUse *du)
{
    char child[CASTALIA_MAX_PATH];
    if (du->sel < 0 || du->sel >= du->count) { return CFALSE; }
    if (!du->row[du->sel].is_dir) { return CFALSE; }
    du_join(du->path, du->row[du->sel].name, child, sizeof(child));
    du_scan(du, child);
    wm_invalidate(win, NULL);
    return CTRUE;
}

/* Up one level, but never above the folder the window was opened on: a disk
 * usage window that can wander out of its root is a file manager, and a bad
 * one. */
static cbool du_up(WmWindow *win, DiskUse *du)
{
    char parent[CASTALIA_MAX_PATH];
    int i, last = -1;
    if (sys_stricmp(du->path, du->root) == 0) { return CFALSE; }
    sys_strlcpy(parent, du->path, sizeof(parent));
    i = (int)sys_strnlen(parent, sizeof(parent));
    if (i > 1 && (parent[i - 1] == '/' || parent[i - 1] == '\\')) {
        parent[i - 1] = '\0';
    }
    for (i = 0; parent[i] != '\0'; i++) {
        if (parent[i] == '/' || parent[i] == '\\') { last = i; }
    }
    if (last < 0) { return CFALSE; }
    parent[(last == 0) ? 1 : last] = '\0';
    du_scan(du, parent);
    wm_invalidate(win, NULL);
    return CTRUE;
}

/* Select row 'idx' from either half of the window; a second click on the same
 * row within the double-click window descends into it. The list and the map
 * are two views of one selection, so both routes land here. */
static void du_pick(WmWindow *win, DiskUse *du, int idx, int visible)
{
    cu32 now = sys_now_ms();
    cbool dbl = (idx == du->last_click_row &&
                 (now - du->last_click_ms) < DU_DBLCLICK_MS) ? CTRUE : CFALSE;
    du->sel = idx;
    du->last_click_row = idx;
    du->last_click_ms = now;
    du_scroll_to_sel(du, visible);
    wm_invalidate(win, NULL);
    if (dbl) { du_enter(win, du); }
}

static cbool du_click(WmWindow *win, DiskUse *du, int x, int y)
{
    DuLayout L;
    int i;

    du_layout(win, &L);
    /* Mouse messages arrive in client coordinates, which is exactly what the
     * layout produces -- so nothing here is offset. */
    if (crect_contains(&L.up, x, y)) {
        (void)ui_hot_press(&du->hot_tb, 0);
        return du_up(win, du);
    }
    if (crect_contains(&L.rescan, x, y)) {
        (void)ui_hot_press(&du->hot_tb, 1);
        du_scan(du, du->path); wm_invalidate(win, NULL); return CTRUE;
    }

    if (crect_contains(&L.list, x, y)) {
        int idx = du->top + (y - (L.list.y0 + 2)) / DU_ROW_H;
        if (idx >= 0 && idx < du->count) { du_pick(win, du, idx, L.visible); }
        return CTRUE;
    }

    if (crect_contains(&L.map, x, y)) {
        i = map_hit(du->cell, du->count, x, y);
        if (i >= 0) { du_pick(win, du, i, L.visible); }
        return CTRUE;
    }
    return CFALSE;
}

static cbool du_move(WmWindow *win, DiskUse *du, int x, int y)
{
    DuLayout L;
    int was = du->hot, now = -1;

    du_layout(win, &L);
    if (ui_hot_move(&du->hot_tb,
                    crect_contains(&L.up, x, y) ? 0
                  : crect_contains(&L.rescan, x, y) ? 1 : -1)) {
        CRect br[2];
        br[0] = L.up; br[1] = L.rescan;
        ui_hot_repaint(win, &du->hot_tb, br, 2);
    }
    if (crect_contains(&L.map, x, y)) {
        now = map_hit(du->cell, du->count, x, y);
    } else if (crect_contains(&L.list, x, y)) {
        int idx = du->top + (y - (L.list.y0 + 2)) / DU_ROW_H;
        if (idx >= 0 && idx < du->count) { now = idx; }
    }
    if (now != was) {
        du->hot = now;
        /* Repaint only when the block under the cursor actually changed --
         * a mouse move is not by itself news. */
        wm_invalidate(win, NULL);
        return CTRUE;
    }
    return CFALSE;
}

static cbool du_key(WmWindow *win, DiskUse *du, int key, int ch)
{
    DuLayout L;
    du_layout(win, &L);
    switch (key) {
    case PLAT_KEY_UP:
        if (du->sel > 0) { du->sel--; }
        else if (du->sel < 0 && du->count > 0) { du->sel = 0; }
        break;
    case PLAT_KEY_DOWN:
        if (du->sel < du->count - 1) { du->sel++; }
        break;
    case PLAT_KEY_HOME:  du->sel = (du->count > 0) ? 0 : -1; break;
    case PLAT_KEY_END:   du->sel = du->count - 1; break;
    case PLAT_KEY_PGUP:
        du->sel -= L.visible;
        if (du->sel < 0) { du->sel = (du->count > 0) ? 0 : -1; }
        break;
    case PLAT_KEY_PGDN:
        du->sel += L.visible;
        if (du->sel > du->count - 1) { du->sel = du->count - 1; }
        break;
    case PLAT_KEY_ENTER: return du_enter(win, du);
    case PLAT_KEY_BACKSP:    return du_up(win, du);
    case PLAT_KEY_F2:
        du_scan(du, du->path); wm_invalidate(win, NULL); return CTRUE;
    case PLAT_KEY_ESC: wm_destroy(win); return CTRUE;
    default:
        if (ch == 'r' || ch == 'R') {
            du_scan(du, du->path); wm_invalidate(win, NULL); return CTRUE;
        }
        return CFALSE;
    }
    du_scroll_to_sel(du, L.visible);
    wm_invalidate(win, NULL);
    return CTRUE;
}

/* ---- window ----------------------------------------------------------- */
static cbool diskuse_proc(WmWindow *win, WmMessage msg, long a, long b,
                          void *param)
{
    DiskUse *du = (DiskUse *)wm_user(win);
    switch (msg) {
    case WM_MSG_PAINT:       du_paint(win, (GfxSurface *)param); return CTRUE;
    case WM_MSG_LBUTTONDOWN: return du_click(win, du, (int)a, (int)b);
    case WM_MSG_MOUSEMOVE:   return du_move(win, du, (int)a, (int)b);
    case WM_MSG_LBUTTONUP:
    case WM_MSG_MOUSELEAVE: {
        cbool redraw;
        if (du == NULL) { return CFALSE; }
        redraw = ui_hot_release(&du->hot_tb);
        if (msg == WM_MSG_MOUSELEAVE && ui_hot_move(&du->hot_tb, -1)) {
            redraw = CTRUE;
        }
        if (redraw) {
            DuLayout L;
            CRect br[2];
            du_layout(win, &L);
            br[0] = L.up; br[1] = L.rescan;
            ui_hot_repaint(win, &du->hot_tb, br, 2);
        }
        return CTRUE;
    }
    case WM_MSG_MOUSEWHEEL: {
        DuLayout L;
        if (du == NULL) { return CFALSE; }
        du_layout(win, &L);
        if (ui_scroll_wheel(&du->top, (int)a, du->count, L.visible)) {
            CPoint o = wm_client_origin(win);
            CRect r = crect_offset(&L.list, o.x, o.y);
            wm_invalidate(win, &r);
        }
        return CTRUE;
    }
    case WM_MSG_KEYDOWN:     return du_key(win, du, (int)a, (int)b);
    case WM_MSG_DESTROY:
        if (du != NULL) { sys_free(du, (cu32)sizeof(DiskUse)); }
        return CTRUE;
    default: return CFALSE;
    }
}

void app_diskuse_open_path(const char *dir)
{
    DiskUse *du;
    WmWindow *w;
    PlatVideoInfo vi;
    CRect frame;
    int cw = 560, ch = 350, fx, fy;

    du = (DiskUse *)sys_calloc(1, (cu32)sizeof(DiskUse));
    if (du == NULL) { SYS_LOGE("app", "diskuse: OOM"); return; }
    if (dir != NULL && dir[0] != '\0') {
        sys_strlcpy(du->root, dir, sizeof(du->root));
    } else {
        sys_strlcpy(du->root, sys_home(), sizeof(du->root));
    }
    du->sel = -1;
    du->hot = -1;
    du_scan(du, du->root);

    plat_video_info(&vi);
    if (cw > vi.width - 16) { cw = vi.width - 16; }
    if (ch > vi.height - 60) { ch = vi.height - 60; }
    fx = (vi.width - cw) / 2;
    fy = (vi.height - ch) / 2 - 12;
    if (fx < 0) { fx = 0; }
    if (fy < 0) { fy = 0; }
    frame = crect_make(fx, fy, cw, ch);

    w = wm_create("Disk Usage", &frame, WM_STYLE_APP, diskuse_proc, du);
    if (w == NULL) { sys_free(du, (cu32)sizeof(DiskUse)); return; }
    wm_show(w, CTRUE);
    wm_invalidate(w, NULL);
}

void app_diskuse_open(void)
{
    app_diskuse_open_path(NULL);
}
