/*
 * lastgood.h - Last-known-good configuration: the decision, not the plumbing.
 *
 * `make abuse` proves the shell survives a corrupted CASTALIA.INI. What it
 * survives INTO is a desktop built from defaults -- which is to say every
 * setting the user ever chose is gone, silently, because one file got a bad
 * write. Surviving is not the same as not losing anything.
 *
 * So: once a session has booted and run cleanly, the config that produced it
 * is copied aside as known-good. If a later boot finds the live config
 * unusable -- unparseable, empty of anything recognisable, or simply missing
 * after the previous session died -- the known-good copy comes back instead of
 * the defaults.
 *
 * The part that is easy to get wrong is not the copying, it is deciding WHEN.
 * Restore too eagerly and you undo edits the user meant; promote too eagerly
 * and you enshrine the broken file you were supposed to protect them from.
 * That decision is the whole of this header, expressed as two pure functions
 * over facts the caller has already established, so tests/test_lastgood.c can
 * walk every combination rather than the handful a person thinks to try.
 */
#ifndef CASTALIA_LASTGOOD_H
#define CASTALIA_LASTGOOD_H

#include "castalia/ctypes.h"

typedef enum {
    LG_USE_CURRENT = 0,  /* the live config is fine: boot on it            */
    LG_RESTORE,          /* put the known-good copy back, boot on that     */
    LG_DEFAULTS          /* nothing usable anywhere: boot on defaults      */
} LgAction;

/*
 * What to do at boot.
 *
 *   cfg_present   - a live config file exists at all
 *   cfg_valid     - ...and it parsed into something recognisable
 *   backup_present- a known-good copy exists
 *   prev_dirty    - the previous session did not shut down cleanly
 *
 * A valid live config is used even when the previous session crashed: a crash
 * is not evidence that the CONFIG was at fault, and silently reverting a
 * user's settings every time something else falls over would be its own bug.
 * The backup is for when the live file cannot be used at all.
 */
LgAction lg_decide(cbool cfg_present, cbool cfg_valid,
                   cbool backup_present, cbool prev_dirty);

/*
 * Whether the live config should be promoted to the known-good copy now that
 * a session has ended. Only a clean session promotes, and only a config that
 * is actually there and actually valid -- promoting after a crash is how the
 * file that caused the crash becomes the file you fall back to.
 */
cbool lg_should_promote(cbool cfg_present, cbool cfg_valid, cbool session_clean);

#endif /* CASTALIA_LASTGOOD_H */
