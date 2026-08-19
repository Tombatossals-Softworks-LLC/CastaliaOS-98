/*
 * mkcapp.c - Author a .CAPP package from a manifest + section files.
 *
 * A small host tool (build/mkcapp) that turns metadata and payload files into a
 * validated .CAPP image using the same capp_build the runtime validates with,
 * so the format is round-trippable and packages are real, testable artifacts.
 *
 *   mkcapp -o clock.capp --name Clock --version 1.0.0 --author "You" \
 *          --desc "A desk clock" [--abi 1] \
 *          [--code plugin.bin] [--icon icon.bin] [--res data.bin] [--help h.txt]
 */
#include "castalia/capp.h"
#include "castalia/capp_abi.h"     /* CAPP_ABI_VERSION */
#include "castalia/capp_loader.h"  /* CAPP_CODE_BUILTIN_TAG */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MK_MAX_SEC   CAPP_MAX_SECTIONS
#define MK_MAX_IMAGE (2UL * 1024UL * 1024UL)

static void usage(void)
{
    printf("mkcapp - build a CastaliaOS .CAPP package\n");
    printf("Usage: mkcapp -o OUT.capp --name NAME [options]\n");
    printf("  --name S --version S --author S --desc S   manifest fields\n");
    printf("  --abi N                                     target ABI (default %d)\n",
           CAPP_ABI_VERSION);
    printf("  --code F  --icon F  --res F  --help F        add a section from file F\n");
    printf("  --builtin NAME                              code section = builtin ref\n");
}

/* Read a whole file into a malloc'd buffer. Returns bytes, or -1. */
static long read_all(const char *path, unsigned char **out)
{
    FILE *f = fopen(path, "rb");
    long sz;
    unsigned char *buf;
    size_t got;
    *out = NULL;
    if (!f) { return -1; }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) { fclose(f); return -1; }
    buf = (unsigned char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if ((long)got != sz) { free(buf); return -1; }
    *out = buf;
    return sz;
}

int main(int argc, char **argv)
{
    CappInfo meta;
    const char *out_path = NULL;
    cu16 types[MK_MAX_SEC], flags[MK_MAX_SEC];
    const void *data[MK_MAX_SEC];
    cu32 sizes[MK_MAX_SEC];
    unsigned char *bufs[MK_MAX_SEC];
    int n = 0, i, rc = 0;
    unsigned char *image;
    long total;

    memset(&meta, 0, sizeof(meta));
    meta.abi_version = CAPP_ABI_VERSION;
    strcpy(meta.version, "1.0.0");

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        int type = 0;
        const char *fname = NULL;
        if      (strcmp(a, "-o") == 0 && i + 1 < argc)        { out_path = argv[++i]; }
        else if (strcmp(a, "--name") == 0 && i + 1 < argc)    { snprintf(meta.name, CAPP_NAME_MAX, "%s", argv[++i]); }
        else if (strcmp(a, "--version") == 0 && i + 1 < argc) { snprintf(meta.version, CAPP_VERSTR_MAX, "%s", argv[++i]); }
        else if (strcmp(a, "--author") == 0 && i + 1 < argc)  { snprintf(meta.author, CAPP_AUTHOR_MAX, "%s", argv[++i]); }
        else if (strcmp(a, "--desc") == 0 && i + 1 < argc)    { snprintf(meta.description, CAPP_DESC_MAX, "%s", argv[++i]); }
        else if (strcmp(a, "--abi") == 0 && i + 1 < argc)     { meta.abi_version = (cu16)atoi(argv[++i]); }
        else if (strcmp(a, "--builtin") == 0 && i + 1 < argc) {
            /* Synthesize a code section that references a compiled-in plugin:
             * the tag followed by the NUL-terminated builtin name. */
            const char *bn = argv[++i];
            size_t bl = strlen(bn);
            unsigned char *cb;
            if (n >= MK_MAX_SEC) { fprintf(stderr, "too many sections\n"); return 2; }
            cb = (unsigned char *)malloc(CAPP_CODE_BUILTIN_TAG_LEN + bl + 1);
            if (!cb) { fprintf(stderr, "out of memory\n"); return 1; }
            memcpy(cb, CAPP_CODE_BUILTIN_TAG, CAPP_CODE_BUILTIN_TAG_LEN);
            memcpy(cb + CAPP_CODE_BUILTIN_TAG_LEN, bn, bl + 1); /* include NUL */
            bufs[n] = cb;
            types[n] = CAPP_SEC_CODE; flags[n] = 0;
            data[n]  = cb;
            sizes[n] = (cu32)(CAPP_CODE_BUILTIN_TAG_LEN + bl + 1);
            n++;
        }
        else if (strcmp(a, "--code") == 0 && i + 1 < argc)    { type = CAPP_SEC_CODE;     fname = argv[++i]; }
        else if (strcmp(a, "--icon") == 0 && i + 1 < argc)    { type = CAPP_SEC_ICON;     fname = argv[++i]; }
        else if (strcmp(a, "--res") == 0 && i + 1 < argc)     { type = CAPP_SEC_RESOURCE; fname = argv[++i]; }
        else if (strcmp(a, "--help") == 0 && i + 1 < argc)    { type = CAPP_SEC_HELP;     fname = argv[++i]; }
        else if (strcmp(a, "-h") == 0)                        { usage(); return 0; }
        else { fprintf(stderr, "Unknown/needs-arg: %s\n", a); usage(); return 2; }

        if (type != 0) {
            long sz;
            if (n >= MK_MAX_SEC) { fprintf(stderr, "too many sections\n"); return 2; }
            sz = read_all(fname, &bufs[n]);
            if (sz < 0) { fprintf(stderr, "cannot read %s\n", fname); return 1; }
            types[n] = (cu16)type;
            flags[n] = 0;
            data[n]  = bufs[n];
            sizes[n] = (cu32)sz;
            n++;
        }
    }

    if (out_path == NULL || meta.name[0] == '\0') {
        fprintf(stderr, "need -o OUT.capp and --name NAME\n");
        usage();
        return 2;
    }

    image = (unsigned char *)malloc((size_t)MK_MAX_IMAGE);
    if (!image) { fprintf(stderr, "out of memory\n"); return 1; }

    total = capp_build(image, (cu32)MK_MAX_IMAGE, &meta,
                       types, flags, data, sizes, n);
    if (total < 0) {
        fprintf(stderr, "capp_build failed (%ld)\n", total);
        rc = 1;
    } else {
        FILE *of = fopen(out_path, "wb");
        if (!of || fwrite(image, 1, (size_t)total, of) != (size_t)total) {
            fprintf(stderr, "cannot write %s\n", out_path);
            rc = 1;
        } else {
            printf("wrote %s (%ld bytes, %d section(s))\n", out_path, total, n);
        }
        if (of) { fclose(of); }
    }

    free(image);
    for (i = 0; i < n; i++) { free(bufs[i]); }
    return rc;
}
