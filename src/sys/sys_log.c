/*
 * sys_log.c - Logging subsystem.
 *
 * Appends bounded, timestamped records to a log file (rotated at
 * SYS_LOG_ROTATE_BYTES) and/or stderr. Uses stdio directly rather than the
 * platform file wrappers so it is available before plat_init and cannot be
 * caught in an init cycle. Both the host and Open Watcom DOS runtimes
 * provide stdio.
 *
 * No dynamic allocation: each record is formatted into a fixed stack buffer
 * and truncated safely.
 */
#include "castalia/sys.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define LOG_LINE_MAX 512
#define LOG_PATH_MAX 260

static FILE       *g_fp        = NULL;
static SysLogLevel g_min_level = SYS_LOG_INFO;
static char        g_path[LOG_PATH_MAX];
static cbool       g_have_path = CFALSE;
static long        g_written   = 0;

static const char *level_tag(SysLogLevel lv)
{
    switch (lv) {
    case SYS_LOG_TRACE: return "TRACE";
    case SYS_LOG_DEBUG: return "DEBUG";
    case SYS_LOG_INFO:  return "INFO ";
    case SYS_LOG_WARN:  return "WARN ";
    case SYS_LOG_ERROR: return "ERROR";
    case SYS_LOG_FATAL: return "FATAL";
    default:            return "?????";
    }
}

static void open_log(void)
{
    if (!g_have_path) { g_fp = NULL; return; }
    g_fp = fopen(g_path, "a");
    if (g_fp != NULL) {
        long pos = ftell(g_fp);
        g_written = (pos > 0) ? pos : 0;
    }
}

CResult sys_log_init(const char *path, SysLogLevel min_level)
{
    g_min_level = min_level;
    if (path != NULL && path[0] != '\0') {
        sys_strlcpy(g_path, path, sizeof(g_path));
        g_have_path = CTRUE;
        open_log();
    } else {
        g_have_path = CFALSE;
        g_fp = NULL;
    }
    SYS_LOGI("sys", "%s %s log started (level=%s)",
             "CastaliaOS", "98 PE", level_tag(min_level));
    return CE_OK;
}

void sys_log_shutdown(void)
{
    if (g_fp != NULL) {
        SYS_LOGI("sys", "log stopped");
        fclose(g_fp);
        g_fp = NULL;
    }
    g_have_path = CFALSE;
    g_written = 0;
}

void sys_log_set_level(SysLogLevel min_level)
{
    g_min_level = min_level;
}

const char *sys_log_path(void)
{
    return g_have_path ? g_path : "";
}

void sys_log_flush(void)
{
    if (g_fp != NULL) { fflush(g_fp); }
}

/* Rotate: keep one backup (.old) so a crash still leaves the prior tail. */
static void maybe_rotate(void)
{
    char backup[LOG_PATH_MAX];
    if (!g_have_path || g_fp == NULL) { return; }
    if (g_written < SYS_LOG_ROTATE_BYTES) { return; }
    fclose(g_fp);
    g_fp = NULL;
    sys_strlcpy(backup, g_path, sizeof(backup));
    sys_strlcat(backup, ".old", sizeof(backup));
    remove(backup);          /* ignore error */
    rename(g_path, backup);  /* ignore error */
    g_written = 0;
    open_log();
}

void sys_log_write(SysLogLevel level, const char *subsystem,
                   const char *file, int line, const char *fmt, ...)
{
    char    line_buf[LOG_LINE_MAX];
    char    msg[LOG_LINE_MAX];
    va_list ap;
    int     n;
    const char *base;

    if (level < g_min_level) { return; }
    if (subsystem == NULL) { subsystem = "?"; }

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    /* Trim the file path to its basename to keep lines short. */
    base = file;
    if (file != NULL) {
        const char *p = file;
        while (*p != '\0') {
            if (*p == '/' || *p == '\\') { base = p + 1; }
            p++;
        }
    } else {
        base = "?";
    }

    n = sys_snprintf(line_buf, sizeof(line_buf), "[%s] %-4s %s:%d  %s\n",
                     level_tag(level), subsystem, base, line, msg);

    /* Errors and above always echo to stderr for visibility. */
    if (level >= SYS_LOG_ERROR) {
        fputs(line_buf, stderr);
    }
    if (g_fp != NULL) {
        fputs(line_buf, g_fp);
        fflush(g_fp);
        g_written += n;
        maybe_rotate();
    } else if (level < SYS_LOG_ERROR) {
        /* No usable log file -- either none configured, or the configured
         * path could not be opened (e.g. its directory does not exist).
         * Mirror informational output to stderr so nothing is silently lost
         * (errors already went to stderr above). */
        fputs(line_buf, stderr);
    }
}
