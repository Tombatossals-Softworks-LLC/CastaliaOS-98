/*
 * fsize_core.h - Totalling file sizes without wrapping past two gigabytes.
 *
 * The File Manager's status line says how many bytes the files in a folder
 * come to. It summed them into a `long`, which is 64 bits on every machine
 * this is developed on and 32 bits on the machine it ships to -- so a folder
 * holding more than 2 GB wrapped to a NEGATIVE total on the target and on the
 * target only. The display then made it worse rather than catching it: the
 * "small enough to show in bytes" test is `bytes < 10240`, which a negative
 * number passes, so the status line printed a negative byte count.
 *
 * Two gigabytes in one folder is not exotic on the hardware this targets.
 * FAT32 allows a single file of up to 4 GB, and a CD or a disk image sitting
 * next to a couple of archives reaches it easily.
 *
 * The total is a cs32 here, deliberately, because cs32 is exactly 32 bits on
 * BOTH the host and the target. That is the whole reason tests/test_fsize.c
 * can see this at all: written with `long`, the arithmetic is correct on
 * every machine a test could run on and wrong only where no test runs. It is
 * the same lesson as the memory gauges and the percentages before it.
 *
 * Rather than wrap, the sum SATURATES and says so, because a folder that
 * large should read "more than 2047.9 MB" and not a lie in either direction.
 */
#ifndef CASTALIA_FSIZE_CORE_H
#define CASTALIA_FSIZE_CORE_H

#include "castalia/ctypes.h"

/* The largest total a cs32 holds. Written out rather than derived from
 * <limits.h>, whose LONG_MAX is the HOST's answer and not the target's. */
#define FSIZE_MAX 2147483647L

typedef struct {
    cs32  total;   /* bytes so far; never wraps, saturates at FSIZE_MAX  */
    int   files;   /* how many contributed                               */
    cbool over;    /* the true total is larger than 'total' can say      */
} FsizeSum;

/* Start an empty total. */
void fsize_begin(FsizeSum *s);

/*
 * Add one file's size. Sizes that are negative (plat_file_size returns -1 for
 * "unknown") or zero contribute nothing and are not counted as files, which
 * is what the File Manager already did and what keeps "3 files" honest.
 */
void fsize_add(FsizeSum *s, long bytes);

/*
 * The status line: "No files here", "%ld bytes in N files" while the total is
 * small enough to be worth stating exactly, and a KB/MB label above that.
 * A saturated total is prefixed "more than", never printed as if exact.
 */
void fsize_label(const FsizeSum *s, char *dst, cu32 dstsz);

#endif /* CASTALIA_FSIZE_CORE_H */
