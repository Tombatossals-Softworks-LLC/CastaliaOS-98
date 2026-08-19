/*
 * sw_core.h - The order Alt+Tab walks in.
 *
 * Alt+Tab currently rotates the window stack: press it twice and you are two
 * windows away from where you started. Every system of this era did something
 * different and better, and the difference is not cosmetic -- they walk
 * MOST-RECENTLY-USED order, so Alt+Tab returns you to the window you were just
 * in, and pressing it twice puts you back. That single property is what makes
 * it useful for flipping between two things, which is what people mostly use
 * it for.
 *
 * Keeping that order right is fiddly in exactly the ways that do not show up
 * until someone is using the machine: focusing a window that is already in the
 * list has to move it rather than duplicate it; closing the window you are
 * standing on has to leave a sane place to stand; and a list with a bound has
 * to drop the LEAST recent entry, never the one in front of you.
 *
 * So the order lives here as a small pure structure the shell owns and the
 * tests can drive through sequences no one would sit and click by hand.
 * Nothing here knows what a window is -- it moves integers.
 */
#ifndef CASTALIA_SW_CORE_H
#define CASTALIA_SW_CORE_H

#include "castalia/ctypes.h"

#define SW_MAX 32          /* windows remembered; beyond this the oldest go */

typedef struct {
    int id[SW_MAX];        /* [0] is the most recently focused              */
    int count;
} SwList;

void sw_clear(SwList *l);

/* Note that 'id' was just focused: it moves to the front, or is inserted
 * there if new. When the list is full the LEAST recent entry is dropped. */
void sw_touch(SwList *l, int id);

/* Forget a window that has gone away. Missing ids are ignored. */
void sw_remove(SwList *l, int id);

int sw_count(const SwList *l);
/* The id at position 'index' in MRU order, or -1 out of range. */
int sw_at(const SwList *l, int index);

/*
 * The id 'steps' along from the front, wrapping. steps==1 is "the window
 * before this one", which is what a single Alt+Tab should land on. Returns -1
 * when the list is empty. Negative steps walk backwards, for Shift+Alt+Tab.
 */
int sw_pick(const SwList *l, int steps);

#endif /* CASTALIA_SW_CORE_H */
