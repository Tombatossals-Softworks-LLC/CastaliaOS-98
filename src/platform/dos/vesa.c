/*
 * vesa.c - VESA VBE 2.0 windowed video for the DOS backend.
 *
 * Strategy (compatibility first, per the Bible's real-hardware focus):
 *   - Query the VBE controller info (INT 10h AX=4F00h) and walk the reported
 *     mode list to find an exact width/height/bpp match.
 *   - Set the mode WITHOUT the linear-framebuffer bit and page the display
 *     through window A with the BIOS bank-switch call (AX=4F05h). Banked
 *     access works on essentially every VBE BIOS including S3 Trio3D-class
 *     parts; it is slower than an LFB but our dirty-rectangle repaint keeps
 *     per-frame traffic small.
 *   - Convert each pixel from the 32-bit XRGB back buffer to the hardware
 *     depth with gfx_pack_index_332()/gfx_pack_565().
 *
 * Linear-framebuffer fast path: when the chosen mode advertises a VBE2 LFB
 * (mode attribute bit 7) with a nonzero PhysBasePtr, we set the mode with the
 * LFB bit (0x4000) and DPMI-map the framebuffer's physical address, then
 * present with a straight per-scanline write and NO bank switching. If the LFB
 * set or the physical mapping fails we transparently fall back to the banked
 * window path, which works on essentially every VBE BIOS. The banked path is
 * never removed; the LFB path is a pure speed-up over the same converted
 * pixels.
 *
 * COMPILED ONLY under CASTALIA_DOS. Verified on QEMU/Bochs/real hardware per
 * docs/TESTING.md; it cannot run in the portable host build.
 */
#ifdef CASTALIA_DOS

#include "vesa.h"
#include "../../gfx/vbe_pick.h"
#include "../../gfx/vbe_bank.h"
#include "dos_dpmi.h"
#include "castalia/sys.h"

#include <i86.h>
#include <conio.h>
#include <string.h>

/* Offsets within VbeInfoBlock / ModeInfoBlock (VBE spec). */
#define VIB_VIDEOMODEPTR 0x0E   /* dword: real-mode far ptr to mode list   */
#define MIB_ATTR         0x00   /* word                                    */
#define MIB_WINGRAN      0x04   /* word, KB                                 */
#define MIB_WINSIZE      0x06   /* word, KB                                 */
#define MIB_WINASEG      0x08   /* word                                    */
#define MIB_BPSL         0x10   /* word, bytes per scan line               */
#define MIB_XRES         0x12   /* word                                    */
#define MIB_YRES         0x14   /* word                                    */
#define MIB_BPP          0x19   /* byte                                    */
#define MIB_PHYSBASE     0x28   /* dword, LFB physical base (PhysBasePtr)   */
#define MIB_ATTR_LFB     0x80   /* mode attribute bit 7: LFB available      */
#define VBE_MODE_LFB     0x4000 /* set-mode bit 14: use linear framebuffer  */

static cu16     g_buf_seg = 0;  /* DOS transfer buffer (real-mode segment) */
static cu16     g_buf_sel = 0;
static VesaMode g_mode;
static long     g_cur_bank = -1;

/* Cached copy of the BIOS mode list. It MUST be captured once at init: the
 * per-mode 0x4F01 query reuses the same transfer buffer and would otherwise
 * clobber the controller info (and, on BIOSes that place the list inside the
 * VbeInfoBlock, the list itself) before the fallback ladder rescans. */
static cu16 g_mode_list[VESA_MAX_MODES];
static int  g_mode_count = 0;

/* ---- little-endian reads from the DOS transfer buffer ---------------- */
static cu16 rd16(cu16 off)
{
    cu16 v;
    dpmi_copy_from_dos(&v, g_buf_seg, off, 2);
    return v;
}
static cu8 rd8(cu16 off)
{
    cu8 v;
    dpmi_copy_from_dos(&v, g_buf_seg, off, 1);
    return v;
}
static cu32 rd32(cu16 off)
{
    cu32 v;
    dpmi_copy_from_dos(&v, g_buf_seg, off, 4);
    return v;
}

