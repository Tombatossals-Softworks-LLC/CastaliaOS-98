/*
 * dos_proc.c - DOS child-process launch and power control (Open Watcom).
 *
 * Uses the C runtime's system() to run the child (which maps to COMMAND.COM),
 * so batch pre/post lines and PATH resolution behave as a DOS user expects.
 * Power control uses the keyboard-controller reset pulse and the APM
 * real-mode interface. Compiled only under CASTALIA_DOS.
 */
#ifdef CASTALIA_DOS

#include "dos_proc.h"
#include "castalia/sys.h"

#include <i86.h>
#include <stdlib.h>
#include <direct.h>   /* chdir (Open Watcom) */
#include <conio.h>

int dos_exec(const char *path, const char *args, const char *workdir)
{
    char cmd[512];
    char saved_cwd[CASTALIA_MAX_PATH];
    int  rc;

    if (path == NULL) { return CE_INVALID; }

    /* Remember and change working directory if requested. */
    saved_cwd[0] = '\0';
    if (workdir != NULL && workdir[0] != '\0') {
        getcwd(saved_cwd, (int)sizeof(saved_cwd));
        if (chdir(workdir) != 0) {
            SYS_LOGW("plat", "dos_exec: chdir('%s') failed", workdir);
        }
    }

    sys_strlcpy(cmd, path, sizeof(cmd));
    if (args != NULL && args[0] != '\0') {
        sys_strlcat(cmd, " ", sizeof(cmd));
        sys_strlcat(cmd, args, sizeof(cmd));
    }
    SYS_LOGI("plat", "dos_exec: %s", cmd);
    rc = system(cmd);

    if (saved_cwd[0] != '\0') { chdir(saved_cwd); }
    return rc;
}

void dos_reboot(void)
{
    /* Pulse the keyboard controller reset line: works on PC-compatible
     * hardware including the 440BX-class target. */
    SYS_LOGI("plat", "dos_reboot via KBC 0xFE");
    outp(0x64, 0xFE);
    /* If that somehow returns, fall through to a triple-fault-ish halt. */
    for (;;) { /* spin */ }
}

void dos_poweroff(void)
{
    union REGS r;
    /* APM real-mode: connect, then set system power state to OFF. Best
     * effort; if APM is absent the calls simply fail and we return. */
    r.w.ax = 0x5301; r.w.bx = 0x0000; int386(0x15, &r, &r); /* connect */
    r.w.ax = 0x530E; r.w.bx = 0x0000; r.w.cx = 0x0102; int386(0x15, &r, &r);
    r.w.ax = 0x5307; r.w.bx = 0x0001; r.w.cx = 0x0003; int386(0x15, &r, &r);
    SYS_LOGW("plat", "dos_poweroff: APM did not power down (no support?)");
}

#endif /* CASTALIA_DOS */
