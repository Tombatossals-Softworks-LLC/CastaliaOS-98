/*
 * test_agenda.c - Appointment store (agenda_core.c): ordering, day queries,
 * validation, removal, capacity, and the plain-text round trip.
 * Pure logic, no gfx/wm.
 */
#include "ctest.h"
#include "agenda_core.h"

static Agenda g_a;

static void ag_ordering(void)
{
    int idx[8], n;
    const AgEvent *e;

    ag_clear(&g_a);
    CHECK_EQI(ag_count(&g_a), 0);

    /* Added out of order on purpose: the store must sort them. */
    CHECK(ag_add(&g_a, 2026, 8, 14, 17, 0, "Late meeting"));
    CHECK(ag_add(&g_a, 2026, 8, 14, 9, 30, "Dentist"));
    CHECK(ag_add(&g_a, 2026, 8, 13, 12, 0, "Yesterday lunch"));
    CHECK(ag_add(&g_a, 2026, 8, 14, AG_ALLDAY, 0, "Pay the bill"));
    CHECK_EQI(ag_count(&g_a), 4);

    /* The whole list is in date then time order, earlier day first. */
    e = ag_get(&g_a, 0);
    CHECK_EQI(e->d, 13);
    /* On the 14th: the all-day entry leads, then 09:30, then 17:00. */
    n = ag_day_events(&g_a, 2026, 8, 14, idx, 8);
    CHECK_EQI(n, 3);
    CHECK_EQI(ag_get(&g_a, idx[0])->hh, AG_ALLDAY);
    CHECK_STR(ag_get(&g_a, idx[1])->text, "Dentist");
    CHECK_STR(ag_get(&g_a, idx[2])->text, "Late meeting");

    /* Day queries only see their own day. */
    CHECK_EQI(ag_count_on(&g_a, 2026, 8, 14), 3);
    CHECK_EQI(ag_count_on(&g_a, 2026, 8, 13), 1);
    CHECK_EQI(ag_count_on(&g_a, 2026, 8, 15), 0);
    CHECK_EQI(ag_count_on(&g_a, 2027, 8, 14), 0);   /* same day, other year */
    CHECK_EQI(ag_count_on(&g_a, 2026, 9, 14), 0);   /* same day, other month */
}

static void ag_validation(void)
{
    ag_clear(&g_a);
    /* Impossible dates and times are refused rather than stored wrong. */
    CHECK(!ag_add(&g_a, 2026, 2, 30, 9, 0, "Never"));
    CHECK(!ag_add(&g_a, 2026, 13, 1, 9, 0, "Never"));
    CHECK(!ag_add(&g_a, 2026, 0, 1, 9, 0, "Never"));
    CHECK(!ag_add(&g_a, 2026, 1, 0, 9, 0, "Never"));
    CHECK(!ag_add(&g_a, 2026, 1, 1, 24, 0, "Never"));
    CHECK(!ag_add(&g_a, 2026, 1, 1, 9, 60, "Never"));
    CHECK(!ag_add(&g_a, 2026, 1, 1, 9, 0, ""));        /* empty text */
    CHECK(!ag_add(&g_a, 2026, 1, 1, 9, 0, NULL));
    CHECK(!ag_add(NULL, 2026, 1, 1, 9, 0, "x"));
    CHECK_EQI(ag_count(&g_a), 0);

    /* Leap day exists in 2028 but not in 2026. */
    CHECK(ag_add(&g_a, 2028, 2, 29, 9, 0, "Leap day"));
    CHECK(!ag_add(&g_a, 2026, 2, 29, 9, 0, "No such day"));
    CHECK_EQI(ag_count(&g_a), 1);
}

static void ag_removal_and_capacity(void)
{
    int i;
    ag_clear(&g_a);
    ag_add(&g_a, 2026, 1, 1, 8, 0, "first");
    ag_add(&g_a, 2026, 1, 1, 9, 0, "second");
    ag_add(&g_a, 2026, 1, 1, 10, 0, "third");

    CHECK(ag_remove(&g_a, 1));
    CHECK_EQI(ag_count(&g_a), 2);
    CHECK_STR(ag_get(&g_a, 0)->text, "first");
    CHECK_STR(ag_get(&g_a, 1)->text, "third");   /* the gap closed */

    /* Out-of-range removals change nothing. */
    CHECK(!ag_remove(&g_a, -1));
    CHECK(!ag_remove(&g_a, 2));
    CHECK(!ag_remove(NULL, 0));
    CHECK_EQI(ag_count(&g_a), 2);
    CHECK(ag_get(&g_a, 99) == NULL);

    /* The store fills up and then refuses politely instead of overflowing. */
    ag_clear(&g_a);
    {   /* one assertion for the fill, not one per event */
        int refused = 0;
        for (i = 0; i < AG_MAX_EVENTS; i++) {
            if (!ag_add(&g_a, 2026, 1, 1, i % 24, i % 60, "x")) { refused++; }
        }
        CHECK_EQI(refused, 0);
    }
    CHECK_EQI(ag_count(&g_a), AG_MAX_EVENTS);
    CHECK(!ag_add(&g_a, 2026, 1, 2, 9, 0, "one too many"));
    CHECK_EQI(ag_count(&g_a), AG_MAX_EVENTS);
}

