/*
 * agenda_core.h - Appointments for the Clock & Calendar (no gfx/wm/platform).
 *
 * The store behind the agenda panel, kept pure so the host unit tests can
 * exercise it hermetically (tests/test_agenda.c) -- the same split the other
 * apps use.
 *
 * Events are held in one list kept sorted by date and then by time, so a day's
 * appointments come out chronologically without the UI sorting anything. An
 * all-day event (no time) sorts before the timed ones on its day, which is
 * where you expect to read it.
 *
 * Storage is one plain line per event so a file can be fixed in any editor:
 *
 *   2026-08-14 09:30 Dentist
 *   2026-08-14 --:-- Pay the hosting bill
 */
#ifndef CASTALIA_AGENDA_CORE_H
#define CASTALIA_AGENDA_CORE_H

#include "castalia/ctypes.h"

#define AG_MAX_EVENTS 128
#define AG_TEXT       40     /* event text, with the NUL                  */
#define AG_ALLDAY     (-1)   /* pass as 'hh' for an event with no time    */

typedef struct {
    int  y, m, d;            /* date                                      */
    int  hh, mm;             /* time, or AG_ALLDAY                        */
    char text[AG_TEXT];
} AgEvent;

typedef struct {
    AgEvent ev[AG_MAX_EVENTS];
    int     count;
} Agenda;

void ag_clear(Agenda *a);

/* Add an event, inserted in date/time order. Rejects impossible dates, empty
 * text and a full store; returns CTRUE when it was stored. */
cbool ag_add(Agenda *a, int y, int m, int d, int hh, int mm, const char *text);

/* Remove by index into the whole list. CTRUE when something was removed. */
cbool ag_remove(Agenda *a, int index);

int  ag_count(const Agenda *a);
const AgEvent *ag_get(const Agenda *a, int index);

/* How many events fall on a day, and their indices in chronological order.
 * ag_day_events returns how many indices it wrote. */
int  ag_count_on(const Agenda *a, int y, int m, int d);
int  ag_day_events(const Agenda *a, int y, int m, int d, int *out, int max);

/* Format one event's time as "09:30" or "--:--" (all day). */
void ag_fmt_time(const AgEvent *e, char *out, int outsz);

/* Plain-text round trip. ag_serialize returns the length the text NEEDS, which
 * is >= outsz when it was truncated -- check before writing it out.
 * AG_FILE_MAX is the worst case, so a buffer that size can never truncate. */
#define AG_FILE_MAX (AG_MAX_EVENTS * (AG_TEXT + 20) + 16)
int   ag_serialize(const Agenda *a, char *out, int outsz);
/*
 * Parse an agenda file. Returns CTRUE only when EVERY event in it was taken.
 *
 * It used to discard ag_add's answer, so a file holding more than
 * AG_MAX_EVENTS loaded the first 128 and the Clock's next save wrote those
 * 128 back over all of them. Nothing said a word: the calendar looked right,
 * because the events it had were real.
 *
 * 'dropped' (may be NULL) receives how many did not fit.
 */
cbool ag_parse(Agenda *a, const char *src, int *dropped);

#endif /* CASTALIA_AGENDA_CORE_H */
