/*
 * gfx.h - Layer 3 software rendering primitives.
 *
 * The entire UI is composited into a single back-buffer GfxSurface held in
 * system RAM in a canonical 32-bit XRGB format (0x00RRGGBB). This keeps all
 * drawing logic format-independent and dither-free. The platform present()
 * step is the ONLY place that converts the dirty region down to the real
 * hardware mode (8bpp palettized or 16bpp RGB565). Rationale: the back
 * buffer lives in the machine's 128 MB of RAM, so it does not compete with
 * the 4 MB of VRAM the visible framebuffer needs.
 *
 * All primitives clip to the surface's active clip rect. There is no
 * per-pixel alpha in v1; icons/cursors use a 1-bit mask (a magenta key
 * color) instead, matching the Bible's "avoid heavy alpha blending" rule.
 */
#ifndef CASTALIA_GFX_H
#define CASTALIA_GFX_H

#include "castalia/ctypes.h"
#include "castalia/rect.h"

/* ---- Colors ----------------------------------------------------------- */
#define GFX_RGB(r, g, b) \
    ((CColor)(((cu32)((r) & 0xFF) << 16) | \
              ((cu32)((g) & 0xFF) << 8)  | \
               (cu32)((b) & 0xFF)))
#define GFX_R(c) ((int)(((c) >> 16) & 0xFF))
#define GFX_G(c) ((int)(((c) >> 8)  & 0xFF))
#define GFX_B(c) ((int)( (c)        & 0xFF))

/* The transparency key used by masked blits (bright magenta, "CGA pink").
 * Any source pixel equal to this is not drawn. */
#define GFX_COLORKEY GFX_RGB(255, 0, 255)

/* ---- Surface ---------------------------------------------------------- */
typedef struct {
    int     w, h;       /* dimensions in pixels                           */
    int     pitch;      /* pixels per row (>= w), for sub-surfaces        */
    CColor *pixels;     /* w*h (logical) XRGB pixels, row-major           */
    cbool   owns_pixels;/* whether gfx_surface_free() should free pixels  */
    CRect   clip;       /* active clip rectangle, in surface coordinates  */
} GfxSurface;

/* Allocate a surface with its own pixel buffer. Returns NULL on OOM. */
GfxSurface *gfx_surface_new(int w, int h);
/* Wrap caller-owned pixels (e.g. a platform framebuffer) without copying. */
void        gfx_surface_wrap(GfxSurface *s, CColor *pixels,
                             int w, int h, int pitch);
void        gfx_surface_free(GfxSurface *s);

/*
 * Clip management.
 *
 * gfx_set_clip REPLACES the clip (intersected with the surface bounds only).
 * That is what an authority wants -- the window manager setting up a window's
 * paint -- and it is the wrong thing for everybody else.
 *
 * gfx_clip_narrow adds a restriction to whatever is already in force and
 * returns the previous clip, so the caller can put it back. Anything drawing
 * INSIDE a window's paint wants this one: the manager's clip is the only
 * thing standing between an app and the window in front of it, and replacing
 * it throws that away. ui_draw_button replaced it for every label it drew,
 * so a repaint of a small region over a background window painted that
 * window's button TEXT across whatever was on top of it -- measured at 385
 * pixels of a Calculator's key labels scattered through an open Notepad,
 * from nothing more than moving the mouse.
 */
void  gfx_set_clip(GfxSurface *s, const CRect *r);
CRect gfx_clip_narrow(GfxSurface *s, const CRect *r);
void  gfx_reset_clip(GfxSurface *s);       /* clip = whole surface        */
CRect gfx_get_clip(const GfxSurface *s);

/* ---- Primitives (all clipped) ---------------------------------------- */
void gfx_clear(GfxSurface *s, CColor color);
void gfx_put_pixel(GfxSurface *s, int x, int y, CColor color);
CColor gfx_get_pixel(const GfxSurface *s, int x, int y);
void gfx_fill_rect(GfxSurface *s, const CRect *r, CColor color);
void gfx_hline(GfxSurface *s, int x, int y, int w, CColor color);
void gfx_vline(GfxSurface *s, int x, int y, int h, CColor color);

/*
 * A dotted one-pixel focus ring, every other pixel lit.
 *
 * This era drew keyboard focus as a dotted rectangle, and there is a
 * practical reason beyond period accuracy: a tinted fill or a solid outline
 * has to be a colour, and any colour that reads on the default theme
 * disappears on a high-contrast one. Alternating pixels of the TEXT colour
 * are visible against whatever the panel happens to be, including in 16
 * colours and in safe mode -- which is exactly when somebody is driving the
 * machine without a mouse.
 */
