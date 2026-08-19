/*
 * wm_move.h - Where a window is allowed to end up (pure, host-tested).
 *
 * Moving and resizing a window both come down to the same question -- what
 * rectangle is acceptable -- and that question was being answered twice, in
 * two inline blocks of magic numbers inside wm_dispatch.c, reachable only by
 * dragging a mouse. Now it is answered here, with no manager state, so it can
 * be driven from a test and from the keyboard as well as from a drag.
 *
 * The policy is "you can push a window off an edge, but not out of reach":
 * enough of it must remain on screen to grab again. A window dragged entirely
 * off the right edge is not a feature anyone asked for, and on a machine with
 * no window list to recover it from, it is a window you have lost.
 */
#ifndef CASTALIA_WM_MOVE_H
#define CASTALIA_WM_MOVE_H

#include "castalia/rect.h"

/* The smallest a window may be made. Below this the title bar has no room for
 * its caption buttons and the frame stops being usable. */
#define WM_MIN_W  140
#define WM_MIN_H  90

/* How much must stay on screen: this many pixels horizontally, and this many
 * rows of the title bar vertically. The vertical figure is small on purpose --
 * a couple of rows of title bar is enough to grab. */
#define WM_KEEP_X 40
#define WM_KEEP_Y 8

/*
 * The frame 'f' would have if its top-left were put at (nx,ny). The size is
 * never changed -- a clamp that resizes is a clamp with a bug in it -- only
 * the position, so that at least WM_KEEP_X pixels stay within [0,sw) and the
 * top edge stays in [0, sh-WM_KEEP_Y].
 */
CRect wm_move_clamp(const CRect *f, int nx, int ny, int sw, int sh);

/*
 * The frame 'f' would have at 'nw' x 'nh', keeping its top-left corner. Never
 * smaller than WM_MIN_W x WM_MIN_H, and never running past the right or bottom
 * edge of an sw x sh screen -- unless the window starts so far over that even
 * the minimum size would not fit, in which case the minimum wins. Being told
 * you may not shrink a window any further is a limit; being handed a window
 * too small to use is a bug.
 */
CRect wm_size_clamp(const CRect *f, int nw, int nh, int sw, int sh);

#endif /* CASTALIA_WM_MOVE_H */
