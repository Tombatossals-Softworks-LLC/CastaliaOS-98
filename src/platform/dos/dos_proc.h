/*
 * dos_proc.h - DOS child-process launch and power control (DOS backend).
 * Compiled only under CASTALIA_DOS.
 */
#ifndef CASTALIA_DOS_PROC_H
#define CASTALIA_DOS_PROC_H
#ifdef CASTALIA_DOS
#include "castalia/ctypes.h"

/* Run a DOS program to completion in text mode. Returns its exit code (or a
 * negative CResult on failure to launch). The caller restores video. */
int  dos_exec(const char *path, const char *args, const char *workdir);
void dos_reboot(void);
void dos_poweroff(void);

#endif /* CASTALIA_DOS */
#endif