void gfx_focus_rect(GfxSurface *s, const CRect *r, CColor color);
/* 1px outline just inside the rectangle. */
void gfx_frame_rect(GfxSurface *s, const CRect *r, CColor color);
/* Bresenham line. */
void gfx_line(GfxSurface *s, int x0, int y0, int x1, int y1, CColor color);
/* Filled axis-aligned disc of radius 'r' centered at (cx,cy). Clipped. */
void gfx_fill_circle(GfxSurface *s, int cx, int cy, int r, CColor color);
/* Filled rectangle with rounded corners (radius clamped to half the smaller
 * side). A cheap way to get the softer XP/rounded panel look. Clipped. */
void gfx_fill_round_rect(GfxSurface *s, const CRect *r, int radius, CColor color);
/* Vertical top->bottom gradient fill (used by the desktop and title bars). */
void gfx_vgradient(GfxSurface *s, const CRect *r, CColor top, CColor bottom);
/* Two-segment vertical gradient: 'top' at the top edge, 'mid' at mid_permille
 * of the height (0..1000), 'bottom' at the bottom edge. The bright mid band is
 * what gives XP-style title bars and the taskbar their glossy sheen. */
void gfx_vgradient3(GfxSurface *s, const CRect *r, CColor top, CColor mid,
                    CColor bottom, int mid_permille);

/* Blend 'c' toward 'target' by amt/256 (0 = c unchanged, 256 = target). A cheap
 * way to derive highlight/shadow tints from a base color at paint time. */
CColor gfx_tint(CColor c, CColor target, int amt);

/*
 * Every channel scaled by f/256 (0 = black, 256 = unchanged), truncated.
 *
 * Two multiplies instead of three: red and blue share one, because they sit at
 * bits 16 and 0 with a byte of space between them, so (c & 0x00FF00FF) times f
 * cannot carry out of blue into red -- the largest blue reaches is 0xFF00, one
 * bit short of bit 16 -- and the top lands at 0xFF00FF00, one bit short of
 * overflowing 32. Masking after the shift discards the low bits each channel
 * borrowed from the other.
 *
 * The MACRO is for per-pixel loops. It exists because the desktop's vignette
 * is a pass over every pixel of the screen on every boot, and calling across a
 * translation unit for each of them costs more than the arithmetic does -- it
 * measured 6.4M instructions of an 800x600 bake, which is twice what the
 * cleverness saves. It evaluates both arguments twice and does NOT range-check
 * f: pass 0..256, and pass a variable rather than an expression with effects.
 *
 * The FUNCTION is the same expression with f clamped, for everywhere that is
 * not a hot loop -- and it is what tests/test_color.c drives, so the macro's
 * arithmetic is exhaustively checked through it rather than trusted.
 */
#define GFX_SCALE_RGB(c, f) \
    ((CColor)(((((cu32)(c) & 0x00FF00FFu) * (cu32)(f) >> 8) & 0x00FF00FFu) | \
              ((((cu32)(c) & 0x0000FF00u) * (cu32)(f) >> 8) & 0x0000FF00u)))

CColor gfx_scale_rgb(CColor c, int f);

/* Darken an L-shaped strip along the right + bottom edges of 'r' (outside it),
 * 'size' px deep, fading from amt/256 at the frame edge to ~0 at the outer rim
 * -- a soft drop shadow with no alpha buffer (read-modify-write on the strips
 * only, so it stays inside the dirty-rect budget). Clipped like everything. */
void gfx_drop_shadow(GfxSurface *s, const CRect *r, int size, int amt);

/* ---- Win9x-style bevels (the visual grammar of the whole shell) ------ */
typedef enum {
    GFX_BEVEL_RAISED,   /* buttons, panels: light TL, dark BR             */
    GFX_BEVEL_SUNKEN,   /* text fields, wells: dark TL, light BR          */
    GFX_BEVEL_RAISED_THIN,
    GFX_BEVEL_SUNKEN_THIN,
    GFX_BEVEL_ETCHED    /* group separators                               */
} GfxBevel;

/* Draw a beveled border inside 'r' using the classic two-tone edges. The
 * 'face' fill is optional (pass GFX_NO_FILL to leave the interior alone). */
