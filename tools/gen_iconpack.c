/*
 * gen_iconpack.c - Author the original "Castalia" desktop icon pack.
 *
 * A host-only tool (NOT part of the shipped product) that renders higher-
 * fidelity ORIGINAL desktop icons with the engine's own gfx primitives and
 * writes them as keyed 24-bit BMPs into an output directory. The shell loads
 * them at runtime via [Assets] Icons= (see src/shell/sh_iconpack.c); with no
 * pack configured the desktop keeps its built-in procedural art.
 *
 * Transparent pixels are the renderer's magenta key (GFX_COLORKEY = 255,0,255),
 * which survives the 24-bit BMP round trip exactly, so the keyed blit composites
 * each icon cleanly over the desktop -- no alpha channel needed.
 *
 * Usage:  gen_iconpack <output-dir>        (default: assets/icons/castalia)
 * Rebuild the committed pack with:  make gen-iconpack
 */
#include "castalia/gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ICON 40   /* icon canvas; fits the 40 px desktop grid (ICON_SZ) */

/* ---- individual icons (original silhouettes, drawn from primitives) ---- */

static void draw_computer(GfxSurface *s)
{
    CColor light = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor dark  = GFX_RGB(0x52, 0x5A, 0x66);
    CColor face  = GFX_RGB(0xD2, 0xD7, 0xDF);
    CColor gold  = GFX_RGB(0xE7, 0xC1, 0x5A);
    CRect body   = crect_make(4, 4, 32, 24);
    CRect screen = crect_make(7, 7, 26, 18);
    CRect inner  = crect_make(8, 8, 24, 16);
    CRect stand  = crect_make(18, 28, 4, 4);
    CRect base   = crect_make(11, 32, 18, 4);
    int i;
    /* monitor shell + sunken screen with a blue->navy gradient */
    gfx_bevel(s, &body, GFX_BEVEL_RAISED, light, dark, face);
    gfx_bevel(s, &screen, GFX_BEVEL_SUNKEN, light, dark, GFX_NO_FILL);
    gfx_vgradient(s, &inner, GFX_RGB(0x24, 0x66, 0x9C), GFX_RGB(0x12, 0x2C, 0x42));
    /* tiny gold castle on the screen: base + three merlons */
    {
        CRect keep = crect_make(15, 17, 10, 5);
        gfx_fill_rect(s, &keep, gold);
        for (i = 0; i < 3; i++) {
            CRect m = crect_make(15 + i * 4, 14, 2, 3);
            gfx_fill_rect(s, &m, gold);
        }
    }
    gfx_fill_rect(s, &stand, dark);
    gfx_bevel(s, &base, GFX_BEVEL_RAISED, light, dark, face);
}

static void draw_folder(GfxSurface *s)
{
    CColor edge  = GFX_RGB(0x93, 0x69, 0x1C);
    CColor manila_t = GFX_RGB(0xFB, 0xE2, 0x9C);
    CColor manila_b = GFX_RGB(0xE7, 0xBC, 0x54);
    CRect tab  = crect_make(5, 9, 15, 6);
    CRect body = crect_make(4, 13, 32, 21);
    CRect lip  = crect_make(4, 13, 32, 4);
    gfx_fill_rect(s, &tab, manila_b);
    gfx_frame_rect(s, &tab, edge);
    gfx_vgradient(s, &body, manila_t, manila_b);
    gfx_frame_rect(s, &body, edge);
    /* a lighter front lip for a hint of depth */
    gfx_vgradient(s, &lip, GFX_RGB(0xFF, 0xF0, 0xC4), manila_t);
    gfx_frame_rect(s, &lip, edge);
    gfx_hline(s, body.x0 + 2, body.y0 + 6, crect_w(&body) - 4, manila_t);
}

