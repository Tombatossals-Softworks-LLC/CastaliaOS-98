/*
 * sh_iconpack.c - Optional icon-pack loader (named, load-on-demand, cached).
 *
 * The shell draws its icons procedurally by default, keeping the base image
 * original and asset-free (LEGAL.md / THIRD_PARTY_NOTICES.md). Point
 * [Assets] Icons= at a directory of BMPs and the desktop, taskbar Quick Launch,
 * and File Manager toolbar request icons from it BY NAME (e.g. "computer",
 * "tb-copy", "ql-notepad") -- see assets/icons/README.md for the slot names.
 *
 * Icons load lazily on first paint and are cached for the session (a missing
 * file is cached as NULL, so a slot the pack doesn't provide is stat'd once and
 * then falls back to the caller's procedural/text drawing every frame -- no
 * per-frame disk hits, no reload storm). Transparent pixels are the magenta key
 * (GFX_COLORKEY = 255,0,255); the callers blit keyed, so there is no alpha
 * channel -- matching the renderer's 1-bit-mask rule.
 *
 * A pack can be authored two ways: the original MIT "Castalia" set baked by
 * tools/gen_iconpack.c, or a freely-redistributable CC0/MIT/public-domain
 * pixel-art set converted with tools/png2bmp.py (record it in
 * THIRD_PARTY_NOTICES.md before shipping).
 */
#include "sh_internal.h"
#include "castalia/gfx.h"
#include "castalia/sys.h"

#include <string.h>

#ifdef CASTALIA_DOS
#define SH_ICON_PATHSEP '\\'
#else
#define SH_ICON_PATHSEP '/'
#endif

#define ICONPACK_MAX  48   /* distinct icon names cached per session */
#define ICONPACK_NAME 24

typedef struct {
    char        name[ICONPACK_NAME];
    GfxSurface *surf;          /* NULL = tried and absent (negative cache) */
} IconEnt;

static char    g_dir[CASTALIA_MAX_PATH];
static IconEnt g_cache[ICONPACK_MAX];
static int     g_count;

void sh_iconpack_free(void)
{
    int i;
    for (i = 0; i < g_count; i++) {
        if (g_cache[i].surf != NULL) {
            gfx_surface_free(g_cache[i].surf);
            g_cache[i].surf = NULL;
        }
    }
    g_count = 0;
}

void sh_iconpack_set_dir(const char *dir)
{
    if (dir == NULL) { dir = ""; }
    /* Unchanged dir: keep the cache. Callers (desktop, launcher, apps) hold the
     * cached GfxSurface pointers across a re-init (e.g. SH_CMD_ARRANGE), so we
     * must not free them out from under those references when nothing changed. */
    if (strcmp(dir, g_dir) == 0) { return; }
    sh_iconpack_free();                       /* different pack -> drop the cache */
    sys_strlcpy(g_dir, dir, sizeof(g_dir));
}

/* Low-color tier: map an icon's colors onto the active theme palette (16 = EGA,
 * 256 = 3-3-2) so a low-color theme's icons read as one set instead of being
 * approximated at present time. The magenta key is left untouched so keyed
 * transparency survives. Truecolor themes (target 0) skip this. */
static void quantize_icon(GfxSurface *s, int target)
{
    CColor pal16[16];
    CColor pal256[256];
    int y, x;
    if (s == NULL || (target != 16 && target != 256)) { return; }
    if (target == 16) { gfx_palette_build_ega16(pal16); }
    else              { gfx_palette_build_332(pal256); }
    for (y = 0; y < s->h; y++) {
        CColor *row = s->pixels + (long)y * s->pitch;
        for (x = 0; x < s->w; x++) {
            if (row[x] == GFX_COLORKEY) { continue; }   /* keep transparency */
            row[x] = (target == 16) ? pal16[gfx_pack_index_ega16(row[x])]
                                    : pal256[gfx_pack_index_332(row[x])];
        }
    }
}

static GfxSurface *load_icon(const char *name)
{
    char path[CASTALIA_MAX_PATH];
    cu32 len;
    GfxSurface *s;
    sys_strlcpy(path, g_dir, sizeof(path));
    len = (cu32)strlen(path);
    if (len > 0u && path[len - 1] != '/' && path[len - 1] != '\\' &&
        len + 1u < (cu32)sizeof(path)) {
        path[len] = SH_ICON_PATHSEP;
        path[len + 1u] = '\0';
    }
    sys_strlcat(path, name, sizeof(path));
    sys_strlcat(path, ".bmp", sizeof(path));
    s = gfx_bmp_load(path);
    quantize_icon(s, g_sh.theme.colors);   /* no-op for truecolor themes */
    return s;
}

const GfxSurface *sh_iconpack_icon(const char *name)
{
    int i;
    if (g_dir[0] == '\0' || name == NULL) { return NULL; }

    for (i = 0; i < g_count; i++) {
        if (strcmp(g_cache[i].name, name) == 0) { return g_cache[i].surf; }
    }
    if (g_count >= ICONPACK_MAX) { return NULL; }   /* cache full: no reload storm */

    g_cache[g_count].surf = load_icon(name);        /* may be NULL (cached) */
    sys_strlcpy(g_cache[g_count].name, name, sizeof(g_cache[g_count].name));
    g_count++;
    return g_cache[g_count - 1].surf;
}
