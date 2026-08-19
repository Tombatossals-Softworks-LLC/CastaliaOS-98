/*
 * test_hist.c - The sample ring behind the live graphs (hist_core.c).
 *
 * A graph is only as honest as its buffer. These tests pin the three things
 * that go wrong: the ring losing its ordering when it wraps, the answers while
 * it is still filling, and a scale that clips the data or divides by zero.
 */
#include "ctest.h"
#include "hist_core.h"

static Hist g_h;

static void h_fill_and_wrap(void)
{
    int i;
    hist_clear(&g_h);
    CHECK_EQI(hist_count(&g_h), 0);
    CHECK_EQI((int)hist_max(&g_h), 0);
    CHECK_EQI((int)hist_avg(&g_h), 0);
    CHECK_EQI((int)hist_newest(&g_h), 0);

    /* Partly filled: oldest first, newest last, nothing invented. */
    hist_push(&g_h, 10);
    hist_push(&g_h, 20);
    hist_push(&g_h, 30);
    CHECK_EQI(hist_count(&g_h), 3);
    CHECK_EQI((int)hist_at(&g_h, 0), 10);
    CHECK_EQI((int)hist_at(&g_h, 2), 30);
    CHECK_EQI((int)hist_newest(&g_h), 30);
    CHECK_EQI((int)hist_max(&g_h), 30);
    CHECK_EQI((int)hist_avg(&g_h), 20);
    CHECK_EQI((int)hist_at(&g_h, 3), 0);      /* past the end */
    CHECK_EQI((int)hist_at(&g_h, -1), 0);

    /* Exactly full. */
    hist_clear(&g_h);
    for (i = 0; i < HIST_MAX; i++) { hist_push(&g_h, i); }
    CHECK_EQI(hist_count(&g_h), HIST_MAX);
    CHECK_EQI((int)hist_at(&g_h, 0), 0);
    CHECK_EQI((int)hist_at(&g_h, HIST_MAX - 1), HIST_MAX - 1);
    CHECK_EQI((int)hist_newest(&g_h), HIST_MAX - 1);

    /* One past full: the oldest is gone, the order still runs old to new. */
    hist_push(&g_h, 1000);
    CHECK_EQI(hist_count(&g_h), HIST_MAX);
    CHECK_EQI((int)hist_at(&g_h, 0), 1);
    CHECK_EQI((int)hist_at(&g_h, HIST_MAX - 2), HIST_MAX - 1);
    CHECK_EQI((int)hist_newest(&g_h), 1000);
    CHECK_EQI((int)hist_max(&g_h), 1000);

    /* Well past full, so the ring has wrapped several times. */
    for (i = 0; i < HIST_MAX * 3; i++) { hist_push(&g_h, i); }
    CHECK_EQI(hist_count(&g_h), HIST_MAX);
    CHECK_EQI((int)hist_newest(&g_h), HIST_MAX * 3 - 1);
    CHECK_EQI((int)hist_at(&g_h, 0), HIST_MAX * 2);
    {   /* strictly increasing, which is what a broken wrap breaks */
        int wrong = 0;
        for (i = 1; i < HIST_MAX; i++) {
            if (hist_at(&g_h, i) != hist_at(&g_h, i - 1) + 1) { wrong++; }
        }
        CHECK_EQI(wrong, 0);
    }

    hist_clear(&g_h);
    CHECK_EQI(hist_count(&g_h), 0);
    hist_clear(NULL);
    hist_push(NULL, 5);
    CHECK_EQI(hist_count(NULL), 0);
    CHECK_EQI((int)hist_at(NULL, 0), 0);
    CHECK_EQI((int)hist_max(NULL), 0);
    CHECK_EQI((int)hist_avg(NULL), 0);
    CHECK_EQI((int)hist_newest(NULL), 0);
}

static void h_bars_and_scale(void)
{
    /* A bar never leaves its box, whatever it is handed. */
    CHECK_EQI(hist_bar(50, 100, 40), 20);
    CHECK_EQI(hist_bar(0, 100, 40), 0);
    CHECK_EQI(hist_bar(100, 100, 40), 40);
    CHECK_EQI(hist_bar(500, 100, 40), 40);     /* over the top: clamped */
    CHECK_EQI(hist_bar(-5, 100, 40), 0);
    CHECK_EQI(hist_bar(50, 0, 40), 0);         /* no scale: no bar, no crash */
    CHECK_EQI(hist_bar(50, -1, 40), 0);
    CHECK_EQI(hist_bar(50, 100, 0), 0);
    CHECK_EQI(hist_bar(50, 100, -3), 0);

    /* The axis lands on a number a person reads, and never below the floor. */
    CHECK_EQI((int)hist_scale(0, 10), 10);
    CHECK_EQI((int)hist_scale(7, 10), 10);
    CHECK_EQI((int)hist_scale(11, 10), 20);
    CHECK_EQI((int)hist_scale(30, 10), 50);
    CHECK_EQI((int)hist_scale(60, 10), 100);
    CHECK_EQI((int)hist_scale(100, 10), 100);
    CHECK_EQI((int)hist_scale(101, 10), 200);
    CHECK_EQI((int)hist_scale(4096, 10), 5000);
    CHECK_EQI((int)hist_scale(-5, 10), 10);
    CHECK_EQI((int)hist_scale(3, 0), 5);       /* a zero floor still works */
}

void test_hist(void)
{
    printf("- history\n");
    h_fill_and_wrap();
    h_bars_and_scale();
}
