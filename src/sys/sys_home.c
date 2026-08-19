/*
 * sys_home.c - Where this system keeps its files.
 *
 * Everything the desktop persists -- documents, themes, the agenda, the
 * wallpaper, the Trash, saved screenshots, add-on packages -- lives under one
 * root, named by the CASTALIA_HOME environment variable and created by the
 * installer.
 *
 * That single fact was written out thirty-eight times: every app, the shell,
 * the file dialog and every demo scene did its own getenv and its own
 *
 *     if (home == NULL || home[0] == '\0') { home = "."; }
 *
 * They agreed only because nobody had changed one. The fallback in particular
 * is a decision and not an obvious one: an unset CASTALIA_HOME means "run out
 * of the current directory", which is what makes the headless demos and the
 * abuse harness able to point the whole system at a throwaway folder.
 *
 * CBOOT and INSTALL deliberately do NOT use this. They run before the desktop
 * exists -- CBOOT from BIN, the installer from anywhere -- and must find the
 * INSTALLED tree rather than whatever folder they happen to be started from,
 * so they fall back to "C:\CASTALIA". Neither is linked against sys_* at all,
 * which keeps the two policies from being confused for one.
 *
 * The separator is '/' because that is what all thirty-eight sites wrote, and
 * DOS accepts it in every call this system makes.
 */
#include "castalia/sys.h"

#include <stdlib.h>

const char *sys_home(void)
{
    const char *home = getenv("CASTALIA_HOME");
    if (home == NULL || home[0] == '\0') { return "."; }
    return home;
}

void sys_home_path(char *out, cu32 outsz, const char *sub)
{
    const char *home;
    if (out == NULL || outsz == 0) { return; }
    home = sys_home();
    if (sub == NULL || sub[0] == '\0') {
        sys_strlcpy(out, home, outsz);
        return;
    }
    /* A trailing separator on the root would otherwise double up. It cannot
     * come from the installer, but CASTALIA_HOME is set by hand often enough
     * -- every demo run sets it -- that "C:\CASTALIA\" must work. */
    {
        cu32 n = sys_strnlen(home, outsz);
        if (n > 1 && (home[n - 1] == '/' || home[n - 1] == '\\')) {
            sys_strlcpy(out, home, outsz);
            n = sys_strnlen(out, outsz);
            if (n < outsz) { sys_strlcpy(out + n, sub, outsz - n); }
            return;
        }
    }
    sys_snprintf(out, outsz, "%s/%s", home, sub);
}
