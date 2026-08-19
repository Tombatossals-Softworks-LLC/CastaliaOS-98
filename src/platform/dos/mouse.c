/*
 * mouse.c - INT 33h mouse services (DOS backend, Open Watcom).
 * Compiled only under CASTALIA_DOS. Validated on emulator/hardware per
 * docs/TESTING.md.
 */
#ifdef CASTALIA_DOS

#include "mouse.h"
#include "castalia/plat.h"
#include "castalia/sys.h"

#include <i86.h>

static cbool g_present = CFALSE;
/*
 * Whether the driver speaks the wheel API.
 *
 * INT 33h has no wheel in its original service set; the extension every DOS
 * wheel driver of the period implements (CuteMouse and the drivers that
 * copied it) is a handshake -- AX=0011h answers 574Dh, "WM", and reports in
 * CX whether the mouse under it actually has a wheel. Only after that
 * handshake does function 3 put a signed detent count in BH, and a driver
 * that never saw the handshake keeps BH as button bits, so reading it
 * unconditionally would turn ordinary clicks into scrolling.
 *
 * NOT verified on hardware or in the emulator -- the CI mouse is synthetic
 * and QEMU's is wheel-less. Everything above this seam is driven by the host
 * backend and is checked; this handshake is the part that is only argued for.
 * See docs/TESTING.md.
 */
static cbool g_wheel = CFALSE;

cbool mouse_init(int screen_w, int screen_h)
{
    union REGS r;

    r.w.ax = 0x0000;            /* reset & detect */
    int386(0x33, &r, &r);
    if (r.w.ax != 0xFFFF) {
        SYS_LOGW("plat", "mouse: INT 33h reports no driver");
        g_present = CFALSE;
        return CFALSE;
    }

    /* Constrain the driver's virtual coordinate range to the screen so the
     * values from function 3 map straight to pixels. */
    r.w.ax = 0x0007; r.w.cx = 0; r.w.dx = (unsigned)(screen_w - 1);
    int386(0x33, &r, &r);       /* set horizontal range */
    r.w.ax = 0x0008; r.w.cx = 0; r.w.dx = (unsigned)(screen_h - 1);
    int386(0x33, &r, &r);       /* set vertical range   */

    /* The wheel handshake, after the ranges: a driver that does not know it
     * simply leaves AX alone, which is why the answer is checked rather than
     * the call. */
    r.w.ax = 0x0011;
    r.w.bx = 0; r.w.cx = 0;
    int386(0x33, &r, &r);
    g_wheel = (r.w.ax == 0x574D && (r.w.cx & 0x0001)) ? CTRUE : CFALSE;

    g_present = CTRUE;
    SYS_LOGI("plat", "mouse: INT 33h driver present%s",
             g_wheel ? " (with a wheel)" : "");
    return CTRUE;
}

cbool mouse_has_wheel(void)
{
    return g_wheel;
}

void mouse_shutdown(void)
{
    union REGS r;
    if (!g_present) { return; }
    r.w.ax = 0x0000;            /* reset also hides the DOS cursor */
    int386(0x33, &r, &r);
    g_present = CFALSE;
}

void mouse_read(int *x, int *y, int *buttons)
{
    union REGS r;
    if (!g_present) {
        if (x) { *x = 0; } if (y) { *y = 0; } if (buttons) { *buttons = 0; }
        return;
    }
    r.w.ax = 0x0003;            /* get position and button status */
    int386(0x33, &r, &r);
    if (x) { *x = (int)r.w.cx; }
    if (y) { *y = (int)r.w.dx; }
    if (buttons) {
        int b = 0;
        if (r.w.bx & 0x01) { b |= PLAT_MB_LEFT; }
        if (r.w.bx & 0x02) { b |= PLAT_MB_RIGHT; }
        if (r.w.bx & 0x04) { b |= PLAT_MB_MIDDLE; }
        *buttons = b;
    }
}

int mouse_read_wheel(void)
{
    union REGS r;
    int w;
    if (!g_present || !g_wheel) { return 0; }
    r.w.ax = 0x0003;            /* the same call; BH carries the detents */
    int386(0x33, &r, &r);
    /*
     * BH is a SIGNED byte and arrives in the high half of a 16-bit BX, so it
     * has to be sign-extended by hand: a single notch toward the user reads
     * as 0xFF, and taking that as 255 would scroll a document to its end in
     * one flick.
     *
     * Reading is consuming -- the driver zeroes the counter -- so this is
     * called once per poll and its answer is not asked for twice.
     */
    w = (int)((r.w.bx >> 8) & 0xFF);
    if (w > 127) { w -= 256; }
    return w;
}

#endif /* CASTALIA_DOS */
