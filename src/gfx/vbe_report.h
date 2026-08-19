/*
 * vbe_report.h - Turning a card's mode list into something worth reading.
 *
 * A VESA BIOS can report two hundred modes. Most of a real list is text modes,
 * near-duplicates that differ only in whether a linear framebuffer is offered,
 * and depths this system cannot drive. Printed raw it is three screens of
 * noise, and the two facts a person actually opened the report for -- what the
 * card can do, and why the desktop is running at the size it is -- are lost in
 * it.
 *
 * So the list is tidied, and every step of the tidying is a decision:
 *
 *   - Modes the presenter cannot drive are KEPT, and marked. This is the point
 *     of the report. A card offering 800x600 only at 15bpp is exactly why the
 *     desktop fell back to 640x480, and a list showing only drivable modes
 *     would leave that unexplained -- it would show 640x480 and simply not
 *     mention that 800x600 was there.
 *   - Duplicates on (w, h, bpp) collapse, keeping the linear-framebuffer entry
 *     when there is one, because that is the entry vbe_pick would choose. A
 *     list that showed both would suggest a choice the user does not have.
 *   - The order is by width, then height, then depth, all ascending, so the
 *     list reads as a ladder rather than in whatever order the BIOS stored it.
 *
 * All of it is pure, over a list the caller has already read off the card, so
 * tests/test_vbe.c can walk mode lists no machine here has.
 */
#ifndef CASTALIA_VBE_REPORT_H
#define CASTALIA_VBE_REPORT_H

#include "castalia/ctypes.h"
#include "castalia/plat.h"

/*
 * Tidy 'in' into 'out' (filter, de-duplicate, sort) and return how many modes
 * were written, never more than 'max'.
 *
 * Entries with a non-positive width, height or depth are dropped: a BIOS that
 * answers a mode query with zeroes is reporting a failed read, not a 0x0
 * screen, and printing it as one would be a lie the reader cannot detect.
 *
 * When more modes survive than fit, the SMALLEST are dropped -- a truncated
 * list keeps the top of the ladder, because that is the diagnostic end. The
 * report exists to answer "why is the desktop at 640x480", and the answer is
 * in what the card offers at and above the current mode. Truncating by arrival
 * order instead would make the report depend on the sequence the BIOS happened
 * to store its list in. Callers should still say how many were dropped.
 *
 * 'out' may not alias 'in'.
 */
int vbe_report_modes(const PlatVideoMode *in, int n,
                     PlatVideoMode *out, int max);

/*
 * One row for the report: "800x600x16   linear", plus " (cannot drive)" for a
 * depth the presenter does not write correctly, and a leading "*" for the mode
 * currently on screen.
 *
 * The current mode is marked rather than stated separately because the useful
 * question is where it sits in the ladder -- whether anything better was
 * available and passed over.
 */
void vbe_mode_line(const PlatVideoMode *m, cbool current,
                   char *dst, cu32 dstsz);

#endif /* CASTALIA_VBE_REPORT_H */
