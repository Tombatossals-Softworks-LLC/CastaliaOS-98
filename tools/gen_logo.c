/*
 * gen_logo.c - Bake the Castalia mark to BMPs at the sizes the repo needs.
 *
 * The artwork lives in ONE place (src/shell/sh_logo.c, which is also what the
 * running desktop draws), so the icons in the repo and the Start button can
 * never drift apart. A host tool: build with `make gen-logo`, which also
 * converts the BMPs to PNG.
 *
 * Usage: gen_logo <outdir>
 */
#include <stdio.h>
#include <string.h>
#include "castalia/gfx.h"

void sh_logo_draw(GfxSurface *s, int x, int y, int size);

/* The sizes the project actually uses: favicon tiers, app icon, press kit. */
static const int SIZES[] = { 16, 32, 48, 64, 128, 256 };
#define NSIZES ((int)(sizeof(SIZES) / sizeof(SIZES[0])))

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    int i, made = 0;

    for (i = 0; i < NSIZES; i++) {
        int d = SIZES[i];
        char path[512];
        GfxSurface *s = gfx_surface_new(d, d);
        if (s == NULL) { continue; }
        /* Magenta key: the PNG step turns it into transparency. */
        gfx_clear(s, GFX_COLORKEY);
        sh_logo_draw(s, 0, 0, d);
        sprintf(path, "%s/castalia-mark-%d.bmp", dir, d);
        if (gfx_bmp_save(s, path) == CE_OK) {
            printf("  LOGO  %s\n", path);
            made++;
        }
        gfx_surface_free(s);
    }
    printf("gen_logo: wrote %d mark(s)\n", made);
    return (made == NSIZES) ? 0 : 1;
}
