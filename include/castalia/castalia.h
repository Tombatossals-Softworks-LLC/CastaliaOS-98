/*
 * castalia.h - Umbrella header and product identity for CastaliaOS 98 PE.
 *
 * CastaliaOS 98 PE ("Powerful Edition") is an original, legally clean,
 * Win9x-inspired retro desktop environment. It is NOT Microsoft Windows and
 * contains no Microsoft code, assets, or branding. See LEGAL.md.
 *
 * Include this to pull the full public surface of the runtime. Individual
 * subsystem headers may also be included directly.
 */
#ifndef CASTALIA_H
#define CASTALIA_H

#include "castalia/ctypes.h"

/* ---- Product identity ------------------------------------------------- */
#define CASTALIA_NAME        "CastaliaOS 98 PE"
#define CASTALIA_CODENAME    "Castalia 98"        /* historical: "SE" draft */
#define CASTALIA_EDITION     "Powerful Edition"
#define CASTALIA_VENDOR      "Castalia Project"

#define CASTALIA_VER_MAJOR   0
#define CASTALIA_VER_MINOR   1
#define CASTALIA_VER_PATCH   0
#define CASTALIA_VER_STRING  "0.1.0"
#define CASTALIA_VER_STAGE   "MVP / Phase 1"

/* A single 32-bit version number for comparisons: 0x00MMmmpp. */
#define CASTALIA_VERSION \
    (((cu32)CASTALIA_VER_MAJOR << 16) | \
     ((cu32)CASTALIA_VER_MINOR << 8)  | \
      (cu32)CASTALIA_VER_PATCH)

/* Fixed limits live in ctypes.h so every subsystem header can use them
 * (CASTALIA_MAX_WINDOWS / CASTALIA_MAX_PATH / CASTALIA_MAX_NAME). */

#include "castalia/sys.h"
#include "castalia/cfg.h"
#include "castalia/rect.h"
#include "castalia/gfx.h"
#include "castalia/plat.h"
#include "castalia/wm.h"
#include "castalia/ui.h"
#include "castalia/shell.h"

/* ---------------------------------------------------------------------- */
/* Memory budget                                                          */
/* ---------------------------------------------------------------------- */
/*
 * The hard budget the whole design answers to, and the ceiling an IDLE desktop
 * must stay under. Both are checked by --mem-check (run by `make demos`), so
 * they are limits rather than aspirations.
 *
 * Idle is currently a little over 3.7 MB, and almost all of it is two
 * 800x600x32 surfaces at 1875 KB each: the back buffer everything renders
 * into, and the desktop cache that makes a steady-state repaint a row copy
 * instead of a per-pixel gradient. The ceiling leaves room for the shell's own
 * allocations and deliberately NOT enough for a third full-screen surface --
 * that is the mistake worth catching, and it is how the documented figure
 * silently went from true to half-true the last time.
 */
#define CASTALIA_BUDGET_KB   8192   /* hard ceiling, peak                   */
#define CASTALIA_IDLE_KB_MAX 4096   /* an idle desktop must fit in this      */

#endif /* CASTALIA_H */
