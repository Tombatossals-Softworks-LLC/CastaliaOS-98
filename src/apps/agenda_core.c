/*
 * agenda_core.c - Appointment store (pure logic, host-tested).
 *
 * See agenda_core.h. The list is kept sorted on insert, so every query is a
 * scan and the UI never has to sort.
 */
#include "agenda_core.h"
#include "../sys/cal_core.h"
#include "castalia/sys.h"

static cbool ag_valid(int y, int m, int d, int hh, int mm)
{
    if (y < 1900 || y > 2999) { return CFALSE; }
    if (m < 1 || m > 12) { return CFALSE; }
    if (d < 1 || d > cal_days_in_month(y, m)) { return CFALSE; }
    if (hh == AG_ALLDAY) { return CTRUE; }
    if (hh < 0 || hh > 23) { return CFALSE; }
    if (mm < 0 || mm > 59) { return CFALSE; }
    return CTRUE;
}

/* Sort key: date first, then time. All-day (-1) sorts before any clock time,
 * which is where a reader expects it on the day's list. */
static long ag_key(const AgEvent *e)
{
    long day = ((long)e->y * 12 + (e->m - 1)) * 31 + (e->d - 1);
    long min = (e->hh == AG_ALLDAY) ? -1 : (long)e->hh * 60 + e->mm;
    return day * 1441 + (min + 1);
}

void ag_clear(Agenda *a)
{
    if (a != NULL) { a->count = 0; }
}

int ag_count(const Agenda *a) { return (a == NULL) ? 0 : a->count; }

const AgEvent *ag_get(const Agenda *a, int index)
{
    if (a == NULL || index < 0 || index >= a->count) { return NULL; }
    return &a->ev[index];
}

cbool ag_add(Agenda *a, int y, int m, int d, int hh, int mm, const char *text)
{
    AgEvent e;
    long k;
    int i, j;
    if (a == NULL || text == NULL || text[0] == '\0') { return CFALSE; }
    if (a->count >= AG_MAX_EVENTS) { return CFALSE; }
    if (!ag_valid(y, m, d, hh, mm)) { return CFALSE; }

    e.y = y; e.m = m; e.d = d;
    e.hh = (hh == AG_ALLDAY) ? AG_ALLDAY : hh;
    e.mm = (hh == AG_ALLDAY) ? 0 : mm;
    sys_strlcpy(e.text, text, (cu32)AG_TEXT);

    k = ag_key(&e);
    for (i = 0; i < a->count; i++) {
        if (ag_key(&a->ev[i]) > k) { break; }
    }
    for (j = a->count; j > i; j--) { a->ev[j] = a->ev[j - 1]; }
    a->ev[i] = e;
    a->count++;
    return CTRUE;
}

cbool ag_remove(Agenda *a, int index)
{
    int i;
    if (a == NULL || index < 0 || index >= a->count) { return CFALSE; }
    for (i = index; i < a->count - 1; i++) { a->ev[i] = a->ev[i + 1]; }
    a->count--;
    return CTRUE;
}

int ag_count_on(const Agenda *a, int y, int m, int d)
{
    int i, n = 0;
    if (a == NULL) { return 0; }
    for (i = 0; i < a->count; i++) {
        if (a->ev[i].y == y && a->ev[i].m == m && a->ev[i].d == d) { n++; }
    }
    return n;
}

int ag_day_events(const Agenda *a, int y, int m, int d, int *out, int max)
{
    int i, n = 0;
    if (a == NULL || out == NULL || max <= 0) { return 0; }
    for (i = 0; i < a->count && n < max; i++) {
        if (a->ev[i].y == y && a->ev[i].m == m && a->ev[i].d == d) {
            out[n++] = i;      /* the list is sorted, so these come in order */
        }
    }
    return n;
}

void ag_fmt_time(const AgEvent *e, char *out, int outsz)
{
    if (out == NULL || outsz <= 0) { return; }
    if (e == NULL) { out[0] = '\0'; return; }
    if (e->hh == AG_ALLDAY) { sys_strlcpy(out, "--:--", (cu32)outsz); return; }
    sys_snprintf(out, (cu32)outsz, "%02d:%02d", e->hh, e->mm);
}

/* ---- serialization ---------------------------------------------------- */
static int ag_put(char *out, int outsz, int at, const char *t)
{
    int i = 0;
    while (t[i] != '\0') {
        if (at < outsz - 1) { out[at] = t[i]; }
        at++;
        i++;
    }
    return at;
}

int ag_serialize(const Agenda *a, char *out, int outsz)
{
    int i, at = 0;
    char line[AG_TEXT + 24], tb[8];
    if (a == NULL || out == NULL || outsz <= 0) { return 0; }
    for (i = 0; i < a->count; i++) {
        const AgEvent *e = &a->ev[i];
        ag_fmt_time(e, tb, (int)sizeof tb);
        sys_snprintf(line, sizeof line, "%04d-%02d-%02d %s %s\n",
                     e->y, e->m, e->d, tb, e->text);
        at = ag_put(out, outsz, at, line);
    }
    out[(at < outsz) ? at : outsz - 1] = '\0';
    return at;
}

static int ag_num(const char *s, int n)
{
    int v = 0, i;
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') { return -1; }
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

cbool ag_parse(Agenda *a, const char *src, int *dropped)
{
    int lost = 0;
    if (dropped != NULL) { *dropped = 0; }
    if (a == NULL || src == NULL) { return CFALSE; }
    ag_clear(a);
    while (*src != '\0') {
        char text[AG_TEXT];
        int y, m, d, hh, mm, n = 0;
        const char *line = src;
        while (*src != '\0' && *src != '\n') { src++; }
        /* "YYYY-MM-DD HH:MM text" -- anything shorter is not an event */
        if (src - line >= 17 && line[4] == '-' && line[7] == '-') {
            y = ag_num(line, 4);
            m = ag_num(line + 5, 2);
            d = ag_num(line + 8, 2);
            if (line[11] == '-') { hh = AG_ALLDAY; mm = 0; }
            else { hh = ag_num(line + 11, 2); mm = ag_num(line + 14, 2); }
            if (y > 0 && m > 0 && d > 0) {
                const char *t = line + 17;
                while (*t == ' ') { t++; }
                while (t < src && n < AG_TEXT - 1) { text[n++] = *t++; }
                text[n] = '\0';
                if (n > 0) {
                    /*
                     * Only "did not FIT" counts as dropped. A malformed date
                     * is a broken line, not a lost appointment, and counting
                     * it would make the Clock refuse to save over the file
                     * for ever because of one typo in it.
                     */
                    if (a->count >= AG_MAX_EVENTS) { lost++; }
                    else { ag_add(a, y, m, d, hh, mm, text); }
                }
            }
        }
        if (*src == '\n') { src++; }
    }
    if (dropped != NULL) { *dropped = lost; }
    return (lost == 0) ? CTRUE : CFALSE;
}
