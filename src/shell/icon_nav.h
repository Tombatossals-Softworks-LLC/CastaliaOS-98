/*
 * icon_nav.h - Which icon an arrow key should move to (pure, host-tested).
 *
 * The desktop icons could be reached only with a mouse. On the machine this
 * system was actually verified on -- a FreeDOS box driven by keyboard alone --
 * This Machine, Documents, Control Center, the Log Viewer, the Media Player,
 * the Clock and the Recycle Bin were simply unreachable, and the launcher was
 * the only way in.
 *
 * The awkward part is that icons are DRAGGABLE and their positions persist, so
 * "the next icon" cannot mean "the next in the array". After somebody has
 * rearranged their desktop the array order says nothing about what is next to
 * what, and an arrow key that jumped across the screen because two icons were
 * adjacent in memory would be worse than no arrow key at all.
 *
 * So this answers the spatial question -- given where the icons actually are,
 * which one lies in the direction asked for -- and answers it with no shell
 * state, so tests/test_iconnav.c can lay out desktops by hand.
 */
#ifndef CASTALIA_ICON_NAV_H
#define CASTALIA_ICON_NAV_H

#include "castalia/rect.h"

#define ICON_NAV_LEFT  0
#define ICON_NAV_RIGHT 1
#define ICON_NAV_UP    2
#define ICON_NAV_DOWN  3

/*
 * The icon to move to from 'from' (an index, or -1 for "nothing selected yet")
 * in direction 'dir'. Returns -1 when there is nothing that way, which the
 * caller should treat as "stay where you are" rather than as an error: an
 * arrow at the edge of the desktop should do nothing, not wrap around to the
 * far side, because a selection that leaps the screen is a selection you have
 * to go and find again.
 *
 * From -1 it returns the top-left-most icon, so the first arrow press always
 * lands somewhere sensible whichever key it was.
 *
 * Candidates are ranked by distance along the direction of travel plus a
 * heavily weighted perpendicular offset, so an icon in the same row or column
 * wins over a nearer one that is off to the side. That is the behaviour people
 * expect from arrow keys and it is the reason a plain nearest-centre metric is
 * not enough.
 */
int icon_nav_next(const CRect *rects, int count, int from, int dir);

#endif /* CASTALIA_ICON_NAV_H */
