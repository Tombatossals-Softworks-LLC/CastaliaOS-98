/*
 * lastgood.c - Last-known-good configuration decisions (see lastgood.h).
 *
 * Two functions, no I/O, no state. Everything they need is a fact the caller
 * has already established by looking at the disk.
 */
#include "castalia/ctypes.h"
#include "lastgood.h"

LgAction lg_decide(cbool cfg_present, cbool cfg_valid,
                   cbool backup_present, cbool prev_dirty)
{
    /* A usable config is used, full stop -- including after a crash. A crash
     * is not evidence that the config caused it, and reverting a user's
     * settings every time an unrelated subsystem falls over would lose more
     * than it saves. The dirty flag has its own job (offering safe mode); it
     * is not a reason to throw away a file that parses. */
    if (cfg_present && cfg_valid) {
        CASTALIA_UNUSED(prev_dirty);
        return LG_USE_CURRENT;
    }
    /* Missing or unusable. If something known-good was kept, that is strictly
     * better than defaults: it is what the user last had working. */
    if (backup_present) { return LG_RESTORE; }
    return LG_DEFAULTS;
}

cbool lg_should_promote(cbool cfg_present, cbool cfg_valid, cbool session_clean)
{
    /* Promoting after an unclean session is the one mistake that makes this
     * whole mechanism worse than not having it: the config that was live when
     * the machine went down becomes the config it falls back to. */
    if (!session_clean) { return CFALSE; }
    return (cfg_present && cfg_valid) ? CTRUE : CFALSE;
}
