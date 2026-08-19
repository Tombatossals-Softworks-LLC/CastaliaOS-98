/*
 * hist_core.h - A fixed ring of samples for the live graphs.
 *
 * The Task Manager's Performance panel plots memory and frame time over the
 * last minute or so. That needs three things done right and nothing else: a
 * ring that wraps without losing its ordering, an honest answer while it is
 * still filling, and a scale that follows the data instead of clipping it.
 *
 * All of it is integer arithmetic over a caller-owned struct -- no allocation,
 * no clock, no drawing -- so tests/test_hist.c can drive every edge case.
 */
#ifndef CASTALIA_HIST_CORE_H
#define CASTALIA_HIST_CORE_H

#include "castalia/ctypes.h"
#include "ring_core.h"

#define HIST_MAX 120        /* samples kept; at 2/s that is a minute        */

typedef struct {
    long sample[HIST_MAX];
    Ring ring;              /* which slots are valid, and in what order     */
} Hist;

void hist_clear(Hist *h);
void hist_push(Hist *h, long value);

/* How many samples are available, and one of them: index 0 is the OLDEST
 * still held, index count-1 the newest. Out of range reads give 0. */
int  hist_count(const Hist *h);
long hist_at(const Hist *h, int index);
long hist_newest(const Hist *h);

/* The largest sample held (0 when empty). */
long hist_max(const Hist *h);
/* The mean of what is held, rounded down (0 when empty). */
long hist_avg(const Hist *h);

/* The pixel height of 'value' on a graph 'height' px tall whose top is
 * 'scale'. Clamps to the graph and never divides by zero -- an empty or flat
 * history must not produce a bar taller than the box it lives in. */
int  hist_bar(long value, long scale, int height);

/* A round number at or above 'peak' to use as a graph's top, so the axis does
 * not jitter with every sample. Always at least 'floor_value'. */
long hist_scale(long peak, long floor_value);

#endif /* CASTALIA_HIST_CORE_H */
