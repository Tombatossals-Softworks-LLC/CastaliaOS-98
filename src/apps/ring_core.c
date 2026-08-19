/*
 * ring_core.c - the ring's index arithmetic (see ring_core.h).
 */
#include "ring_core.h"

void ring_init(Ring *r, int cap)
{
    if (r == NULL) { return; }
    r->cap = (cap > 0) ? cap : 1;
    r->count = 0;
    r->head = 0;
}

void ring_reset(Ring *r)
{
    if (r == NULL) { return; }
    r->count = 0;
    r->head = 0;
}

int ring_push(Ring *r)
{
    int slot;
    if (r == NULL) { return 0; }
    /*
     * An uninitialised ring has cap 0, which every reader here would divide
     * by. It must not crash -- but it must not quietly WORK either.
     *
     * The first version set cap = 1 and carried on. That turned "somebody
     * forgot ring_init" into a one-slot ring, and the Console -- whose struct
     * arrives zeroed from sys_calloc -- became a one-line console: the banner
     * and the directory listing scrolled off the instant they were printed.
     * It looked plausible enough to miss, and was only caught by opening the
     * window and looking at it.
     *
     * So: slot 0, and the ring stays empty. count never rises, ring_at keeps
     * answering -1, and the window shows NOTHING rather than a convincing
     * last line. A blank console is a bug report; a one-line console is a
     * puzzle.
     */
    if (r->cap < 1) { return 0; }
    slot = r->head;
    r->head = (r->head + 1) % r->cap;
    if (r->count < r->cap) { r->count++; }
    return slot;
}

int ring_at(const Ring *r, int i)
{
    int at;
    if (r == NULL || r->cap < 1) { return -1; }
    if (i < 0 || i >= r->count) { return -1; }
    /*
     * The oldest sits 'count' back from the head. Written as a loop rather
     * than `+ cap * 2` because that form only works while count <= cap, which
     * is true here but is an invariant held somewhere else -- and the version
     * that quietly depends on it is the one that breaks when somebody changes
     * the other end.
     */
    at = r->head - r->count + i;
    while (at < 0) { at += r->cap; }
    return at % r->cap;
}

int ring_count(const Ring *r) { return (r != NULL) ? r->count : 0; }

cbool ring_full(const Ring *r)
{
    /* `count >= cap` alone answers TRUE for an uninitialised ring, where both
     * are zero -- a ring with no capacity reporting itself full. It holds
     * nothing and can hold nothing; "full" is the wrong word for that, and a
     * caller asking "should I evict?" would get yes. */
    if (r == NULL || r->cap < 1) { return CFALSE; }
    return (r->count >= r->cap) ? CTRUE : CFALSE;
}
