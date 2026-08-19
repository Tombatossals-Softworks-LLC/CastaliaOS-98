/*
 * cal_core.c - Calendar arithmetic (see cal_core.h). Integers only.
 */
#include "cal_core.h"

int cal_leap(int year)
{
    return (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;
}

int cal_days_in_month(int year, int month)
{
    static const int D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12) { return 0; }
    if (month == 2) { return 28 + cal_leap(year); }
    return D[month - 1];
}

int cal_weekday(int year, int month, int day)
{
    static const int T[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    int y = year;
    if (month < 1 || month > 12) { return -1; }
    if (day < 1 || day > cal_days_in_month(year, month)) { return -1; }
    /* January and February are counted as months 13 and 14 of the previous
     * year, which is what lets the leap day sit at the end. */
    if (month < 3) { y -= 1; }
    return (y + y / 4 - y / 100 + y / 400 + T[month - 1] + day) % 7;
}

cbool cal_valid(int year, int month, int day)
{
    if (year < CAL_YEAR_MIN || year > CAL_YEAR_MAX) { return CFALSE; }
    if (month < 1 || month > 12) { return CFALSE; }
    if (day < 1 || day > cal_days_in_month(year, month)) { return CFALSE; }
    return CTRUE;
}

int cal_clamp_day(int year, int month, int day)
{
    int dim = cal_days_in_month(year, month);
    if (dim == 0) { return 0; }        /* no such month: no such day */
    if (day < 1) { return 1; }
    if (day > dim) { return dim; }
    return day;
}
