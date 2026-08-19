/*
 * sw_core.c - Most-recently-used window order (see sw_core.h).
 *
 * A small array moved by hand rather than a linked list: the bound is 32, the
 * operations are all "move one entry to the front", and on this machine a
 * memmove of a few dozen ints costs less than the pointer chasing would.
 */
#include "castalia/ctypes.h"
#include "sw_core.h"

void sw_clear(SwList *l)
{
    if (l == NULL) { return; }
    l->count = 0;
}

static int sw_index_of(const SwList *l, int id)
{
    int i;
    for (i = 0; i < l->count; i++) {
        if (l->id[i] == id) { return i; }
    }
    return -1;
}

void sw_touch(SwList *l, int id)
{
    int at, i;
    if (l == NULL) { return; }
    at = sw_index_of(l, id);
    if (at == 0) { return; }                  /* already in front           */
    if (at > 0) {
        for (i = at; i > 0; i--) { l->id[i] = l->id[i - 1]; }
        l->id[0] = id;
        return;
    }
    /* New. Drop the least recent if there is no room -- never the front,
     * which is where the user is standing. */
    if (l->count < SW_MAX) { l->count++; }
    for (i = l->count - 1; i > 0; i--) { l->id[i] = l->id[i - 1]; }
    l->id[0] = id;
}

void sw_remove(SwList *l, int id)
{
    int at, i;
    if (l == NULL) { return; }
    at = sw_index_of(l, id);
    if (at < 0) { return; }
    for (i = at; i < l->count - 1; i++) { l->id[i] = l->id[i + 1]; }
    l->count--;
}

int sw_count(const SwList *l)
{
    return (l == NULL) ? 0 : l->count;
}

int sw_at(const SwList *l, int index)
{
    if (l == NULL || index < 0 || index >= l->count) { return -1; }
    return l->id[index];
}

int sw_pick(const SwList *l, int steps)
{
    int at;
    if (l == NULL || l->count <= 0) { return -1; }
    /* Wrap in both directions; C89 leaves negative % implementation-defined
     * enough that doing it by hand is the honest way. */
    at = steps % l->count;
    if (at < 0) { at += l->count; }
    return l->id[at];
}
