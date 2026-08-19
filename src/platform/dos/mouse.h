/*
 * mouse.h - PS/2 mouse via INT 33h (DOS backend). Compiled only under
 * CASTALIA_DOS. PS/2 input is first-class per the hardware profile.
 */
#ifndef CASTALIA_DOS_MOUSE_H
#define CASTALIA_DOS_MOUSE_H
#ifdef CASTALIA_DOS
#include "castalia/ctypes.h"

/* Reset the driver and constrain it to the screen. Returns CTRUE if a mouse
 * is present. */
cbool mouse_init(int screen_w, int screen_h);
void  mouse_shutdown(void);
/* Latest absolute position (clamped to screen) and button bitmask
 * (PLAT_MB_* values). */
void  mouse_read(int *x, int *y, int *buttons);
/* Whether the driver answered the wheel handshake at init. */
cbool mouse_has_wheel(void);
/*
 * Detents since the last call: positive away from the user, negative toward
 * them, zero when there is no wheel or nothing turned. Reading CONSUMES the
 * count, so call it once per poll.
 */
int   mouse_read_wheel(void);

#endif /* CASTALIA_DOS */
#endif
