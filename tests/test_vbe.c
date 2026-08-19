/*
 * test_vbe.c - VESA mode selection (vbe_pick.c).
 *
 * This is code that runs once, on hardware none of us has, at the moment
 * before there is a screen to report a problem on. It is therefore exactly the
 * code that most needs to be decided somewhere it CAN be tested, and the mode
 * lists below are the awkward ones real cards actually offer: 15bpp where you
 * expected 16, a banked mode listed before the linear one, a 24bpp mode the
 * presenter cannot write, and entries flagged unsupported that must be skipped
 * even though their numbers look right.
 */
#include "ctest.h"
#include "../src/gfx/vbe_pick.h"
#include "../src/gfx/vbe_report.h"
#include "../src/gfx/vbe_bank.h"

/* Build one entry: supported + graphics unless said otherwise. */
static VbeMode M(cu16 mode, int w, int h, int bpp, cbool lfb)
{
    VbeMode m;
    m.mode = mode; m.w = w; m.h = h; m.bpp = bpp;
    m.supported = CTRUE; m.graphics = CTRUE; m.lfb = lfb;
    return m;
}

void test_vbe(void)
{
    VbeMode list[10];
    int n, got;

    printf("- vesa mode selection\n");

    /* ---- what the presenter can actually drive ------------------------ */
    CHECK(vbe_depth_drivable(8));
    CHECK(vbe_depth_drivable(16));
    /* 15bpp is 5:5:5 and the presenter writes 5:6:5 -- every colour would be
     * wrong. 24 and 32 are wider than the two bytes it writes. */
    CHECK(!vbe_depth_drivable(15));
    CHECK(!vbe_depth_drivable(24));
    CHECK(!vbe_depth_drivable(32));
    CHECK(!vbe_depth_drivable(4));
    CHECK(!vbe_depth_drivable(0));

    /* ---- the ordinary case: exactly what was asked for ---------------- */
    n = 0;
    list[n++] = M(0x101, 640, 480,  8, CFALSE);
    list[n++] = M(0x114, 800, 600, 16, CTRUE);
    list[n++] = M(0x103, 800, 600,  8, CFALSE);
    got = vbe_pick(list, n, 800, 600, 16);
    CHECK_EQI(got, 1);
    CHECK_EQI((int)list[got].mode, 0x114);

    /* ---- a linear framebuffer beats a banked one at the same depth ----- */
    n = 0;
    list[n++] = M(0x114, 800, 600, 16, CFALSE);   /* banked, listed first  */
    list[n++] = M(0x11A, 800, 600, 16, CTRUE);    /* linear, listed later  */
    got = vbe_pick(list, n, 800, 600, 16);
    CHECK_EQI(got, 1);
    CHECK(list[got].lfb);

    /* ---- 15bpp where 16 was expected ---------------------------------- */
    /* The old exact-match rule found nothing at 800x600 and the ladder fell to
     * 640x480x8, losing resolution it did not have to. 800x600x8 was there. */
    n = 0;
    list[n++] = M(0x113, 800, 600, 15, CTRUE);    /* cannot be driven      */
    list[n++] = M(0x103, 800, 600,  8, CFALSE);
    list[n++] = M(0x101, 640, 480,  8, CFALSE);
    got = vbe_pick(list, n, 800, 600, 16);
    CHECK_EQI(got, 1);
    CHECK_EQI(list[got].bpp, 8);

    /* ---- a 24bpp card must never be selected into ---------------------- */
    n = 0;
    list[n++] = M(0x115, 800, 600, 24, CTRUE);
    got = vbe_pick(list, n, 800, 600, 16);
    CHECK_EQI(got, -1);
    /* ...even when 24bpp is what was asked for. */
    CHECK_EQI(vbe_pick(list, n, 800, 600, 24), -1);

    /* ---- the deepest drivable mode wins when the request is unavailable  */
    n = 0;
    list[n++] = M(0x103, 800, 600,  8, CTRUE);
    list[n++] = M(0x114, 800, 600, 16, CFALSE);
    got = vbe_pick(list, n, 800, 600, 32);        /* nonsense request      */
    CHECK_EQI(got, 1);
    CHECK_EQI(list[got].bpp, 16);

    /* ...but an explicit 8bpp request is honoured over the deeper mode, which
     * is what safe mode depends on. */
    got = vbe_pick(list, n, 800, 600, 8);
    CHECK_EQI(got, 0);
    CHECK_EQI(list[got].bpp, 8);

    /* ---- flags are respected ------------------------------------------- */
    n = 0;
    list[n] = M(0x114, 800, 600, 16, CTRUE); list[n].supported = CFALSE; n++;
    list[n] = M(0x115, 800, 600, 16, CTRUE); list[n].graphics  = CFALSE; n++;
    list[n++] = M(0x116, 800, 600, 16, CFALSE);
    got = vbe_pick(list, n, 800, 600, 16);
    CHECK_EQI(got, 2);

    /* ---- the size is never substituted --------------------------------- */
    /* The caller owns the fallback ladder. A picker that quietly handed back a
     * different resolution would make that ladder impossible to reason about. */
    n = 0;
    list[n++] = M(0x101, 640, 480, 16, CTRUE);
    CHECK_EQI(vbe_pick(list, n, 800, 600, 16), -1);
    CHECK_EQI(vbe_pick(list, n, 640, 480, 16), 0);

    /* ---- ties resolve to the earlier entry, so the choice is stable ---- */
    n = 0;
    list[n++] = M(0x114, 640, 480, 16, CTRUE);
    list[n++] = M(0x11A, 640, 480, 16, CTRUE);
    CHECK_EQI(vbe_pick(list, n, 640, 480, 16), 0);

    /* ---- refusals ------------------------------------------------------ */
    CHECK_EQI(vbe_pick(NULL, 3, 640, 480, 16), -1);
    CHECK_EQI(vbe_pick(list, 0, 640, 480, 16), -1);
    CHECK_EQI(vbe_pick(list, -1, 640, 480, 16), -1);
    /* A card that reports nothing usable at all. */
    n = 0;
    list[n++] = M(0x100, 320, 200, 24, CFALSE);
    CHECK_EQI(vbe_pick(list, n, 640, 480, 16), -1);
}

