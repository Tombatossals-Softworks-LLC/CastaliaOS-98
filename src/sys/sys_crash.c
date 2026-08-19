/*
 * sys_crash.c - Fatal error boundary, crash log, and boot-dirty flag.
 *
 * When a subsystem hits an unrecoverable state it calls sys_fatal(). We:
 *   1. Write a crash record to the crash log (append).
 *   2. Set a persistent "boot dirty" flag file that CBOOT checks on the next
 *      boot to offer safe mode (Bible: "If boot fails twice, offer safe mode").
 *   3. Invoke the installed handler (the shell installs one that paints a
 *      recoverable dialog and drops to DOS). With no handler, we abort.
 *
 * A clean session calls sys_crash_mark_clean() to clear the flag once the
 * desktop has been stable for a bit.
 */
#include "castalia/sys.h"

#include <stdio.h>
#include <stdlib.h>

#define CRASH_PATH_MAX 260

static char       g_crashlog[CRASH_PATH_MAX];
static char       g_dirty[CRASH_PATH_MAX];
static cbool      g_have_crashlog = CFALSE;
static cbool      g_have_dirty    = CFALSE;
static SysCrashFn g_handler       = NULL;
static void      *g_handler_user  = NULL;
static cbool      g_prev_dirty    = CFALSE;

CResult sys_crash_init(const char *crashlog_path, const char *dirty_flag_path)
{
    g_have_crashlog = CFALSE;
    g_have_dirty    = CFALSE;
    g_prev_dirty    = CFALSE;

    if (crashlog_path != NULL && crashlog_path[0] != '\0') {
        sys_strlcpy(g_crashlog, crashlog_path, sizeof(g_crashlog));
        g_have_crashlog = CTRUE;
    }
    if (dirty_flag_path != NULL && dirty_flag_path[0] != '\0') {
        FILE *f;
        sys_strlcpy(g_dirty, dirty_flag_path, sizeof(g_dirty));
        g_have_dirty = CTRUE;
        /* Detect an unclean previous shutdown. */
        f = fopen(g_dirty, "rb");
        if (f != NULL) { g_prev_dirty = CTRUE; fclose(f); }
    }
    /* Mark the session dirty for its whole runtime; cleared on clean exit. */
    if (g_have_dirty) {
        FILE *f = fopen(g_dirty, "wb");
        if (f != NULL) {
            fputs("CastaliaOS session in progress\n", f);
            fclose(f);
        }
    }
    return CE_OK;
}

void sys_crash_shutdown(void)
{
    g_handler = NULL;
    g_handler_user = NULL;
    g_have_crashlog = CFALSE;
    g_have_dirty = CFALSE;
}

void sys_crash_set_handler(SysCrashFn fn, void *user)
{
    g_handler = fn;
    g_handler_user = user;
}

cbool sys_crash_previous_was_dirty(void)
{
    return g_prev_dirty;
}

void sys_crash_mark_clean(void)
{
    if (g_have_dirty) {
        remove(g_dirty); /* absence of the flag == clean */
    }
}

void sys_fatal(const char *subsystem, CResult code,
               const char *file, int line, const char *message)
{
    if (subsystem == NULL) { subsystem = "?"; }
    if (message == NULL)   { message = "(no message)"; }

    SYS_LOGF(subsystem, "FATAL code=%d at %s:%d  %s",
             (int)code, (file ? file : "?"), line, message);

    if (g_have_crashlog) {
        FILE *f = fopen(g_crashlog, "a");
        if (f != NULL) {
            fprintf(f, "----\nsubsystem=%s\ncode=%d\nlocation=%s:%d\n"
                       "message=%s\nlive_bytes=%lu\npeak_bytes=%lu\n",
                    subsystem, (int)code, (file ? file : "?"), line, message,
                    (unsigned long)sys_mem_live_bytes(),
                    (unsigned long)sys_mem_peak_bytes());
            fclose(f);
        }
    }

    if (g_handler != NULL) {
        g_handler(subsystem, code, file, line, message, g_handler_user);
        /* Handler may choose to return (e.g. after dropping to DOS). If it
         * returns, we still stop the process to avoid running in a corrupt
         * state. */
    }
    /* Flush logs and exit with a nonzero status; CBOOT will offer safe mode
     * because the dirty flag is still present. */
    sys_log_shutdown();
    exit(2);
}

const char *sys_crash_log_path(void)
{
    return g_have_crashlog ? g_crashlog : "";
}
