/*
 * test_home.c - Where this system keeps its files (sys_home.c).
 *
 * One line of policy, so a short suite -- but the policy is load-bearing in a
 * way that is easy to miss. An unset CASTALIA_HOME must mean "the current
 * directory", because that is what lets tools/run_demos.sh and the abuse
 * harness point the entire desktop at a throwaway folder; if the fallback
 * ever became an absolute path, those runs would start writing into a real
 * installation and the harness would look like it was still passing.
 *
 * The value is read fresh on every call rather than cached, which the demos
 * depend on: they set CASTALIA_HOME after startup.
 */
#include "ctest.h"
#include "castalia/sys.h"

#include <stdlib.h>
#include <string.h>

/* setenv/unsetenv are POSIX; these tests are host-only, and sys_home() itself
 * uses nothing but getenv, which Watcom has too. */
static void set_home(const char *v)
{
    if (v == NULL) { unsetenv("CASTALIA_HOME"); }
    else { setenv("CASTALIA_HOME", v, 1); }
}

void test_home(void)
{
    char b[CASTALIA_MAX_PATH];
    char saved[CASTALIA_MAX_PATH];
    const char *orig = getenv("CASTALIA_HOME");
    int had = (orig != NULL);

    printf("- home directory\n");
    if (had) { sys_strlcpy(saved, orig, sizeof saved); }

    /* ---- the root itself ---------------------------------------------- */
    set_home("/home/user/castalia");
    CHECK_STR(sys_home(), "/home/user/castalia");
    set_home("C:\\CASTALIA");
    CHECK_STR(sys_home(), "C:\\CASTALIA");

    /* Unset and empty both mean "here". This is the case the demos and the
     * abuse harness rely on, and the one an absolute default would break. */
    set_home(NULL);
    CHECK_STR(sys_home(), ".");
    set_home("");
    CHECK_STR(sys_home(), ".");

    /* Read fresh every time: the demos set CASTALIA_HOME after startup, so a
     * value cached on first use would send every save to the wrong tree. */
    set_home("/one");
    CHECK_STR(sys_home(), "/one");
    set_home("/two");
    CHECK_STR(sys_home(), "/two");

    /* ---- joining ------------------------------------------------------- */
    set_home("/home/user/castalia");
    sys_home_path(b, (cu32)sizeof b, "DOCS");
    CHECK_STR(b, "/home/user/castalia/DOCS");
    sys_home_path(b, (cu32)sizeof b, "SYS/AGENDA.TXT");
    CHECK_STR(b, "/home/user/castalia/SYS/AGENDA.TXT");

    /* No subpath is the root, not the root with a separator stuck on it --
     * callers pass this straight to plat_opendir. */
    sys_home_path(b, (cu32)sizeof b, NULL);
    CHECK_STR(b, "/home/user/castalia");
    sys_home_path(b, (cu32)sizeof b, "");
    CHECK_STR(b, "/home/user/castalia");

    /* A hand-set CASTALIA_HOME with a trailing separator must not double it.
     * Every demo run sets this variable by hand, so it really happens. */
    set_home("/home/user/castalia/");
    sys_home_path(b, (cu32)sizeof b, "DOCS");
    CHECK_STR(b, "/home/user/castalia/DOCS");
    set_home("C:\\CASTALIA\\");
    sys_home_path(b, (cu32)sizeof b, "DOCS");
    CHECK_STR(b, "C:\\CASTALIA\\DOCS");
    /* A lone root is not a trailing separator to strip. */
    set_home("/");
    sys_home_path(b, (cu32)sizeof b, "DOCS");
    CHECK_STR(b, "//DOCS");

    /* Unset joins onto ".", which is what makes a throwaway home work. */
    set_home(NULL);
    sys_home_path(b, (cu32)sizeof b, "DOCS");
    CHECK_STR(b, "./DOCS");

    /* ---- degenerate output --------------------------------------------- */
    set_home("/home/user/castalia");
    sys_home_path(NULL, 16, "DOCS");                 /* no crash */
    sys_home_path(b, 0, "DOCS");                     /* no crash, no write */
    {
        char small[8];
        sys_home_path(small, (cu32)sizeof small, "DOCS");
        CHECK(small[7] == '\0');                     /* truncated, not overrun */
        set_home("/home/user/castalia/");            /* the strlcpy path too */
        sys_home_path(small, (cu32)sizeof small, "DOCS");
        CHECK(small[7] == '\0');
    }

    if (had) { set_home(saved); } else { set_home(NULL); }
}