#define GFX_NO_FILL ((CColor)0xFF000000UL)  /* sentinel, never a real XRGB */
void gfx_bevel(GfxSurface *s, const CRect *r, GfxBevel style,
               CColor light, CColor dark, CColor face);

/* ---- Blitting --------------------------------------------------------- */
typedef enum {
    GFX_BLIT_COPY,    /* straight copy                                    */
    GFX_BLIT_KEYED    /* skip pixels equal to GFX_COLORKEY                */
} GfxBlitMode;

/* Copy 'src_r' from src to (dx,dy) in dst, clipped to dst's clip rect. */
void gfx_blit(GfxSurface *dst, int dx, int dy,
              const GfxSurface *src, const CRect *src_r, GfxBlitMode mode);

/* ---- BMP images (used by Paint + the image viewer) ------------------- */
/*
 * Pure in-memory codec (no file I/O, host-tested): encode writes a 24-bit
 * bottom-up BMP into 'out'; decode reads a 24/32/8-bit uncompressed BMP into a
 * newly allocated surface (caller frees with gfx_surface_free).
 */
/* Bytes a 24-bit BMP of a w*h surface needs (header + padded rows). */
cu32 gfx_bmp_encoded_size(int w, int h);
/* Encode 's' as a 24-bit BMP into 'out' (>= gfx_bmp_encoded_size). Returns the
 * byte count, or a negative CResult on bad args / too-small buffer. */
int  gfx_bmp_encode(const GfxSurface *s, unsigned char *out, cu32 cap);
/*
 * The same format, one piece at a time, so a whole 800x600 image does not have
 * to be held in memory to be written to disk -- which on a four-megabyte
 * machine is the difference between saving a screenshot and failing to.
 * gfx_bmp_save() streams with these; gfx_bmp_encode() is built on them too, so
 * there is one place that knows what a BMP looks like.
 *
 * Each returns the bytes written, or 0 if the arguments or the capacity are
 * wrong. Rows are written bottom-up: y = h-1 first, y = 0 last.
 */
cu32 gfx_bmp_row_stride(int w);
cu32 gfx_bmp_write_header(const GfxSurface *s, unsigned char *out, cu32 cap);
cu32 gfx_bmp_write_row(const GfxSurface *s, int y, unsigned char *out, cu32 cap);
/* Decode a BMP image in memory into a new surface, or NULL if unsupported. */
GfxSurface *gfx_bmp_decode(const void *data, cu32 len);

/* File wrappers over the codec, using the platform file API (host + DOS). */
CResult     gfx_bmp_save(const GfxSurface *s, const char *path);
GfxSurface *gfx_bmp_load(const char *path);

/* ---- Fonts (original bitmap faces, see gfx_font_data.c) -------------- */
typedef enum {
    GFX_FONT_SYSTEM = 0,  /* 8x8 original system face, 6px advance        */
    GFX_FONT_BOLD   = 1,  /* same face with synthesized heavier stems     */
    GFX_FONT_ITALIC = 2,  /* same face sheared right toward the top       */
    GFX_FONT_BOLDITALIC = 3,
    GFX_FONT_COUNT
} GfxFontId;

/* Selectable physical face for the system text. All faces share the same 8-row
 * cell and 6px advance, so switching faces changes no layout metric. The
 * original hand-authored 8x8 is the default; GFX_FACE_SPLEEN is the bundled
 * BSD-licensed Spleen 5x8 (see gfx_font_spleen.c / THIRD_PARTY_NOTICES.md).
 * Chosen at startup from [Assets] Font=. Affects GFX_FONT_SYSTEM and the
 * synthesized GFX_FONT_BOLD alike. */
enum { GFX_FACE_SYSTEM = 0, GFX_FACE_SPLEEN = 1, GFX_FACE_COUNT };
void gfx_font_select(int face);

int  gfx_font_height(GfxFontId font);

/*
 * The range of character codes this face actually has glyphs for.
 *
 * Everything outside it draws as a hollow box, deliberately, so a missing
 * glyph is visible rather than silent -- which is right for text and wrong
 * for anything trying to SHOW the user what is available. The Character Map
 * used to run 32..255 and spent 129 of its 224 cells drawing that same box,
 * offering characters this system cannot render as though it could.
 *
 * Asking the font means a face with wider coverage would widen the map on its
 * own, rather than needing a second constant somewhere else to be remembered.
 */