CResult vesa_init(void)
{
    DpmiRegs r;
    char sig[5];

    memset(&g_mode, 0, sizeof(g_mode));
    g_cur_bank = -1;

    /* One 512-byte low buffer serves every VBE query. */
    if (dpmi_alloc_dos(32, &g_buf_seg, &g_buf_sel) != CE_OK) {
        return CE_NOMEM;
    }

    /* Ask for VBE2 info: write "VBE2" into the buffer first. */
    dpmi_copy_to_dos(g_buf_seg, 0, "VBE2", 4);
    memset(&r, 0, sizeof(r));
    r.eax = 0x4F00;
    r.es  = g_buf_seg;
    r.edi = 0;
    if (dpmi_int(0x10, &r) != CE_OK || (r.eax & 0xFFFF) != 0x004F) {
        SYS_LOGE("plat", "VESA: no VBE BIOS (ax=%04lX)", (unsigned long)r.eax);
        dpmi_free_dos(g_buf_sel);
        g_buf_seg = g_buf_sel = 0;
        return CE_UNSUPPORTED;
    }
    dpmi_copy_from_dos(sig, g_buf_seg, 0, 4);
    sig[4] = '\0';
    if (memcmp(sig, "VESA", 4) != 0) {
        SYS_LOGW("plat", "VESA: signature mismatch '%s'", sig);
    }
    SYS_LOGI("plat", "VESA: VBE present, signature '%s'", sig);

    /* Snapshot the mode list NOW, while the transfer buffer still holds the
     * controller info from 0x4F00. Every later 0x4F01 (get_mode_info) reuses
     * this buffer, so re-reading the list pointer on a fallback rescan would
     * return garbage -- the root cause of the boot failing when the preferred
     * mode is absent but a fallback exists. */
    {
        cu32 listptr = rd32(VIB_VIDEOMODEPTR);
        cu16 lseg = (cu16)(listptr >> 16);
        cu16 loff = (cu16)(listptr & 0xFFFF);
        long linear = ((long)lseg << 4) + loff;
        int  n = 0;
        while (n < VESA_MAX_MODES - 1) {
            cu16 m = *(volatile cu16 *)(linear + (long)n * 2);
            if (m == 0xFFFF) { break; }
            g_mode_list[n++] = m;
        }
        g_mode_list[n] = 0xFFFF;
        g_mode_count = n;
        SYS_LOGI("plat", "VESA: cached %d BIOS video modes", n);
    }
    return CE_OK;
}

void vesa_shutdown(void)
{
    union REGS r;
    /* Release the linear-framebuffer mapping before dropping the mode. */
    if (g_mode.linear && g_mode.lfb != NULL) {
        dpmi_unmap_physical(g_mode.lfb);
        g_mode.lfb = NULL;
        g_mode.linear = CFALSE;
    }
    /* Restore 80x25 text mode. */
    r.w.ax = 0x0003;
    int386(0x10, &r, &r);
    if (g_buf_sel != 0) {
        dpmi_free_dos(g_buf_sel);
        g_buf_seg = g_buf_sel = 0;
    }
    g_mode.active = CFALSE;
}

/* Fetch mode info for 'mode' into the transfer buffer. Returns CTRUE on ok. */
static cbool get_mode_info(cu16 mode)
{
    DpmiRegs r;
    memset(&r, 0, sizeof(r));
    r.eax = 0x4F01;
    r.ecx = mode;
    r.es  = g_buf_seg;
    r.edi = 0;
    if (dpmi_int(0x10, &r) != CE_OK) { return CFALSE; }
    return ((r.eax & 0xFFFF) == 0x004F) ? CTRUE : CFALSE;
}

/*
 * Walk the cached BIOS mode list, describe every entry, and let vbe_pick.c
 * choose. The walk (and the shared transfer buffer that get_mode_info reuses)
 * is the part that has to happen here; WHICH mode is best is a policy with
 * real consequences on odd hardware, so it lives in a pure module with tests
 * rather than in a loop nobody can run.
 *
 * The snapshot taken in vesa_init is iterated rather than the live BIOS list,
 * because get_mode_info overwrites the shared buffer on every call.
 */
int vesa_enum_modes(VbeMode *out, int max)
{
    int i, n = 0;
    if (out == NULL || max <= 0) { return 0; }
    for (i = 0; i < g_mode_count && n < max; i++) {
        cu16 mode = g_mode_list[i];
        cu16 attr;
        if (mode == 0xFFFF) { break; }
        if (!get_mode_info(mode)) { continue; }
        attr = rd16(MIB_ATTR);
        out[n].mode      = mode;
        out[n].w         = rd16(MIB_XRES);
        out[n].h         = rd16(MIB_YRES);
        out[n].bpp       = rd8(MIB_BPP);
        out[n].supported = ((attr & 0x01) != 0) ? CTRUE : CFALSE;
        out[n].graphics  = ((attr & 0x10) != 0) ? CTRUE : CFALSE;
        out[n].lfb       = (((attr & MIB_ATTR_LFB) != 0) &&
                            rd32(MIB_PHYSBASE) != 0) ? CTRUE : CFALSE;
        n++;
    }
    return n;
}

