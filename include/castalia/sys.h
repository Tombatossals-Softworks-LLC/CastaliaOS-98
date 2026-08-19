/*
 * sys.h - Layer 2 system runtime: logging, safe strings, memory, time,
 *         and crash/error reporting.
 *
 * These services are the foundation every other subsystem stands on. They
 * are host-portable C89 and carry no platform assumptions of their own;
 * where they need the wall clock or a millisecond tick they call the
 * platform layer (plat.h).
 *
 * Convention: every subsystem exposes sys_/gfx_/wm_/... _init() and
 * _shutdown() and the two are symmetric. sys must be initialized first.
 */
#ifndef CASTALIA_SYS_H
#define CASTALIA_SYS_H

#include "castalia/ctypes.h"

/* ---------------------------------------------------------------------- */
/* Logging                                                                */
/* ---------------------------------------------------------------------- */

typedef enum {
    SYS_LOG_TRACE = 0,
    SYS_LOG_DEBUG = 1,
    SYS_LOG_INFO  = 2,
    SYS_LOG_WARN  = 3,
    SYS_LOG_ERROR = 4,
    SYS_LOG_FATAL = 5
} SysLogLevel;

/*
 * Initialize the logging subsystem. 'path' is the log file to append to
 * (rotated when it exceeds the size cap); pass NULL to log to stderr only.
 * 'min_level' filters out anything below it. Returns CE_OK on success.
 * Safe to call before any other subsystem.
 */
CResult sys_log_init(const char *path, SysLogLevel min_level);

/* Flush and close the log. Symmetric with sys_log_init. */
void sys_log_shutdown(void);

/* Change the minimum level at runtime (e.g. verbose in safe mode). */
void sys_log_set_level(SysLogLevel min_level);

/* The active log file path, or "" if logging only to stderr. Lets the Log
 * Viewer app tail the same file the logger writes. Never NULL. */
const char *sys_log_path(void);

/* Flush any buffered log records to disk so a reader (Log Viewer) sees the
 * latest lines. No-op when there is no file. */
void sys_log_flush(void);

/*
 * Emit a log record. 'subsystem' is a short tag ("gfx", "wm", ...).
 * Prefer the SYS_LOG* macros so file/line are captured automatically.
 * Never allocates; message is bounded and truncated safely.
 */
void sys_log_write(SysLogLevel level, const char *subsystem,
                   const char *file, int line, const char *fmt, ...);

/*
 * The log macros capture __FILE__/__LINE__ automatically (Bible: "Log
 * subsystem, code, file, line, human message"). They use variadic macros --
 * the single, deliberate C99 convenience in the otherwise C89 core -- which
 * all target toolchains (gcc, clang, Open Watcom) support. Everything else
 * compiles clean under -std=c89 -pedantic.
 */
#define SYS_LOGT(sub, ...) sys_log_write(SYS_LOG_TRACE, (sub), __FILE__, __LINE__, __VA_ARGS__)
#define SYS_LOGD(sub, ...) sys_log_write(SYS_LOG_DEBUG, (sub), __FILE__, __LINE__, __VA_ARGS__)
#define SYS_LOGI(sub, ...) sys_log_write(SYS_LOG_INFO,  (sub), __FILE__, __LINE__, __VA_ARGS__)
#define SYS_LOGW(sub, ...) sys_log_write(SYS_LOG_WARN,  (sub), __FILE__, __LINE__, __VA_ARGS__)
#define SYS_LOGE(sub, ...) sys_log_write(SYS_LOG_ERROR, (sub), __FILE__, __LINE__, __VA_ARGS__)
#define SYS_LOGF(sub, ...) sys_log_write(SYS_LOG_FATAL, (sub), __FILE__, __LINE__, __VA_ARGS__)

/* Rotate the log when it passes this many bytes (see Bible perf budget). */
#define SYS_LOG_ROTATE_BYTES (512L * 1024L)

/* ---------------------------------------------------------------------- */
/* Safe strings                                                           */
/* ---------------------------------------------------------------------- */
/*
 * All string handling in the core avoids strcpy/strcat/sprintf. These
 * helpers always NUL-terminate within 'dstsz' and report truncation.
 */

/* Bounded copy. Always NUL-terminates when dstsz > 0. Returns the length
 * that would have been written (like strlcpy); >= dstsz means truncated. */
cu32 sys_strlcpy(char *dst, const char *src, cu32 dstsz);

/* Bounded append. Always NUL-terminates when dstsz > 0. */
cu32 sys_strlcat(char *dst, const char *src, cu32 dstsz);

/* Bounded formatted print. Always NUL-terminates. Returns chars written
 * (excluding the terminator), or a truncation-clamped count. */
int  sys_snprintf(char *dst, cu32 dstsz, const char *fmt, ...);