void gfx_font_range(GfxFontId font, int *first, int *last);
int  gfx_text_width(GfxFontId font, const char *text);

/*
 * Copy 'text' into 'dst', shortened from the RIGHT and marked "..." when it
 * needs more than 'width' pixels in 'font'.
 *
 * Here rather than in ui.h because it needs only gfx_text_width, and because
 * the window manager needs it for window titles -- src/wm has never depended
 * on the controls library and should not start. ui_text_fit forwards to this.
 */
void gfx_text_fit(char *dst, cu32 dstsz, const char *text, int width,
                  GfxFontId font);
/* Draw NUL-terminated ASCII text; unknown glyphs render as a box. */
void gfx_draw_text(GfxSurface *s, GfxFontId font, int x, int y,
                   const char *text, CColor color);
/* Draw text with a 1px drop shadow (used for title bars). */
void gfx_draw_text_shadow(GfxSurface *s, GfxFontId font, int x, int y,
                          const char *text, CColor color, CColor shadow);

/* Text alignment flags for gfx_draw_text_rect. */
#define GFX_ALIGN_LEFT    0x00
#define GFX_ALIGN_HCENTER 0x01
#define GFX_ALIGN_RIGHT   0x02
#define GFX_ALIGN_TOP     0x00
#define GFX_ALIGN_VCENTER 0x10
#define GFX_ALIGN_BOTTOM  0x20
void gfx_draw_text_rect(GfxSurface *s, GfxFontId font, const CRect *r,
                        const char *text, CColor color, int align_flags);

/* ---- Hardware pixel packing (used by the platform present() step) ----- */
/*
 * The UI is composited in 32-bit XRGB; the platform layer converts to the
 * real hardware depth here. These helpers are format kernels only -- they
 * touch no hardware and are host-testable.
 */

/* Fill 'pal' with a standard 3-3-2 (R:3 G:3 B:2) 256-entry palette. */
void gfx_palette_build_332(CColor pal[256]);
/* Map an XRGB color to its nearest 3-3-2 palette index (no search needed). */
cu8  gfx_pack_index_332(CColor c);
/* Pack an XRGB color into 16-bit RGB565. */
cu16 gfx_pack_565(CColor c);

/* ---- Low-color theme pipeline (16 / 256 color export + preview) ------- */
/*
 * The real-time 8bpp present path uses the search-free 3-3-2 packer above (the
 * P2 frame budget rules out a per-pixel palette search). These kernels are the
 * OFFLINE half of the color pipeline: quantizing theme colors and icon/asset
 * exports down to a 16-color or 256-color target for preview and for themes
 * that deliberately target the classic 16-color look.
 */

/* Fill 'pal' with the standard 16-color VGA/EGA palette (black..bright white). */
void gfx_palette_build_ega16(CColor pal[16]);

/* Nearest palette index by squared RGB distance. General (works for the 16
 * EGA colors or any irregular 256-entry palette); O(count) per call. */
cu8  gfx_pack_index_nearest(CColor c, const CColor *pal, int count);

/* Convenience: nearest of the 16 EGA colors. */
cu8  gfx_pack_index_ega16(CColor c);

/*
 * Quantize an array of XRGB colors IN PLACE to how they render on a target of
 * 'target_colors' colors: 16 uses the EGA palette (nearest match), anything
 * else uses the 3-3-2 256-color palette (matching the 8bpp present). Each
 * color is replaced by the representable color it maps to -- so callers can
 * preview a theme or bake a low-color asset. Colors already representable are
 * unchanged.
 */
void gfx_quantize_colors(CColor *colors, int count, int target_colors);

/*
 * Nearest-neighbour column map: cols[i] is the source column that output
 * column i samples, for a scale from 'src_w' to 'out_w'.
 *
 * Every nearest-neighbour scaler in this system computed this inside its
 * inner loop -- the wallpaper stretch, the image viewer, the Control Center's
 * preview and pc_scale -- which is a divide per PIXEL for a value that only
 * depends on the column. The wallpaper stretch alone did 480,000 of them per
 * bake where 800 would do. A divide is one instruction and twenty to forty
 * cycles on the machines this targets.
 *
 * The arithmetic is exactly what those loops did, so every one of them draws
 * the same picture as before. A source width below 1 maps everything to
 * column 0 rather than dividing by zero.
 */
void gfx_scale_map(int *cols, int out_w, int src_w);

#endif /* CASTALIA_GFX_H */
