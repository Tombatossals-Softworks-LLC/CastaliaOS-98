/*
 * cal_core.h - The calendar arithmetic, in one place.
 *
 * Leap years and month lengths were written twice: once in agenda_core.c and
 * once in app_clock.c, a few lines apart in spirit and not quite the same in
 * fact -- asked for the length of month 13, one answered 0 and the other 30.
 * A calendar that reports thirty days in a month that does not exist is the
 * more dangerous of the two, because it is the answer that looks usable.
 *
 * This is the third caller's fault as much as anybody's: the Control Center's
 * Date and Time panel needs exactly the same four functions, and a system
 * where "how long is February" has two answers is one where it is about to
 * have three. So: one copy, and it is tested.
 *
 * Everything here is integer arithmetic over a proleptic Gregorian calendar.
 * Nothing reads a clock, allocates, or draws.
 */
#ifndef CASTALIA_CAL_CORE_H
#define CASTALIA_CAL_CORE_H

#include "castalia/ctypes.h"

/*
 * The range this system will accept as a date.
 *
 * The floor is DOS's: INT 21h will not set a date before 1980, so a system
 * that let somebody type 1979 would take the value, fail silently at the
 * platform layer and show the old date back. The ceiling is where DOS's own
 * date services stop.
 */
#define CAL_YEAR_MIN 1980
#define CAL_YEAR_MAX 2099

/* 1 for a leap year, 0 otherwise. Gregorian: divisible by four, except
 * centuries, except those divisible by four hundred -- so 1900 is not a leap
 * year and 2000 is, which is the pair that catches a wrong implementation. */
int cal_leap(int year);

/*
 * Days in a month, or 0 when there is no such month.
 *
 * Zero rather than a plausible thirty: the caller has asked about something
 * that does not exist, and the only useful reply is one that cannot be
 * mistaken for a length.
 */
int cal_days_in_month(int year, int month);

/*
 * Weekday of a date -- 0 Sunday through 6 Saturday -- or -1 if the date is
 * not real. Sakamoto's method, which needs no table beyond twelve small
 * numbers and no division beyond the century corrections.
 */
int cal_weekday(int year, int month, int day);

/* Whether this is a real date inside the range above. */
cbool cal_valid(int year, int month, int day);

/*
 * The nearest real day in that month: what a date picker needs when the month
 * changes underneath a day that no longer exists. Standing on 31 January and
 * moving to February gives the 28th, or the 29th in a leap year, rather than
 * an invalid date or a silent jump into March.
 */
int cal_clamp_day(int year, int month, int day);

#endif /* CASTALIA_CAL_CORE_H */