/* Length with an upper bound, so a missing terminator can't run away. */
cu32 sys_strnlen(const char *s, cu32 maxlen);

/* ---------------------------------------------------------------------- */
/* Little-endian byte access (src/sys/sys_le.c)                           */
/* ---------------------------------------------------------------------- */
/*
 * Every on-disk format this system owns is little-endian, deliberately, so a
 * file written on the DOS target opens on a modern machine and the reverse.
 * These are the only four functions that decide that -- they were written
 * five times over before they were written once, and an invariant with five
 * implementations is five chances to corrupt a file format quietly.
 *
 * NULL is a no-op / zero rather than a crash: these parse files, and a caller
 * that has already failed to read one should not take the program with it.
 */
cu16 sys_le16(const unsigned char *p);
cu32 sys_le32(const unsigned char *p);
void sys_put_le16(unsigned char *p, cu16 v);
void sys_put_le32(unsigned char *p, cu32 v);

/* Case-insensitive compare (ASCII). Returns <0, 0, >0 like strcmp. */
int  sys_stricmp(const char *a, const char *b);

/* Trim ASCII whitespace in place; returns dst. */
char *sys_strtrim(char *s);

/* ---------------------------------------------------------------------- */
/* Memory                                                                 */
/* ---------------------------------------------------------------------- */
/*
 * Thin accounted wrappers over the platform allocator. Every allocation is
 * counted so System Info and the leak checks can report live bytes. Freeing
 * a NULL pointer is a no-op.
 */
void *sys_alloc(cu32 size);
void *sys_realloc(void *ptr, cu32 old_size, cu32 new_size);
void  sys_free(void *ptr, cu32 size);

/* Zero-initializing helpers. */
void *sys_calloc(cu32 count, cu32 size);

/* Live accounting for diagnostics. */
cu32 sys_mem_live_bytes(void);
cu32 sys_mem_peak_bytes(void);
cu32 sys_mem_alloc_count(void);

/* ---------------------------------------------------------------------- */
/* Time helpers (built on plat_ticks_ms)                                  */
/* ---------------------------------------------------------------------- */
cu32 sys_now_ms(void);        /* monotonic millisecond tick               */
void sys_format_clock(char *dst, cu32 dstsz); /* "HH:MM" 24h from wall clock */
/* "HH:MM" or "HH:MM:SS" (24h) depending on 'with_seconds'. */
void sys_format_clock_ex(char *dst, cu32 dstsz, cbool with_seconds);

/* ---------------------------------------------------------------------- */
/* Crash / fatal error boundary                                           */
/* ---------------------------------------------------------------------- */
/*
 * A fatal condition writes a crash record (subsystem, code, file, line,
 * message, live memory) to the crash log and sets the "dirty" flag that
 * CBOOT reads on next boot to offer safe mode. If a crash handler has been
 * installed (the shell installs one that paints a recoverable dialog and
 * drops to DOS), it is invoked; otherwise the process aborts cleanly.
 */
typedef void (*SysCrashFn)(const char *subsystem, CResult code,
                           const char *file, int line, const char *message,
                           void *user);

CResult sys_crash_init(const char *crashlog_path, const char *dirty_flag_path);
void    sys_crash_shutdown(void);
void    sys_crash_set_handler(SysCrashFn fn, void *user);

/* Record a fatal error and invoke the handler. Does not return normally
 * unless a handler is installed and chooses to return. */
void    sys_fatal(const char *subsystem, CResult code,
                  const char *file, int line, const char *message);

#define SYS_FATAL(sub, code, msg) sys_fatal((sub), (code), __FILE__, __LINE__, (msg))

/* Clear the boot-dirty flag once a session has run cleanly for a while. */
void    sys_crash_mark_clean(void);

/* Returns CTRUE if the previous session set the dirty flag. */
cbool   sys_crash_previous_was_dirty(void);

/* Where the crash log is being written, or "" if none was configured. The
 * crash screen prints it: telling someone "see the log" without saying which
 * file is advice they cannot act on. */
const char *sys_crash_log_path(void);

/*
 * Where this system keeps its files (sys_home.c).
 *
 * CASTALIA_HOME names the root the installer creates; unset or empty means
 * "the current directory", which is what lets the demos and the abuse harness
 * point the whole system at a throwaway folder. sys_home() never returns NULL,
 * so no caller needs its own fallback -- which is how thirty-eight of them
 * came to have one each.
 *
 * sys_home_path() joins a subpath onto it ("DOCS", "SYS/AGENDA.TXT"); a NULL
 * or empty subpath gives the root itself.
 *
 * CBOOT and the installer do not use these: they run before the desktop
 * exists and must find the installed tree, not the current directory. See the
 * comment at the top of sys_home.c.
 */
const char *sys_home(void);
void        sys_home_path(char *out, cu32 outsz, const char *sub);

#endif /* CASTALIA_SYS_H */
