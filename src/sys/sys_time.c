/*
 * sys_time.c - Time helpers built on the platform tick + wall clock.
 *
 * Kept separate from the platform layer so the upper stack has a stable,
 * dependency-light time API. The actual monotonic tick and wall clock come
 * from plat_ticks_ms()/plat_wall_clock().
 */
#include "castalia/sys.h"
#include "castalia/plat.h"

cu32 sys_now_ms(void)
{
    return plat_ticks_ms();
}

void sys_format_clock(char *dst, cu32 dstsz)
{
    sys_format_clock_ex(dst, dstsz, CFALSE);
}

void sys_format_clock_ex(char *dst, cu32 dstsz, cbool with_seconds)
{
    int h = 0, m = 0, s = 0;
    if (dst == NULL || dstsz == 0) { return; }
    plat_wall_clock(&h, &m, &s);
    if (with_seconds) { sys_snprintf(dst, dstsz, "%02d:%02d:%02d", h, m, s); }
    else              { sys_snprintf(dst, dstsz, "%02d:%02d", h, m); }
}
