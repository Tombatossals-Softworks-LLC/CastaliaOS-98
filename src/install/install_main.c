/*
 * install_main.c - INSTALL.EXE entry point.
 *
 * A thin CLI over the portable installer core (install_core.c). Defaults match
 * a real FreeDOS machine (install to C:\CASTALIA, edit C:\ boot files), but
 * every path is overridable so the same binary is used by the host unit tests
 * and by a technician installing to a non-C: drive.
 *
 *   INSTALL.EXE                 install to C:\CASTALIA, wire the boot entry
 *   INSTALL.EXE uninstall       restore boot files and remove C:\CASTALIA
 *   INSTALL.EXE --root D:\CAST  install to a different drive/dir
 *   INSTALL.EXE --src Q:\       copy program files from the CD/media root
 *   INSTALL.EXE --sysroot D:\   put the boot entry on a different system drive
 *   INSTALL.EXE uninstall --keep   remove the boot entry but keep the files
 *
 * Built for DOS by Makefile.dos and for the host by the top-level Makefile
 * (build/install) so the flow can be exercised without DOS.
 */
#include "castalia/install.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void)
{
    printf("CastaliaOS 98 PE installer\n");
    printf("Usage: INSTALL.EXE [uninstall] [options]\n");
    printf("  --root PATH     install root      (default $CASTALIA_HOME or C:\\CASTALIA)\n");
    printf("  --sysroot PATH  boot-file drive   (default C:\\)\n");
    printf("  --src PATH      copy files from    (installation media, optional)\n");
    printf("  --keep          uninstall: keep the installed files\n");
    printf("  -q, --quiet     less output\n");
    printf("  -h, --help      this help\n");
}

int main(int argc, char **argv)
{
    InstallOpts o;
    int do_uninstall = 0;
    int keep_tree = 0;
    int i;
    int rc;
    const char *env_home;

    memset(&o, 0, sizeof(o));
    env_home = getenv("CASTALIA_HOME");
    o.install_root = (env_home && env_home[0]) ? env_home : "C:\\CASTALIA";
    o.sys_root     = "C:\\";
    o.src_root     = NULL;
    o.verbose      = 1;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "uninstall") == 0 || strcmp(a, "/uninstall") == 0 ||
            strcmp(a, "--uninstall") == 0) {
            do_uninstall = 1;
        } else if (strcmp(a, "--root") == 0 && i + 1 < argc) {
            o.install_root = argv[++i];
        } else if (strcmp(a, "--sysroot") == 0 && i + 1 < argc) {
            o.sys_root = argv[++i];
        } else if (strcmp(a, "--src") == 0 && i + 1 < argc) {
            o.src_root = argv[++i];
        } else if (strcmp(a, "--keep") == 0) {
            keep_tree = 1;
        } else if (strcmp(a, "-q") == 0 || strcmp(a, "--quiet") == 0) {
            o.verbose = 0;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0 ||
                   strcmp(a, "/?") == 0) {
            usage();
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", a);
            usage();
            return 2;
        }
    }

    printf("CastaliaOS 98 PE - %s\n", do_uninstall ? "Uninstall" : "Install");
    printf("  Install root : %s\n", o.install_root);
    printf("  Boot files   : %s\n", o.sys_root);
    if (o.src_root) { printf("  Source media : %s\n", o.src_root); }
    printf("---------------------------------------------\n");

    if (do_uninstall) {
        rc = uninstall_run(&o, keep_tree ? 0 : 1);
    } else {
        rc = install_run(&o);
    }

    if (rc == INST_OK) {
        if (do_uninstall) {
            printf("Uninstall complete. Your original boot files are restored.\n");
        } else {
            printf("Install complete. Reboot to start CastaliaOS 98 PE.\n");
            printf("To undo: INSTALL.EXE uninstall\n");
        }
        return 0;
    }

    fprintf(stderr, "FAILED (code %d). Nothing was left half-written; see\n", rc);
    fprintf(stderr, "docs/RECOVERY.md, or restore *%s backups by hand.\n",
            INST_BACKUP_SUFFIX);
    return 1;
}
