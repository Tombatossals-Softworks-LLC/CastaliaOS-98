/*
 * test_bmp.c - Host unit tests for the pure BMP codec (gfx_bmp.c):
 * encode -> decode round trip, header, size, and rejection of junk.
 */
#include "ctest.h"
#include "castalia/gfx.h"

/* Read a little-endian field out of an encoded header. Written here rather
 * than borrowed from the codec on purpose: a check that reads a field back
 * with the same function that wrote it is checking nothing. */
static unsigned long rd32(const unsigned char *p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8)
         | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

void test_bmp(void)
{
    GfxSurface *s = gfx_surface_new(8, 4);
    unsigned char buf[512];
    cu32 need;
    int n, x, y, mism = 0;
    GfxSurface *d;

    CHECK(s != NULL);
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 8; x++) {
            s->pixels[y * s->pitch + x] = GFX_RGB(x * 16, y * 32, (x + y) * 8);
        }
    }
    s->pixels[1 * s->pitch + 2] = GFX_RGB(0x12, 0x34, 0x56);

    need = gfx_bmp_encoded_size(8, 4);
    CHECK_EQI(need, 54u + 24u * 4u);   /* row=24 (already 4-aligned), 4 rows */

    n = gfx_bmp_encode(s, buf, sizeof(buf));
    CHECK_EQI(n, (int)need);
    CHECK(buf[0] == 'B' && buf[1] == 'M');

    d = gfx_bmp_decode(buf, (cu32)n);
    CHECK(d != NULL);
    CHECK_EQI(d->w, 8);
    CHECK_EQI(d->h, 4);
    for (y = 0; y < 4; y++) {
        for (x = 0; x < 8; x++) {
            if ((s->pixels[y * s->pitch + x] & 0xFFFFFFUL) !=
                (d->pixels[y * d->pitch + x] & 0xFFFFFFUL)) { mism++; }
        }
    }
    CHECK_EQI(mism, 0);
    CHECK_EQI(d->pixels[1 * d->pitch + 2] & 0xFFFFFFUL, 0x123456UL);
    gfx_surface_free(d);

    /* rejections */
    CHECK(gfx_bmp_decode("XX", 2) == NULL);
    CHECK(gfx_bmp_decode((const void *)0, 0) == NULL);
    CHECK(gfx_bmp_encode(s, buf, 10) < 0);   /* buffer too small */

    /* ---- the streaming path -------------------------------------------- */
    /*
     * gfx_bmp_save() does not build the image in memory -- it writes a header
     * and then one row at a time, so saving an 800x600 screen costs 2400 bytes
     * of working memory instead of 1.4 MB, which on a four-megabyte machine is
     * the difference between saving a picture and failing to.
     *
     * gfx_bmp_encode() is built on the same two pieces, which is what keeps
     * the two paths from drifting -- but it also means comparing one against
     * the other proves almost nothing. The first version of this did exactly
     * that and could not fail: breaking a header field broke BOTH sides
     * identically and the comparison still matched. It is kept below for the
     * one thing it does catch (the two assembling the file in the same ORDER,
     * bottom-up, header first), and the field values are checked against
     * literal numbers instead.
     */
    {
        unsigned char streamed[512];
        cu32 stride = gfx_bmp_row_stride(s->w);
        cu32 at;
        int yy, diff = 0;

        CHECK_EQI((long)stride, 24);          /* 8 px * 3, already aligned */
        CHECK_EQI((long)gfx_bmp_row_stride(1), 4);   /* 3 bytes -> padded  */
        CHECK_EQI((long)gfx_bmp_row_stride(5), 16);  /* 15 -> 16           */
        CHECK_EQI((long)gfx_bmp_row_stride(0), 0);
        CHECK_EQI((long)gfx_bmp_row_stride(-4), 0);

        at = gfx_bmp_write_header(s, streamed, sizeof(streamed));
        CHECK_EQI((long)at, 54);
        for (yy = s->h - 1; yy >= 0; yy--) {   /* bottom-up, as BMP wants */
            CHECK_EQI((long)gfx_bmp_write_row(s, yy, streamed + at,
                                              (cu32)sizeof(streamed) - at),
                      (long)stride);
            at += stride;
        }
        CHECK_EQI((long)at, (long)need);
        for (x = 0; x < (int)at; x++) {
            if (streamed[x] != buf[x]) { diff++; }
        }
        CHECK_EQI(diff, 0);
    }

    /* ---- the header, against literal numbers --------------------------- */
    /*
     * Read straight out of the encoded bytes and compared with what the format
     * says must be there, not with what the writer computed. This is the check
     * that bites when a field is wrong, because nothing here asks the writer
     * what it thinks the answer is.
     */
    {
        unsigned char h2[54 + 12 * 2];
        GfxSurface *p = gfx_surface_new(3, 2);   /* 9 bytes/row -> 3 of pad */
        int k;
        CHECK(p != NULL);
        for (k = 0; k < 6; k++) {
            p->pixels[(k / 3) * p->pitch + (k % 3)] = GFX_RGB(k, k + 1, k + 2);
        }
        CHECK_EQI((long)gfx_bmp_row_stride(3), 12);
        CHECK_EQI(gfx_bmp_encode(p, h2, sizeof(h2)), 54 + 12 * 2);
        CHECK(h2[0] == 'B' && h2[1] == 'M');
        CHECK_EQI((long)rd32(h2 + 2),  54 + 12 * 2);  /* file size          */
        CHECK_EQI((long)rd32(h2 + 10), 54);           /* pixel data offset  */
        CHECK_EQI((long)rd32(h2 + 14), 40);           /* DIB header size    */
        CHECK_EQI((long)rd32(h2 + 18), 3);            /* width              */
        CHECK_EQI((long)rd32(h2 + 22), 2);            /* height             */
        CHECK_EQI((long)rd32(h2 + 26) & 0xFFFF, 1);   /* planes             */
        CHECK_EQI((long)rd32(h2 + 28) & 0xFFFF, 24);  /* bits per pixel     */
        CHECK_EQI((long)rd32(h2 + 30), 0);            /* BI_RGB, no codec   */
        CHECK_EQI((long)rd32(h2 + 34), 12 * 2);       /* image byte count   */
        /* The three pad bytes at the end of each row must be zero, and the
         * rows must be bottom-up: the LAST source row comes first. */
        for (k = 0; k < 2; k++) {
            CHECK_EQI(h2[54 + k * 12 + 9], 0);
            CHECK_EQI(h2[54 + k * 12 + 10], 0);
            CHECK_EQI(h2[54 + k * 12 + 11], 0);
        }
        CHECK_EQI(h2[54 + 2], GFX_R(p->pixels[1 * p->pitch + 0]));
        CHECK_EQI(h2[54 + 12 + 2], GFX_R(p->pixels[0 * p->pitch + 0]));
        /* ...and it survives its own decoder, padding and all. */
        {
            GfxSurface *back = gfx_bmp_decode(h2, 54 + 12 * 2);
            int bad = 0;
            CHECK(back != NULL);
            CHECK_EQI(back->w, 3);
            CHECK_EQI(back->h, 2);
            for (k = 0; k < 6; k++) {
                if ((back->pixels[(k / 3) * back->pitch + (k % 3)] & 0xFFFFFFUL)
                    != (p->pixels[(k / 3) * p->pitch + (k % 3)] & 0xFFFFFFUL)) {
                    bad++;
                }
            }
            CHECK_EQI(bad, 0);
            gfx_surface_free(back);
        }
        gfx_surface_free(p);
    }

    /* ...and both halves refuse rather than overrun. */
    CHECK_EQI((long)gfx_bmp_write_header(s, buf, 53u), 0);
    CHECK_EQI((long)gfx_bmp_write_header(NULL, buf, 64u), 0);
    CHECK_EQI((long)gfx_bmp_write_header(s, NULL, 64u), 0);
    CHECK_EQI((long)gfx_bmp_write_row(s, 0, buf, 23u), 0);   /* stride is 24 */
    CHECK_EQI((long)gfx_bmp_write_row(s, -1, buf, 64u), 0);
    CHECK_EQI((long)gfx_bmp_write_row(s, 4, buf, 64u), 0);   /* h is 4: 0..3 */
    CHECK_EQI((long)gfx_bmp_write_row(NULL, 0, buf, 64u), 0);
    CHECK_EQI((long)gfx_bmp_write_row(s, 0, NULL, 64u), 0);

    gfx_surface_free(s);
}
