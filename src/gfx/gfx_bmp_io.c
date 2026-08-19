/*
 * gfx_bmp_io.c - File wrappers over the BMP codec (gfx_bmp.c).
 *
 * Split from the pure codec so the codec stays host-testable; these use the
 * platform file API + accounted memory and work on host and DOS alike.
 */
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#define BMP_LOAD_MAX (4UL * 1024UL * 1024UL)   /* refuse absurd files */

/*
 * Written a row at a time, not all at once.
 *
 * The whole-image version allocated gfx_bmp_encoded_size() up front, which for
 * the 800x600 screen this ships on is 1.4 MB -- more than a third of the
 * target machine's memory, asked for at the moment somebody presses Capture
 * Screen. A row is 2400 bytes. The file that comes out is byte for byte the
 * same one (tests/test_bmp.c checks that against gfx_bmp_encode), and the
 * saving costs one write per row instead of one per image.
 */
CResult gfx_bmp_save(const GfxSurface *s, const char *path)
{
    cu32 stride;
    unsigned char hdr[54];
    unsigned char *row;
    PlatFile *f;
    int y;
    CResult rc = CE_OK;

    if (s == NULL || path == NULL) { return CE_INVALID; }
    if (s->w <= 0 || s->h <= 0) { return CE_INVALID; }
    stride = gfx_bmp_row_stride(s->w);
    if (stride == 0u) { return CE_INVALID; }
    if (gfx_bmp_write_header(s, hdr, sizeof(hdr)) != 54u) { return CE_INVALID; }

    row = (unsigned char *)sys_alloc(stride);
    if (row == NULL) { return CE_NOMEM; }
    f = plat_fopen(path, "wb");
    if (f == NULL) { sys_free(row, stride); return CE_IO; }

    if (plat_fwrite(f, hdr, 54u) != 54u) { rc = CE_IO; }
    for (y = s->h - 1; rc == CE_OK && y >= 0; y--) {   /* BMP is bottom-up */
        if (gfx_bmp_write_row(s, y, row, stride) != stride) { rc = CE_INVALID; break; }
        if (plat_fwrite(f, row, stride) != stride) { rc = CE_IO; break; }
    }
    if (plat_fclose(f) != CE_OK) { rc = CE_IO; }
    sys_free(row, stride);
    /* A half-written bitmap is worse than none: it opens, and it is wrong. */
    if (rc != CE_OK) { plat_file_remove(path); }
    return rc;
}

GfxSurface *gfx_bmp_load(const char *path)
{
    long sz;
    void *buf;
    PlatFile *f;
    cu32 got;
    GfxSurface *s;

    if (path == NULL) { return NULL; }
    sz = plat_file_size(path);
    if (sz <= 0 || (cu32)sz > BMP_LOAD_MAX) { return NULL; }
    buf = sys_alloc((cu32)sz);
    if (buf == NULL) { return NULL; }
    f = plat_fopen(path, "rb");
    if (f == NULL) { sys_free(buf, (cu32)sz); return NULL; }
    got = plat_fread(f, buf, (cu32)sz);
    plat_fclose(f);
    s = (got == (cu32)sz) ? gfx_bmp_decode(buf, (cu32)sz) : NULL;
    sys_free(buf, (cu32)sz);
    return s;
}