/* ---------------------------------------------------------------------- */
/* Reporting the list (vbe_report.c)                                       */
/*                                                                         */
/* Picking a mode and reporting the list are different jobs with opposite  */
/* instincts. The picker throws away everything it cannot use; the report  */
/* has to KEEP those, because a card offering 800x600 only at 15bpp is     */
/* precisely why the desktop is running at 640x480, and a list showing     */
/* only what worked would leave that unexplained.                          */
/* ---------------------------------------------------------------------- */

static PlatVideoMode R(int w, int h, int bpp, cbool lfb)
{
    PlatVideoMode m;
    m.w = w; m.h = h; m.bpp = bpp; m.lfb = lfb;
    return m;
}

void test_vbe_report(void)
{
    PlatVideoMode in[24], out[24];
    char line[64];
    int n, c;

    printf("- vesa mode report\n");

    /* ---- the ladder is sorted, whatever order the BIOS stored it in ---- */
    n = 0;
    in[n++] = R(1024, 768, 8, CTRUE);
    in[n++] = R(640, 480, 16, CTRUE);
    in[n++] = R(800, 600, 8, CTRUE);
    in[n++] = R(640, 480, 8, CTRUE);
    in[n++] = R(800, 600, 16, CTRUE);
    c = vbe_report_modes(in, n, out, 24);
    CHECK_EQI(c, 5);
    CHECK_EQI(out[0].w, 640);   CHECK_EQI(out[0].bpp, 8);
    CHECK_EQI(out[1].w, 640);   CHECK_EQI(out[1].bpp, 16);
    CHECK_EQI(out[2].w, 800);   CHECK_EQI(out[2].bpp, 8);
    CHECK_EQI(out[3].w, 800);   CHECK_EQI(out[3].bpp, 16);
    CHECK_EQI(out[4].w, 1024);

    /* Same width, different heights: height breaks the tie before depth. */
    n = 0;
    in[n++] = R(640, 480, 8, CTRUE);
    in[n++] = R(640, 400, 16, CTRUE);
    c = vbe_report_modes(in, n, out, 24);
    CHECK_EQI(c, 2);
    CHECK_EQI(out[0].h, 400);
    CHECK_EQI(out[1].h, 480);

    /* ---- a depth this system cannot drive is KEPT ---------------------- */
    n = 0;
    in[n++] = R(800, 600, 15, CTRUE);
    in[n++] = R(640, 480, 8, CTRUE);
    c = vbe_report_modes(in, n, out, 24);
    CHECK_EQI(c, 2);
    /* It is in the list, and it is the bigger one -- which is the fact the
     * reader needs to understand why the desktop is at 640x480. */
    CHECK_EQI(out[1].w, 800);
    CHECK_EQI(out[1].bpp, 15);
    CHECK(!vbe_depth_drivable(out[1].bpp));

    /* ---- duplicates collapse, keeping the linear entry ------------------ */
    n = 0;
    in[n++] = R(800, 600, 16, CFALSE);      /* banked listed first */
    in[n++] = R(800, 600, 16, CTRUE);       /* ...linear later     */
    c = vbe_report_modes(in, n, out, 24);
    CHECK_EQI(c, 1);
    CHECK(out[0].lfb);                      /* the one vbe_pick would take */
    /* ...and in the other order, which is the order that actually tempts a
     * "first wins" implementation into reporting banked. */
    n = 0;
    in[n++] = R(800, 600, 16, CTRUE);
    in[n++] = R(800, 600, 16, CFALSE);
    c = vbe_report_modes(in, n, out, 24);
    CHECK_EQI(c, 1);
    CHECK(out[0].lfb);
    /* Two banked copies stay banked -- collapsing must not invent an LFB. */
    n = 0;
    in[n++] = R(800, 600, 16, CFALSE);
    in[n++] = R(800, 600, 16, CFALSE);
    c = vbe_report_modes(in, n, out, 24);
    CHECK_EQI(c, 1);
    CHECK(!out[0].lfb);
    /* The same size at a DIFFERENT depth is not a duplicate. */
    n = 0;
    in[n++] = R(800, 600, 8, CTRUE);
    in[n++] = R(800, 600, 16, CTRUE);
    c = vbe_report_modes(in, n, out, 24);
    CHECK_EQI(c, 2);

    /* ---- a failed BIOS read is dropped, not printed as a 0x0 screen ---- */
    n = 0;
    in[n++] = R(0, 0, 0, CFALSE);
    in[n++] = R(640, 480, 8, CTRUE);
    in[n++] = R(640, 0, 8, CTRUE);
    in[n++] = R(-1, 480, 8, CTRUE);
    in[n++] = R(640, 480, 0, CTRUE);
    c = vbe_report_modes(in, n, out, 24);
    CHECK_EQI(c, 1);
    CHECK_EQI(out[0].w, 640);

    /* ---- a full output array keeps the TOP of the ladder ---------------
     *
     * The diagnostic end. A reader opens this to find out why the desktop is
     * at 640x480, and the answer is in what the card offers above that. The
     * order the modes arrive in must not change which ones survive, so the
     * same three lists are tried in three different orders below. */
    n = 0;
    in[n++] = R(1024, 768, 16, CTRUE);
    in[n++] = R(640, 480, 8, CTRUE);
    in[n++] = R(800, 600, 16, CTRUE);
    c = vbe_report_modes(in, n, out, 2);
    CHECK_EQI(c, 2);
    CHECK_EQI(out[0].w, 800);
    CHECK_EQI(out[1].w, 1024);
    /* Ascending arrival: the smallest is inserted first and must be evicted. */
    n = 0;
    in[n++] = R(640, 480, 8, CTRUE);
    in[n++] = R(800, 600, 16, CTRUE);
    in[n++] = R(1024, 768, 16, CTRUE);
    c = vbe_report_modes(in, n, out, 2);
    CHECK_EQI(c, 2);
    CHECK_EQI(out[0].w, 800);
    CHECK_EQI(out[1].w, 1024);
    /* Descending arrival: the smallest arrives last, into a full array, and
     * must be refused rather than displacing something bigger. */
    n = 0;
    in[n++] = R(1024, 768, 16, CTRUE);
    in[n++] = R(800, 600, 16, CTRUE);
    in[n++] = R(640, 480, 8, CTRUE);
    c = vbe_report_modes(in, n, out, 2);
    CHECK_EQI(c, 2);
    CHECK_EQI(out[0].w, 800);
    CHECK_EQI(out[1].w, 1024);
    /* A single slot keeps the single best mode. */
    c = vbe_report_modes(in, n, out, 1);
    CHECK_EQI(c, 1);
    CHECK_EQI(out[0].w, 1024);

    /* ---- refusals ------------------------------------------------------ */
    CHECK_EQI(vbe_report_modes(NULL, 3, out, 24), 0);
    CHECK_EQI(vbe_report_modes(in, 3, NULL, 24), 0);
    CHECK_EQI(vbe_report_modes(in, 0, out, 24), 0);
    CHECK_EQI(vbe_report_modes(in, -1, out, 24), 0);
    CHECK_EQI(vbe_report_modes(in, 3, out, 0), 0);

    /* ---- one row of the report ----------------------------------------- */
    {
        PlatVideoMode m = R(800, 600, 16, CTRUE);
        vbe_mode_line(&m, CFALSE, line, (cu32)sizeof line);
        CHECK_STR(line, "  800x600x16     linear");
        /* The mode on screen is marked in place, so the reader can see where
         * it sits in the ladder and whether anything better was passed over. */
        vbe_mode_line(&m, CTRUE, line, (cu32)sizeof line);
        CHECK_STR(line, "* 800x600x16     linear");
    }
    {
        PlatVideoMode m = R(800, 600, 15, CFALSE);
        vbe_mode_line(&m, CFALSE, line, (cu32)sizeof line);
        CHECK_STR(line, "  800x600x15     banked (cannot drive)");
    }
    /* Degenerate input answers with an empty row rather than crashing. */
    vbe_mode_line(NULL, CFALSE, line, (cu32)sizeof line);
    CHECK_STR(line, "");
    {
        PlatVideoMode m = R(800, 600, 16, CTRUE);
        vbe_mode_line(&m, CFALSE, NULL, 16);          /* no crash */
        vbe_mode_line(&m, CFALSE, line, 0);           /* no crash */
        {
            char small[8];
            vbe_mode_line(&m, CFALSE, small, (cu32)sizeof small);
            CHECK(small[7] == '\0');
        }
    }
}

