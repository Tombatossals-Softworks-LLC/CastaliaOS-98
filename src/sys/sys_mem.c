/*
 * sys_mem.c - Accounted memory wrappers.
 *
 * Thin layer over malloc/free that tracks live and peak bytes so System
 * Info and the leak checks can report footprint against the Bible's
 * under-8MB idle-shell budget. The DOS build uses the same code; Open
 * Watcom's flat-model malloc backs it. Callers pass the size on free/realloc
 * so we never need a per-block header, keeping overhead at zero bytes.
 */
#include "castalia/sys.h"

#include <stdlib.h>
#include <string.h>

static cu32 g_live      = 0;
static cu32 g_peak      = 0;
static cu32 g_allocs    = 0;

static void account_add(cu32 n)
{
    g_live += n;
    if (g_live > g_peak) { g_peak = g_live; }
    g_allocs++;
}

static void account_sub(cu32 n)
{
    if (n > g_live) { g_live = 0; } else { g_live -= n; }
    if (g_allocs > 0) { g_allocs--; }
}

void *sys_alloc(cu32 size)
{
    void *p;
    if (size == 0) { size = 1; }
    p = malloc((size_t)size);
    if (p != NULL) { account_add(size); }
    return p;
}

void *sys_calloc(cu32 count, cu32 size)
{
    cu32 total;
    void *p;
    /*
     * count * size is computed in 32 bits and wraps, and a wrapped product is
     * the worst possible outcome: sys_calloc(65536, 65536) came to zero, which
     * the line below then turned into a one-byte allocation handed back as a
     * non-NULL pointer the caller believes addresses four gigabytes.
     *
     * Every caller today passes a literal 1, so this was latent rather than
     * live -- but a memory primitive should not be safe only because of who
     * happens to call it, and the next caller should not have to know.
     */
    if (count != 0u && size > (cu32)0xFFFFFFFFUL / count) { return NULL; }
    total = count * size;
    if (total == 0) { total = 1; }
    p = calloc(1, (size_t)total);
    if (p != NULL) { account_add(total); }
    return p;
}

void *sys_realloc(void *ptr, cu32 old_size, cu32 new_size)
{
    void *p;
    if (new_size == 0) { new_size = 1; }
    p = realloc(ptr, (size_t)new_size);
    if (p != NULL) {
        /* Adjust accounting: remove old, add new. If ptr was NULL, old==0. */
        if (ptr != NULL) { account_sub(old_size); }
        account_add(new_size);
    }
    return p;
}

void sys_free(void *ptr, cu32 size)
{
    if (ptr == NULL) { return; }
    free(ptr);
    account_sub(size);
}

cu32 sys_mem_live_bytes(void)  { return g_live; }
cu32 sys_mem_peak_bytes(void)  { return g_peak; }
cu32 sys_mem_alloc_count(void) { return g_allocs; }