static void draw_settings(GfxSurface *s)
{
    CColor light = GFX_RGB(0xE8, 0xEC, 0xF2);
    CColor dark  = GFX_RGB(0x5C, 0x64, 0x70);
    CColor steel_t = GFX_RGB(0xC9, 0xCF, 0xDA);
    CColor steel_b = GFX_RGB(0x8C, 0x94, 0xA2);
    int cx = 20, cy = 20, i;
    CRect body = crect_make(cx - 12, cy - 12, 24, 24);
    CRect hub  = crect_make(cx - 4, cy - 4, 8, 8);
    /* four orthogonal teeth */
    CRect t_n = crect_make(cx - 4, cy - 17, 8, 6);
    CRect t_s = crect_make(cx - 4, cy + 11, 8, 6);
    CRect t_w = crect_make(cx - 17, cy - 4, 6, 8);
    CRect t_e = crect_make(cx + 11, cy - 4, 6, 8);
    gfx_fill_rect(s, &t_n, dark); gfx_fill_rect(s, &t_s, dark);
    gfx_fill_rect(s, &t_w, dark); gfx_fill_rect(s, &t_e, dark);
    /* four diagonal teeth (corner blocks) */
    for (i = 0; i < 4; i++) {
        int dx = (i & 1) ? 9 : -15;
        int dy = (i & 2) ? 9 : -15;
        CRect c = crect_make(cx + dx, cy + dy, 6, 6);
        gfx_fill_rect(s, &c, dark);
    }
    gfx_vgradient(s, &body, steel_t, steel_b);
    gfx_frame_rect(s, &body, dark);
    gfx_bevel(s, &hub, GFX_BEVEL_SUNKEN, light, dark, GFX_RGB(0x3C, 0x44, 0x50));
}

static void draw_document(GfxSurface *s)
{
    CColor edge  = GFX_RGB(0x77, 0x80, 0x8D);
    CColor paper = GFX_RGB(0xFB, 0xFC, 0xFE);
    CColor head_t = GFX_RGB(0x3E, 0x78, 0xC0);
    CColor head_b = GFX_RGB(0x27, 0x55, 0x94);
    CColor line  = GFX_RGB(0xA6, 0xAE, 0xBC);
    CRect body = crect_make(9, 3, 22, 34);
    CRect head = crect_make(9, 3, 22, 8);
    int i;
    gfx_fill_rect(s, &body, paper);
    gfx_vgradient(s, &head, head_t, head_b);
    gfx_frame_rect(s, &body, edge);
    /* folded top-right corner */
    gfx_line(s, body.x1 - 9, body.y0, body.x1 - 1, body.y0 + 8, edge);
    /* text lines */
    for (i = 0; i < 5; i++) {
        gfx_hline(s, body.x0 + 3, 15 + i * 4, crect_w(&body) - 6, line);
    }
}

static void draw_trash(GfxSurface *s)
{
    CColor dark  = GFX_RGB(0x51, 0x5A, 0x67);
    CColor metal_t = GFX_RGB(0xC4, 0xCB, 0xD6);
    CColor metal_b = GFX_RGB(0x8E, 0x97, 0xA6);
    CRect lid  = crect_make(9, 6, 22, 5);
    CRect knob = crect_make(17, 3, 6, 3);
    CRect bin  = crect_make(11, 11, 18, 25);
    int i;
    gfx_fill_rect(s, &knob, metal_b);
    gfx_fill_rect(s, &lid, metal_t);
    gfx_frame_rect(s, &lid, dark);
    gfx_vgradient(s, &bin, metal_t, metal_b);
    gfx_frame_rect(s, &bin, dark);
    /* vertical ridges */
    for (i = 0; i < 3; i++) {
        gfx_vline(s, bin.x0 + 5 + i * 4, bin.y0 + 3, crect_h(&bin) - 6, dark);
    }
}

/* ---- toolbar glyphs (16 px) -------------------------------------------- */
/*
 * The File Manager's toolbar asks the pack for one icon per button and falls
 * back to a wide TEXT button for any it does not get. Rename and 2-Pane had
 * no icon in any pack, so a toolbar of eight small square glyphs carried two
 * word-buttons in the middle of it -- not a fallback anyone chose, just the
 * two slots nobody filled.
 *
 * They are drawn here rather than taken from Tango because Tango has no
 * equivalent for either: its nearest "rename" glyph is the text-editor icon,
 * which the pack already uses for Notepad, and it has no split-view icon at
 * all. Original art, so the provenance stays clean either way.
 */
