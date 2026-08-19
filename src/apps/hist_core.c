/*
 * hist_core.c - The sample ring behind the live graphs (pure, host-tested).
 *
 * See hist_core.h. Everything is O(HIST_MAX) at worst and allocates nothing.
 */
#include "hist_core.h"

void hist_clear(Hist *h)
{
    int i;
    if (h == NULL) { return; }
    for (i = 0; i < HIST_MAX; i++) { h->sample[i] = 0; }
    ring_init(&h->ring, HIST_MAX);
}

void hist_push(Hist *h, long value)
{
    if (h == NULL) { return; }
    h->sample[ring_push(&h->ring)] = value;
}

int hist_count(const Hist *h)
{
    /* NULL-checked here rather than relying on ring_count: taking &h->ring
     * off a null pointer is undefined before the callee ever sees it. */
    return (h != NULL) ? ring_count(&h->ring) : 0;
}

long hist_at(const Hist *h, int index)
{
    int at;
    if (h == NULL) { return 0; }
    /* The oldest sample sits head-count back, wrapped -- which is ring_at's
     * whole job, and was spelled out here as one of four copies. */
    at = ring_at(&h->ring, index);
    return (at >= 0) ? h->sample[at] : 0;
}

long hist_newest(const Hist *h)
{
    return (h == NULL || hist_count(h) == 0) ? 0 : hist_at(h, hist_count(h) - 1);
}

long hist_max(const Hist *h)
{
    long m = 0;
    int i;
    if (h == NULL) { return 0; }
    for (i = 0; i < hist_count(h); i++) {
        long v = hist_at(h, i);
        if (v > m) { m = v; }
    }
    return m;
}

long hist_avg(const Hist *h)
{
    long sum = 0;
    int i;
    if (h == NULL || hist_count(h) == 0) { return 0; }
    for (i = 0; i < hist_count(h); i++) { sum += hist_at(h, i); }
    return sum / (long)hist_count(h);
}

int hist_bar(long value, long scale, int height)
{
    long px;
    if (height <= 0) { return 0; }
    if (scale <= 0) { return 0; }
    if (value <= 0) { return 0; }
    if (value >= scale) { return height; }
    /* value < scale here (the early-out above), and both callers feed this
     * KILOBYTES or milliseconds -- at most a few thousand against a height of
     * a few dozen. A caller passing raw BYTES would overflow this multiply
     * above ~53 MB, which is the bug ui_meter_fill exists to avoid; if one
     * ever appears, scale it down before calling rather than widening here. */
    px = (value * (long)height) / scale;
    if (px < 0) { px = 0; }
    if (px > (long)height) { px = height; }
    return (int)px;
}

long hist_scale(long peak, long floor_value)
{
    long step = 1;
    if (floor_value < 1) { floor_value = 1; }
    if (peak < floor_value) { peak = floor_value; }
    /* Round up to 1, 2 or 5 times a power of ten, the way an axis is read. */
    while (step * 10 <= peak) { step *= 10; }
    if (peak <= step) { return step; }
    if (peak <= step * 2) { return step * 2; }
    if (peak <= step * 5) { return step * 5; }
    return step * 10;
}
