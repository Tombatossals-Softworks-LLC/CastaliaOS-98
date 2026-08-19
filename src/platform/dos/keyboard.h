/*
 * keyboard.h - Keyboard via INT 16h (DOS backend). Compiled only under
 * CASTALIA_DOS.
 */
#ifndef CASTALIA_DOS_KEYBOARD_H
#define CASTALIA_DOS_KEYBOARD_H
#ifdef CASTALIA_DOS
#include "castalia/ctypes.h"

void  keyboard_init(void);
void  keyboard_shutdown(void);
/* Non-blocking: if a key is waiting, fills *key (PLAT_KEY_* / ASCII) and
 * *ch (ASCII or 0) and returns CTRUE. */
cbool keyboard_poll(int *key, int *ch);

/* Current keyboard modifier state as a PLAT_MOD_* bitmask, read from the BIOS
 * shift-flags byte (0040:0017). */
int   keyboard_mods(void);

#endif /* CASTALIA_DOS */
#endif