static void ag_roundtrip(void)
{
    static char buf[AG_FILE_MAX];
    Agenda back;
    char tb[8];
    int n;

    ag_clear(&g_a);
    ag_add(&g_a, 2026, 8, 14, 9, 30, "Dentist at the clinic");
    ag_add(&g_a, 2026, 8, 14, AG_ALLDAY, 0, "Pay the hosting bill");
    ag_add(&g_a, 2026, 12, 25, 0, 0, "Christmas");

    n = ag_serialize(&g_a, buf, (int)sizeof buf);
    CHECK(n > 0);
    CHECK(n < AG_FILE_MAX);              /* the bound really is the worst case */

    CHECK(ag_parse(&back, buf, NULL));
    CHECK_EQI(ag_count(&back), 3);
    /* Text with spaces survives, and so does the all-day marker. */
    CHECK_STR(ag_get(&back, 0)->text, "Pay the hosting bill");
    CHECK_EQI(ag_get(&back, 0)->hh, AG_ALLDAY);
    CHECK_STR(ag_get(&back, 1)->text, "Dentist at the clinic");
    CHECK_EQI(ag_get(&back, 1)->hh, 9);
    CHECK_EQI(ag_get(&back, 1)->mm, 30);
    CHECK_EQI(ag_get(&back, 2)->m, 12);

    ag_fmt_time(ag_get(&back, 1), tb, (int)sizeof tb);
    CHECK_STR(tb, "09:30");
    ag_fmt_time(ag_get(&back, 0), tb, (int)sizeof tb);
    CHECK_STR(tb, "--:--");

    /* Parsing is forgiving: junk lines are skipped, not stored as events. */
    CHECK(ag_parse(&back, "not an event\n2026-08-14 09:30 Real one\n\n#note\n", NULL));
    CHECK_EQI(ag_count(&back), 1);
    CHECK_STR(ag_get(&back, 0)->text, "Real one");

    /* An empty store round-trips to an empty file. */
    ag_clear(&g_a);
    CHECK_EQI(ag_serialize(&g_a, buf, (int)sizeof buf), 0);
    CHECK(ag_parse(&back, buf, NULL));
    CHECK_EQI(ag_count(&back), 0);
}

void test_agenda(void)
{
    printf("- agenda\n");
    ag_ordering();
    ag_validation();
    ag_removal_and_capacity();
    ag_roundtrip();

    /*
     * A file holding more than the calendar can is REPORTED, not silently
     * halved. ag_parse used to throw away ag_add's answer, so an agenda with
     * more than AG_MAX_EVENTS loaded its first 128 and the Clock's next save
     * wrote those back over all of them. Nothing looked wrong: the events it
     * did have were real ones.
     */
    {
        static char big[AG_FILE_MAX * 2];
        Agenda over;
        int dropped = -1, i2, o = 0;
        for (i2 = 0; i2 < AG_MAX_EVENTS + 5; i2++) {
            o += sys_snprintf(big + o, sizeof(big) - (cu32)o,
                              "2026-08-%02d %02d:%02d Event %d\n",
                              1 + (i2 % 28), i2 % 24, i2 % 60, i2);
        }
        big[o] = '\0';
        CHECK(!ag_parse(&over, big, &dropped));
        CHECK_EQI(dropped, 5);
        CHECK_EQI(over.count, AG_MAX_EVENTS);

        /* ...and one that fits reports nothing dropped, so the check above
         * is about THIS file rather than about any file at all. */
        o = 0;
        for (i2 = 0; i2 < 10; i2++) {
            o += sys_snprintf(big + o, sizeof(big) - (cu32)o,
                              "2026-08-%02d 09:00 Event %d\n",
                              1 + (i2 % 28), i2);
        }
        big[o] = '\0';
        dropped = -1;
        CHECK(ag_parse(&over, big, &dropped));
        CHECK_EQI(dropped, 0);
        CHECK_EQI(over.count, 10);

        /*
         * Only "did not FIT" counts. A comment, a line of prose, or an event
         * with a MALFORMED DATE are all broken lines rather than lost
         * appointments -- and counting them would make the Clock refuse to
         * save over a perfectly good agenda for ever because of one typo.
         */
        dropped = -1;
        CHECK(ag_parse(&over, "#a note\nnot an event\n2026-08-14 09:30 Real\n",
                       &dropped));
        CHECK_EQI(dropped, 0);
        CHECK_EQI(over.count, 1);
        dropped = -1;
        CHECK(ag_parse(&over, "2026-99-99 09:30 Bad date\n"
                              "2026-08-14 09:30 Real\n", &dropped));
        CHECK_EQI(dropped, 0);
        CHECK_EQI(over.count, 1);
    }
}
