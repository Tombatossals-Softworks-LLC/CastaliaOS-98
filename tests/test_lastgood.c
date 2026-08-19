/*
 * test_lastgood.c - Last-known-good configuration decisions (lastgood.c).
 *
 * Four booleans go in, so there are exactly sixteen boot cases and eight
 * promote cases -- few enough to walk ALL of them rather than the handful a
 * person would think to write. That matters more than usual here, because
 * both mistakes this code can make are quiet ones: restoring when it should
 * not silently undoes a user's edits, and promoting when it should not
 * enshrines the very file that broke the machine.
 *
 * Two invariants are asserted over the whole table rather than case by case,
 * since they are the properties that actually have to hold:
 *   - a valid live config is ALWAYS used, whatever else is true;
 *   - an unclean session NEVER promotes.
 */
#include "ctest.h"
#include "../src/cfg/lastgood.h"

void test_lastgood(void)
{
    int a, b, c, d;

    printf("- last-known-good config\n");

    /* ---- the whole boot table, all sixteen combinations --------------- */
    for (a = 0; a < 2; a++) {          /* cfg_present    */
        for (b = 0; b < 2; b++) {      /* cfg_valid      */
            for (c = 0; c < 2; c++) {  /* backup_present */
                for (d = 0; d < 2; d++) {  /* prev_dirty */
                    LgAction got = lg_decide((cbool)a, (cbool)b,
                                             (cbool)c, (cbool)d);
                    LgAction want;
                    if (a && b)      { want = LG_USE_CURRENT; }
                    else if (c)      { want = LG_RESTORE; }
                    else             { want = LG_DEFAULTS; }
                    CHECK_EQI((int)got, (int)want);
                    /* A valid live config is used no matter what else is
                     * true -- including after a crash. */
                    if (a && b) { CHECK_EQI((int)got, LG_USE_CURRENT); }
                    /* Nothing usable anywhere can only mean defaults. */
                    if (!(a && b) && !c) { CHECK_EQI((int)got, LG_DEFAULTS); }
                    /* The backup is never reached while the live file is
                     * usable, so a good boot cannot lose the user's edits. */
                    if (a && b) { CHECK(got != LG_RESTORE); }
                }
            }
        }
    }

    /* The cases worth naming, so a future reader sees the intent and not just
     * the loop above. */
    CHECK_EQI((int)lg_decide(CTRUE,  CTRUE,  CTRUE,  CFALSE), LG_USE_CURRENT);
    CHECK_EQI((int)lg_decide(CTRUE,  CTRUE,  CTRUE,  CTRUE),  LG_USE_CURRENT);
    CHECK_EQI((int)lg_decide(CTRUE,  CFALSE, CTRUE,  CFALSE), LG_RESTORE);
    CHECK_EQI((int)lg_decide(CFALSE, CFALSE, CTRUE,  CFALSE), LG_RESTORE);
    CHECK_EQI((int)lg_decide(CTRUE,  CFALSE, CFALSE, CFALSE), LG_DEFAULTS);
    CHECK_EQI((int)lg_decide(CFALSE, CFALSE, CFALSE, CTRUE),  LG_DEFAULTS);
    /* A file that exists but is garbage is exactly the corrupt-INI case the
     * abuse suite feeds it, and it must reach for the backup. */
    CHECK_EQI((int)lg_decide(CTRUE,  CFALSE, CTRUE,  CTRUE),  LG_RESTORE);

    /* ---- the whole promote table, all eight combinations -------------- */
    for (a = 0; a < 2; a++) {          /* cfg_present   */
        for (b = 0; b < 2; b++) {      /* cfg_valid     */
            for (c = 0; c < 2; c++) {  /* session_clean */
                cbool got = lg_should_promote((cbool)a, (cbool)b, (cbool)c);
                cbool want = (c && a && b) ? CTRUE : CFALSE;
                CHECK_EQI((int)got, (int)want);
                /* An unclean session never promotes -- otherwise the config
                 * that was live when the machine went down becomes the one it
                 * falls back to. */
                if (!c) { CHECK_EQI((int)got, (int)CFALSE); }
                /* And an invalid config is never promoted even after a clean
                 * run, or the backup stops being known-good. */
                if (!b) { CHECK_EQI((int)got, (int)CFALSE); }
            }
        }
    }

    CHECK(lg_should_promote(CTRUE, CTRUE, CTRUE));
    CHECK(!lg_should_promote(CTRUE, CTRUE, CFALSE));
    CHECK(!lg_should_promote(CTRUE, CFALSE, CTRUE));
    CHECK(!lg_should_promote(CFALSE, CFALSE, CTRUE));
}
