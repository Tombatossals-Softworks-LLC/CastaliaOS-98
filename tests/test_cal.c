/*
 * test_cal.c - Calendar arithmetic (cal_core.c).
 *
 * This code was written twice before it was written once, and the two copies
 * disagreed: asked how long month 13 is, agenda_core said 0 and app_clock said
 * 30. So the checks below are less about February than about the answers a
 * calendar gives when the question is wrong, which is where both copies were
 * going to be used from a third place that had not been written yet.
 *
 * The leap-year rule is checked at the two years that separate a correct
 * implementation from the common one: 1900 is NOT a leap year and 2000 IS.
 * Anything that only tests "divisible by four" passes on both counts and is
 * wrong for eight decades either side of them.
 */
#include "ctest.h"
#include "../src/sys/cal_core.h"

static void ct_leap(void)
{
    /* The ordinary rule. */
    CHECK_EQI(cal_leap(2024), 1);
    CHECK_EQI(cal_leap(2023), 0);
    CHECK_EQI(cal_leap(1996), 1);

    /* The two that matter. A century is not a leap year unless it divides by
     * four hundred -- get this wrong and every date after February 1900 or
     * 2100 is off by a day. */
    CHECK_EQI(cal_leap(1900), 0);
    CHECK_EQI(cal_leap(2000), 1);
    CHECK_EQI(cal_leap(2100), 0);
    CHECK_EQI(cal_leap(2400), 1);
}

static void ct_month_lengths(void)
{
    CHECK_EQI(cal_days_in_month(2023, 1), 31);
    CHECK_EQI(cal_days_in_month(2023, 4), 30);
    CHECK_EQI(cal_days_in_month(2023, 12), 31);

    CHECK_EQI(cal_days_in_month(2023, 2), 28);
    CHECK_EQI(cal_days_in_month(2024, 2), 29);
    CHECK_EQI(cal_days_in_month(1900, 2), 28);
    CHECK_EQI(cal_days_in_month(2000, 2), 29);

    /* A month that does not exist has no length. Thirty was the other copy's
     * answer, and it is the dangerous one: it looks like a measurement. */
    CHECK_EQI(cal_days_in_month(2023, 0), 0);
    CHECK_EQI(cal_days_in_month(2023, 13), 0);
    CHECK_EQI(cal_days_in_month(2023, -1), 0);
    CHECK_EQI(cal_days_in_month(2023, 99), 0);
}

static void ct_weekday(void)
{
    /* Dates whose weekday is checkable against something outside this file. */
    CHECK_EQI(cal_weekday(2000, 1, 1), 6);    /* Saturday */
    CHECK_EQI(cal_weekday(1999, 12, 31), 5);  /* Friday   */
    CHECK_EQI(cal_weekday(2024, 2, 29), 4);   /* Thursday */
    CHECK_EQI(cal_weekday(1981, 8, 12), 3);   /* Wednesday: the IBM PC       */
    CHECK_EQI(cal_weekday(2026, 8, 18), 2);   /* Tuesday                     */

    /* Every weekday appears across one week, in order, with no repeats --
     * which a formula that was subtly wrong could not manage. */
    {
        int i, seen[7], d;
        for (i = 0; i < 7; i++) { seen[i] = 0; }
        for (d = 1; d <= 7; d++) {
            int w = cal_weekday(2024, 7, d);
            CHECK(w >= 0 && w < 7);
            if (w >= 0 && w < 7) { seen[w]++; }
        }
        for (i = 0; i < 7; i++) { CHECK_EQI(seen[i], 1); }
    }

    /* The day after the 28th of a non-leap February is March, so the weekday
     * has to run continuously across the join. */
    CHECK_EQI((cal_weekday(2023, 2, 28) + 1) % 7, cal_weekday(2023, 3, 1));
    CHECK_EQI((cal_weekday(2024, 2, 28) + 1) % 7, cal_weekday(2024, 2, 29));
    CHECK_EQI((cal_weekday(2024, 2, 29) + 1) % 7, cal_weekday(2024, 3, 1));
    /* ...and across a year end. */
    CHECK_EQI((cal_weekday(2023, 12, 31) + 1) % 7, cal_weekday(2024, 1, 1));

    /* A date that is not real has no weekday, rather than a plausible one. */
    CHECK_EQI(cal_weekday(2023, 2, 29), -1);
    CHECK_EQI(cal_weekday(2023, 13, 1), -1);
    CHECK_EQI(cal_weekday(2023, 4, 31), -1);
    CHECK_EQI(cal_weekday(2023, 1, 0), -1);
}

static void ct_valid(void)
{
    CHECK(cal_valid(2026, 8, 18) == CTRUE);
    CHECK(cal_valid(2024, 2, 29) == CTRUE);
    CHECK(cal_valid(2023, 2, 29) == CFALSE);
    CHECK(cal_valid(2023, 4, 31) == CFALSE);
    CHECK(cal_valid(2023, 0, 1) == CFALSE);
    CHECK(cal_valid(2023, 1, 0) == CFALSE);

    /* The range is DOS's, not an opinion: INT 21h will not set a date before
     * 1980, so accepting one here would mean taking a value, failing at the
     * platform layer and showing the old date back with no explanation. */
    CHECK(cal_valid(1980, 1, 1) == CTRUE);
    CHECK(cal_valid(1979, 12, 31) == CFALSE);
    CHECK(cal_valid(2099, 12, 31) == CTRUE);
    CHECK(cal_valid(2100, 1, 1) == CFALSE);
}

static void ct_clamp(void)
{
    /* The date-picker case: standing on the 31st and moving to a month that
     * has no 31st. */
    CHECK_EQI(cal_clamp_day(2023, 2, 31), 28);
    CHECK_EQI(cal_clamp_day(2024, 2, 31), 29);
    CHECK_EQI(cal_clamp_day(2023, 4, 31), 30);
    /* A day that already fits is left exactly alone. */
    CHECK_EQI(cal_clamp_day(2023, 1, 31), 31);
    CHECK_EQI(cal_clamp_day(2023, 6, 15), 15);
    /* Below the start of the month comes back to the first. */
    CHECK_EQI(cal_clamp_day(2023, 6, 0), 1);
    CHECK_EQI(cal_clamp_day(2023, 6, -9), 1);
    /* No such month, no such day -- not "the 1st of nowhere". */
    CHECK_EQI(cal_clamp_day(2023, 13, 5), 0);
    CHECK_EQI(cal_clamp_day(2023, 0, 5), 0);
}

void test_cal(void)
{
    printf("- calendar arithmetic\n");
    ct_leap();
    ct_month_lengths();
    ct_weekday();
    ct_valid();
    ct_clamp();
}