static cu16 find_mode(int w, int h, int bpp)
{
    /* static, not automatic: 256 entries is several kilobytes and the DOS
     * stack is not the place for it. Single-threaded and only touched here. */
    static VbeMode cand[VESA_MAX_MODES];
    int n, best;

    n = vesa_enum_modes(cand, VESA_MAX_MODES);
    best = vbe_pick(cand, n, w, h, bpp);
    if (best < 0) { return 0xFFFF; }
    if (cand[best].bpp != bpp) {
        SYS_LOGW("plat", "VESA: no %dx%dx%d; using %d bpp instead",
                 w, h, bpp, cand[best].bpp);
    }
    return cand[best].mode;
}

CResult vesa_set_mode(int w, int h, int bpp, VesaMode *out)
{
    cu16 mode = find_mode(w, h, bpp);
    DpmiRegs r;
    cu16 attr;
    cu32 physbase;
    cbool lfb_ok;

    if (mode == 0xFFFF) {
        SYS_LOGW("plat", "VESA: no mode for %dx%dx%d", w, h, bpp);
        return CE_UNSUPPORTED;
    }
    /* Re-fetch the chosen mode's info so we capture window + LFB parameters. */
    if (!get_mode_info(mode)) { return CE_FAIL; }

    attr     = rd16(MIB_ATTR);
    physbase = rd32(MIB_PHYSBASE);
    lfb_ok   = ((attr & MIB_ATTR_LFB) != 0) && (physbase != 0);

    g_mode.width             = w;
    g_mode.height            = h;
    g_mode.bpp               = bpp;
    g_mode.bytes_per_pixel   = (bpp + 7) / 8;
    g_mode.bytes_per_scanline= rd16(MIB_BPSL);
    g_mode.win_seg           = rd16(MIB_WINASEG);
    g_mode.win_granularity   = (cu32)rd16(MIB_WINGRAN) * 1024UL;
    g_mode.mode_number       = mode;
    g_mode.linear            = CFALSE;
    g_mode.lfb               = NULL;
    g_mode.phys_base         = physbase;
    if (g_mode.win_seg == 0) { g_mode.win_seg = 0xA000; }
    if (g_mode.win_granularity == 0) { g_mode.win_granularity = 65536UL; }

    /* Preferred: set the mode with the LFB bit and map the framebuffer. */
    if (lfb_ok) {
        memset(&r, 0, sizeof(r));
        r.eax = 0x4F02;
        r.ebx = (cu32)(mode & 0x7FFF) | VBE_MODE_LFB;
        if (dpmi_int(0x10, &r) == CE_OK && (r.eax & 0xFFFF) == 0x004F) {
            cu32 fbsize = (cu32)g_mode.bytes_per_scanline * (cu32)h;
            void *p = dpmi_map_physical(physbase, fbsize);
            if (p != NULL) {
                g_mode.linear = CTRUE;
                g_mode.lfb    = (unsigned char *)p;
                g_mode.active = CTRUE;
                g_cur_bank    = -1;
                if (bpp == 8) { vesa_set_palette_332(); }
                if (out) { *out = g_mode; }
                SYS_LOGI("plat", "VESA: mode %04X %dx%dx%d LFB @%08lX bpsl=%u",
                         mode, w, h, bpp, (unsigned long)physbase,
                         g_mode.bytes_per_scanline);
                return CE_OK;
            }
            SYS_LOGW("plat", "VESA: LFB map failed; using banked window");
        } else {
            SYS_LOGW("plat", "VESA: LFB set failed; using banked window");
        }
    }

    /* Fallback: banked window (do NOT set bit 14). Works on every VBE BIOS. */
    memset(&r, 0, sizeof(r));
    r.eax = 0x4F02;
    r.ebx = mode & 0x7FFF;
    if (dpmi_int(0x10, &r) != CE_OK || (r.eax & 0xFFFF) != 0x004F) {
        SYS_LOGE("plat", "VESA: set mode %04X failed", mode);
        return CE_FAIL;
    }
    g_mode.active = CTRUE;
    g_cur_bank = -1;
    if (bpp == 8) { vesa_set_palette_332(); }
    if (out) { *out = g_mode; }
    SYS_LOGI("plat", "VESA: mode %04X %dx%dx%d banked bpsl=%u gran=%lu set",
             mode, w, h, bpp, g_mode.bytes_per_scanline,
             (unsigned long)g_mode.win_granularity);
    return CE_OK;
}