/* ---------------------------------------------------------------------- */
/* Walking a banked framebuffer (vbe_bank.c)                               */
/*                                                                         */
/* The presenter turned an absolute byte offset into a bank and a window    */
/* offset with a divide and a multiply, PER PIXEL -- 480,000 of each for    */
/* one 800x600 present, every frame, on the fallback path taken by the      */
/* cards least able to afford it. Stepping replaces both with an add and a  */
/* compare.                                                                */
/*                                                                         */
/* The check that matters is not that stepping is fast, it is that stepping */
/* lands in exactly the same place as dividing. Anything else writes pixels */
/* into the wrong bank, which on real hardware is a torn or scrambled       */
/* screen and on this machine is nothing at all.                            */
/* ---------------------------------------------------------------------- */
void test_vbe_bank(void)
{
    VbeBank w;
    long gran, off, step;
    int i;

    printf("- banked framebuffer walk\n");

    /* ---- seek is the plain divide -------------------------------------- */
    vbe_bank_seek(&w, 65536L, 0L);
    CHECK_EQI((int)w.bank, 0);  CHECK_EQI((int)w.local, 0);
    vbe_bank_seek(&w, 65536L, 65535L);
    CHECK_EQI((int)w.bank, 0);  CHECK_EQI((int)w.local, 65535);
    vbe_bank_seek(&w, 65536L, 65536L);
    CHECK_EQI((int)w.bank, 1);  CHECK_EQI((int)w.local, 0);
    vbe_bank_seek(&w, 65536L, 65537L);
    CHECK_EQI((int)w.bank, 1);  CHECK_EQI((int)w.local, 1);
    /* A 4 KB granularity is what several real cards report. */
    vbe_bank_seek(&w, 4096L, 10000L);
    CHECK_EQI((int)w.bank, 2);  CHECK_EQI((int)w.local, 10000 - 2 * 4096);

    /* ---- stepping equals seeking, everywhere --------------------------- */
    /* The whole claim, checked over a full 800x600x16 frame's worth of
     * offsets at each granularity a card is likely to report. */
    {
        static const long GRANS[] = { 65536L, 32768L, 16384L, 4096L, 1024L };
        int g;
        for (g = 0; g < (int)(sizeof(GRANS) / sizeof(GRANS[0])); g++) {
            int mismatched = 0;
            gran = GRANS[g];
            vbe_bank_seek(&w, gran, 0L);
            for (off = 0; off < 960000L; off += 2L) {
                VbeBank ref;
                vbe_bank_seek(&ref, gran, off);
                if (ref.bank != w.bank || ref.local != w.local) {
                    mismatched++;
                }
                vbe_bank_step(&w, 2L);
            }
            CHECK_EQI(mismatched, 0);
        }
    }

    /* ---- a one-byte step, which is the 8bpp path ----------------------- */
    {
        int mismatched = 0;
        vbe_bank_seek(&w, 4096L, 0L);
        for (off = 0; off < 20000L; off++) {
            VbeBank ref;
            vbe_bank_seek(&ref, 4096L, off);
            if (ref.bank != w.bank || ref.local != w.local) { mismatched++; }
            vbe_bank_step(&w, 1L);
        }
        CHECK_EQI(mismatched, 0);
    }

    /* ---- a whole-scanline step crosses several banks at once ----------- */
    step = 1600L;                       /* 800 px at 16bpp */
    vbe_bank_seek(&w, 1024L, 0L);
    for (i = 0; i < 50; i++) { vbe_bank_step(&w, step); }
    {
        VbeBank ref;
        vbe_bank_seek(&ref, 1024L, step * 50L);
        CHECK_EQI((int)(w.bank - ref.bank), 0);
        CHECK_EQI((int)(w.local - ref.local), 0);
    }

    /* ---- the offset stays inside the window, always -------------------- */
    {
        int outside = 0;
        vbe_bank_seek(&w, 4096L, 0L);
        for (i = 0; i < 5000; i++) {
            vbe_bank_step(&w, 3L);
            if (w.local < 0 || w.local >= 4096L) { outside++; }
        }
        CHECK_EQI(outside, 0);
    }

    /* ---- stepping back is stepping forward, undone ---------------------- */
    vbe_bank_seek(&w, 4096L, 9000L);
    vbe_bank_step(&w, 5000L);
    vbe_bank_step(&w, -5000L);
    {
        VbeBank ref;
        vbe_bank_seek(&ref, 4096L, 9000L);
        CHECK_EQI((int)(w.bank - ref.bank), 0);
        CHECK_EQI((int)(w.local - ref.local), 0);
    }

    /* ---- refusals ------------------------------------------------------ */
    /* A granularity of zero must not divide by it. The VBE "0 means 64 KB"
     * convention belongs to the caller reading the mode info, and applying it
     * here too would be the same decision in two places. */
    vbe_bank_seek(&w, 0L, 100L);
    CHECK_EQI((int)w.gran, 1);
    vbe_bank_seek(&w, -8L, 100L);
    CHECK_EQI((int)w.gran, 1);
    /* A negative offset is a caller bug, answered as the start of the frame. */
    vbe_bank_seek(&w, 4096L, -1L);
    CHECK_EQI((int)w.bank, 0);  CHECK_EQI((int)w.local, 0);
    vbe_bank_seek(NULL, 4096L, 0L);     /* no crash */
    vbe_bank_step(NULL, 4L);            /* no crash */
}
