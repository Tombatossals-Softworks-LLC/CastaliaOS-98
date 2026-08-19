/*
 * keyboard.c - INT 16h keyboard polling (DOS backend, Open Watcom).
 *
 * Uses the enhanced-keyboard BIOS calls (AH=11h check, AH=10h read) and maps
 * the common extended scan codes (arrows, function keys, Delete) to the
 * PLAT_KEY_* space. Compiled only under CASTALIA_DOS.
 */
#ifdef CASTALIA_DOS

#include "keyboard.h"
#include "castalia/plat.h"

#include <i86.h>

void keyboard_init(void)     { /* nothing to set up for polled INT 16h */ }
void keyboard_shutdown(void) { }

/* The BIOS keyboard shift-flags byte at 0040:0017 (identity-mapped under the
 * extender): bit0 right Shift, bit1 left Shift, bit2 Ctrl, bit3 Alt. */
int keyboard_mods(void)
{
    volatile unsigned char *flags = (volatile unsigned char *)0x00000417UL;
    unsigned char f = *flags;
    int mods = 0;
    if (f & 0x03) { mods |= PLAT_MOD_SHIFT; }
    if (f & 0x04) { mods |= PLAT_MOD_CTRL; }
    if (f & 0x08) { mods |= PLAT_MOD_ALT; }
    return mods;
}

/* Map an extended (ASCII==0) scan code to a PLAT_KEY_* value, or 0. */
static int map_extended(int scan)
{
    switch (scan) {
    case 0x48: return PLAT_KEY_UP;
    case 0x50: return PLAT_KEY_DOWN;
    case 0x4B: return PLAT_KEY_LEFT;
    case 0x4D: return PLAT_KEY_RIGHT;
    case 0x53: return PLAT_KEY_DELETE;
    case 0x47: return PLAT_KEY_HOME;
    case 0x4F: return PLAT_KEY_END;
    case 0x49: return PLAT_KEY_PGUP;
    case 0x51: return PLAT_KEY_PGDN;
    case 0x3B: return PLAT_KEY_F1;
    case 0x3C: return PLAT_KEY_F2;
    case 0x3D: return PLAT_KEY_F3;
    case 0x3F: return PLAT_KEY_F5;      /* refresh the folder being browsed */
    case 0x44: return PLAT_KEY_F10;     /* the menu for the selected item */
    case 0x6B: return PLAT_KEY_CLOSE;   /* Alt+F4  (AL=0, scan 0x6B) */
    /* Alt+Tab. The shell also honours Shift on this key to walk the switch
     * order backwards, and the modifier reaches it -- keyboard_mods() reads
     * the BIOS shift byte on every event. What is NOT confirmed on the target
     * is whether a real BIOS emits this same scan code for Shift+Alt+Tab at
     * all; if it emits nothing, Shift+Alt+Tab is simply inert there, and the
     * forward direction is unaffected either way. */
    case 0xA5: return PLAT_KEY_NEXTWIN; /* Alt+Tab (AL=0, scan 0xA5) */
    /* Alt+Space -> the window menu. Reported here the same way Alt+F4 is, as
     * a scan code with no character. Not confirmed on the target: some BIOSes
     * return AX=0x3920 for this combination (a real space with scan 0x39)
     * rather than AL=0, in which case it arrives as an ordinary space and the
     * menu simply does not open. The mouse route -- right-click the title bar
     * -- works regardless, which is why the feature does not depend on this
     * line being right. */
    case 0x39: return PLAT_KEY_SYSMENU; /* Alt+Space (AL=0, scan 0x39) */
    default:   return 0;
    }
}

/* Is a keystroke waiting? INT 16h AH=11h reports this via ZF, but Open
 * Watcom's union REGS does not expose the flags register, so we read the
 * BIOS keyboard buffer head/tail pointers directly (0040:001A / 0040:001C,
 * identity-mapped under the DOS extender). head != tail means a key waits. */
static cbool kb_pending(void)
{
    volatile unsigned short *head = (volatile unsigned short *)0x0000041AUL;
    volatile unsigned short *tail = (volatile unsigned short *)0x0000041CUL;
    return (*head != *tail) ? CTRUE : CFALSE;
}

cbool keyboard_poll(int *key, int *ch)
{
    union REGS r;

    if (!kb_pending()) { return CFALSE; }

    /* AH=10h: read the enhanced keystroke (AL=ASCII, AH=scan code). */
    r.h.ah = 0x10;
    int386(0x16, &r, &r);
    {
        int ascii = r.h.al;
        int scan  = r.h.ah;
        /* The enhanced BIOS call reports the dedicated "grey" cursor/navigation
         * keys (arrows, Home/End, PgUp/PgDn, Insert, Delete) with AL=0xE0 -- not
         * AL=0 as the keypad equivalents do -- so software can tell them apart.
         * Treat both 0x00 and 0xE0 as "no ASCII; use the scan code". Without
         * this the arrow keys arrive as a bogus 0xE0 character and are dropped. */
        if (ascii == 0 || ascii == 0xE0) {
            int k = map_extended(scan);
            if (key) { *key = k; }
            if (ch)  { *ch = 0; }
            return (k != 0) ? CTRUE : CFALSE;
        }
        if (key) { *key = ascii; }
        if (ch)  { *ch = (ascii >= 32 && ascii < 127) ? ascii : 0; }
        return CTRUE;
    }
}

#endif /* CASTALIA_DOS */