#define TBI 16

static void draw_tb_rename(GfxSurface *s)
{
    CColor ink   = GFX_RGB(0x2E, 0x34, 0x36);
    CColor paper = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor line  = GFX_RGB(0x9A, 0xA0, 0xA6);
    CColor wood  = GFX_RGB(0xF5, 0xC2, 0x11);
    CColor lead  = GFX_RGB(0x55, 0x57, 0x53);
    CRect page = crect_make(0, 0, 9, 11);
    int i;

    /* the thing being named */
    gfx_fill_rect(s, &page, paper);
    gfx_frame_rect(s, &page, ink);
    for (i = 0; i < 3; i++) { gfx_hline(s, 2, 3 + i * 2, 5, line); }

    /*
     * ...and the pencil over it, tip down-left, the way you hold one.
     *
     * The body is stacked VERTICALLY, one pixel at a time. Offsetting a
     * 45-degree line perpendicular to itself -- by (+1,+1) against a (1,-1)
     * direction, which is the geometrically obvious move -- steps x+y by TWO
     * each time, so the copies land only on even anti-diagonals and the band
     * comes out as a checkerboard. Offsetting straight down steps x+y by one
     * and the pixels actually meet.
     */
    for (i = 0; i < 4; i++) {
        gfx_line(s, 6, 12 + i, 12, 6 + i, wood);
    }
    gfx_line(s, 6, 11, 12, 5, ink);          /* the upper edge     */
    gfx_vline(s, 13, 5, 5, ink);             /* the ferrule end    */
    /* the sharpened tip: a short dark wedge closing the low end */
    gfx_line(s, 4, 14, 6, 12, lead);
    gfx_line(s, 5, 15, 6, 14, lead);
    gfx_line(s, 4, 15, 5, 14, ink);
    gfx_put_pixel(s, 4, 15, ink);
}

/*
 * Copy: two sheets, one behind the other.
 *
 * Tango's edit-copy is a single near-white page with no outline. On the
 * toolbar's glossy button -- which samples at (238,242,247), nearly white
 * itself -- 9% of its pixels clear a contrast of 60 against that face; every
 * other icon in the row manages 47% or more. It was a pale smudge in the
 * middle of eight legible glyphs. It also showed ONE page, which is not what
 * copying looks like.
 */
static void draw_tb_copy(GfxSurface *s)
{
    CColor ink   = GFX_RGB(0x2E, 0x34, 0x36);
    CColor paper = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor line  = GFX_RGB(0x9A, 0xA0, 0xA6);
    CRect back  = crect_make(1, 0, 9, 12);
    CRect front = crect_make(5, 3, 10, 13);
    int i;

    gfx_fill_rect(s, &back, paper);
    gfx_frame_rect(s, &back, ink);
    gfx_fill_rect(s, &front, paper);
    gfx_frame_rect(s, &front, ink);
    for (i = 0; i < 3; i++) { gfx_hline(s, 7, 6 + i * 3, 6, line); }
}

/*
 * Log Viewer: a page of dated, graded lines.
 *
 * Tango's text-x-generic is a white page with pale grey rules and no outline.
 * The Start menu paints its columns white and pale blue, so of that icon's 256
 * pixels FOUR stand out against the panel behind it -- the next faintest icon
 * in the menu manages 23 and the median is 104. It was not a faint icon, it
 * was a blank space with a label next to it.
 *
 * The severity dots are the point: a log is lines that are not all equal, and
 * four coloured squares carry at 16 px where four grey rules do not.
 */