static void set_bank(long bank)
{
    union REGS r;
    if (bank == g_cur_bank) { return; }
    r.w.ax = 0x4F05;
    r.w.bx = 0x0000;        /* window A, set position                     */
    r.w.dx = (unsigned)bank;
    int386(0x10, &r, &r);
    g_cur_bank = bank;
}

void vesa_set_palette_332(void)
{
    int i;
    CColor pal[256];
    gfx_palette_build_332(pal);
    /* Program the VGA DAC (ports 0x3C8/0x3C9), 6 bits per channel. */
    outp(0x3C8, 0);
    for (i = 0; i < 256; i++) {
        outp(0x3C9, (unsigned char)(GFX_R(pal[i]) >> 2));
        outp(0x3C9, (unsigned char)(GFX_G(pal[i]) >> 2));
        outp(0x3C9, (unsigned char)(GFX_B(pal[i]) >> 2));
    }
}

void vesa_present_rect(const GfxSurface *back, const CRect *r)
{
    CRect full;
    CRect c;
    int y;
    long win_base;

    if (!g_mode.active) { return; }
    full = crect_make(0, 0, g_mode.width, g_mode.height);
    c = crect_intersect(&full, r);
    if (crect_empty(&c)) { return; }

    /* Linear fast path: straight per-scanline write, no bank switching. */
    if (g_mode.linear && g_mode.lfb != NULL) {
        for (y = c.y0; y < c.y1; y++) {
            const CColor *srow = back->pixels + (long)y * back->pitch;
            unsigned char *drow = g_mode.lfb
                                + (long)y * g_mode.bytes_per_scanline
                                + (long)c.x0 * g_mode.bytes_per_pixel;
            int x;
            if (g_mode.bytes_per_pixel == 1) {
                for (x = c.x0; x < c.x1; x++) {
                    *drow++ = gfx_pack_index_332(srow[x]);
                }
            } else {
                for (x = c.x0; x < c.x1; x++) {
                    cu16 px = gfx_pack_565(srow[x]);
                    *drow++ = (unsigned char)(px & 0xFF);
                    *drow++ = (unsigned char)(px >> 8);
                }
            }
        }
        return;
    }

    win_base = (long)g_mode.win_seg << 4;

    /*
     * The bank and the offset within the window are CARRIED along the row
     * rather than recomputed. They used to be a divide and a multiply per
     * pixel -- 480,000 of each for one 800x600 present, every frame, on the
     * path taken by exactly the cards that can least afford it. The write
     * position advances by a fixed number of bytes per pixel, so an add and a
     * compare give the same answer; vbe_bank.c is that carry, and
     * tests/test_vbe.c checks it lands where the divide did over a full
     * frame's worth of offsets at five different granularities.
     *
     * set_bank still returns immediately when the bank has not moved, so the
     * BIOS is called only on a real boundary crossing, as before.
     */
    for (y = c.y0; y < c.y1; y++) {
        const CColor *srow = back->pixels + (long)y * back->pitch;
        long line_off = (long)y * g_mode.bytes_per_scanline
                      + (long)c.x0 * g_mode.bytes_per_pixel;
        VbeBank w;
        int x;
        vbe_bank_seek(&w, (long)g_mode.win_granularity, line_off);
        for (x = c.x0; x < c.x1; x++) {
            volatile unsigned char *vp;
            set_bank(w.bank);
            vp = (volatile unsigned char *)(win_base + w.local);
            if (g_mode.bytes_per_pixel == 1) {
                *vp = gfx_pack_index_332(srow[x]);
            } else {
                cu16 px = gfx_pack_565(srow[x]);
                vp[0] = (unsigned char)(px & 0xFF);
                /* If the high byte would cross a bank boundary, re-page. */
                if (w.local + 1 >= (long)g_mode.win_granularity) {
                    set_bank(w.bank + 1);
                    vp = (volatile unsigned char *)(win_base + 0);
                    vp[0] = (unsigned char)(px >> 8);
                } else {
                    vp[1] = (unsigned char)(px >> 8);
                }
            }
            vbe_bank_step(&w, (long)g_mode.bytes_per_pixel);
        }
    }
}

#endif /* CASTALIA_DOS */