static void draw_m_logview(GfxSurface *s)
{
    CColor ink   = GFX_RGB(0x2E, 0x34, 0x36);
    CColor paper = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor line  = GFX_RGB(0x88, 0x8A, 0x85);
    CColor lvl[4];
    CRect page = crect_make(1, 0, 13, 16);
    int i;

    lvl[0] = GFX_RGB(0x34, 0x65, 0xA4);   /* info    */
    lvl[1] = GFX_RGB(0x4E, 0x9A, 0x06);   /* ok      */
    lvl[2] = GFX_RGB(0xF5, 0x79, 0x00);   /* warning */
    lvl[3] = GFX_RGB(0xCC, 0x00, 0x00);   /* error   */

    gfx_fill_rect(s, &page, paper);
    gfx_frame_rect(s, &page, ink);
    for (i = 0; i < 4; i++) {
        CRect dot = crect_make(3, 2 + i * 3, 2, 2);
        gfx_fill_rect(s, &dot, lvl[i]);
        gfx_hline(s, 6, 3 + i * 3, 6, line);
    }
}

static void draw_tb_dual(GfxSurface *s)
{
    CColor ink   = GFX_RGB(0x2E, 0x34, 0x36);
    CColor left  = GFX_RGB(0x72, 0x9F, 0xCF);
    CColor right = GFX_RGB(0xFF, 0xFF, 0xFF);
    CColor bar   = GFX_RGB(0x34, 0x65, 0xA4);
    CRect frame = crect_make(1, 2, 14, 12);
    CRect lp    = crect_make(2, 3, 6, 10);
    CRect rp    = crect_make(9, 3, 5, 10);
    CRect lt    = crect_make(2, 3, 6, 2);
    CRect rt    = crect_make(9, 3, 5, 2);

    gfx_fill_rect(s, &lp, left);
    gfx_fill_rect(s, &rp, right);
    /* a title strip on each, so it reads as two PANES rather than a table */
    gfx_fill_rect(s, &lt, bar);
    gfx_fill_rect(s, &rt, bar);
    gfx_frame_rect(s, &frame, ink);
    gfx_vline(s, 8, 2, 12, ink);           /* the split itself */
}

/* ---- driver ----------------------------------------------------------- */

static int save_icon_sz(void (*draw)(GfxSurface *), const char *dir,
                        const char *name, int size)
{
    char path[512];
    GfxSurface *s;
    cu32 need;
    unsigned char *buf;
    int n;
    FILE *f;

    s = gfx_surface_new(size, size);
    if (s == NULL) { fprintf(stderr, "gen_iconpack: OOM\n"); return -1; }
    gfx_reset_clip(s);
    gfx_clear(s, GFX_COLORKEY);     /* transparent canvas */
    draw(s);

    need = gfx_bmp_encoded_size(s->w, s->h);
    buf = (unsigned char *)malloc(need);
    if (buf == NULL) { gfx_surface_free(s); return -1; }
    n = gfx_bmp_encode(s, buf, need);
    gfx_surface_free(s);
    if (n < 0) { free(buf); fprintf(stderr, "gen_iconpack: encode %s failed\n", name); return -1; }

    snprintf(path, sizeof(path), "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (f == NULL) { free(buf); fprintf(stderr, "gen_iconpack: cannot write %s\n", path); return -1; }
    fwrite(buf, 1, (size_t)n, f);
    fclose(f);
    free(buf);
    printf("  wrote %s (%d bytes)\n", path, n);
    return 0;
}

static int save_icon(void (*draw)(GfxSurface *), const char *dir, const char *name)
{
    return save_icon_sz(draw, dir, name, ICON);
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "assets/icons/castalia";
    int rc = 0;
    rc |= save_icon(draw_computer, dir, "computer.bmp");
    rc |= save_icon(draw_folder,   dir, "folder.bmp");
    rc |= save_icon(draw_settings, dir, "settings.bmp");
    rc |= save_icon(draw_document, dir, "document.bmp");
    rc |= save_icon(draw_trash,    dir, "trash.bmp");
    /* Toolbar glyphs, at the 16 px the File Manager blits them at. */
    rc |= save_icon_sz(draw_tb_rename, dir, "tb-rename.bmp", TBI);
    rc |= save_icon_sz(draw_tb_copy,   dir, "tb-copy.bmp",   TBI);
    rc |= save_icon_sz(draw_tb_dual,   dir, "tb-dual.bmp",   TBI);
    rc |= save_icon_sz(draw_m_logview, dir, "m-logview.bmp", TBI);
    return rc ? 1 : 0;
}
