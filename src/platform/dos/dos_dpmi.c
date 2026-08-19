/*
 * dos_dpmi.c - DPMI real-mode services implementation (Open Watcom / DOS4GW).
 * See dos_dpmi.h. Compiled only under CASTALIA_DOS.
 */
#ifdef CASTALIA_DOS

#include "dos_dpmi.h"
#include "castalia/sys.h"

#include <i86.h>       /* int386, int386x, REGS, SREGS (Open Watcom)      */
#include <string.h>

CResult dpmi_alloc_dos(cu16 paragraphs, cu16 *rm_seg, cu16 *sel)
{
    union REGS r;
    r.w.ax = 0x0100;        /* DPMI: allocate DOS memory block            */
    r.w.bx = paragraphs;
    int386(0x31, &r, &r);
    if (r.w.cflag) {
        SYS_LOGE("plat", "dpmi_alloc_dos failed (%u paragraphs)", paragraphs);
        return CE_NOMEM;
    }
    if (rm_seg) { *rm_seg = r.w.ax; } /* real-mode segment */
    if (sel)    { *sel    = r.w.dx; } /* PM selector for free */
    return CE_OK;
}

void dpmi_free_dos(cu16 sel)
{
    union REGS r;
    r.w.ax = 0x0101;        /* DPMI: free DOS memory block                */
    r.w.dx = sel;
    int386(0x31, &r, &r);
}

CResult dpmi_int(int intno, DpmiRegs *regs)
{
    union REGS  r;
    struct SREGS s;
    if (regs == NULL) { return CE_INVALID; }
    segread(&s);
    r.w.ax = 0x0300;        /* DPMI: simulate real-mode interrupt         */
    r.h.bl = (unsigned char)intno;
    r.h.bh = 0;
    r.w.cx = 0;             /* words to copy from PM stack: none          */
    /* ES:EDI -> the DpmiRegs frame (in our flat data segment). */
    s.es = FP_SEG(regs);
    r.x.edi = (unsigned)FP_OFF(regs);
    int386x(0x31, &r, &r, &s);
    if (r.w.cflag) {
        SYS_LOGE("plat", "dpmi_int 0x%02X failed", intno);
        return CE_FAIL;
    }
    return CE_OK;
}

/* Under DOS/4GW the low 1 MB is identity-mapped and reachable through the
 * default flat data selector, so a real-mode segment:offset becomes the
 * linear address seg*16+off. */
void dpmi_copy_to_dos(cu16 rm_seg, cu16 off, const void *src, cu32 bytes)
{
    unsigned char *dst = (unsigned char *)(((cu32)rm_seg << 4) + off);
    memcpy(dst, src, (size_t)bytes);
}

void dpmi_copy_from_dos(void *dst, cu16 rm_seg, cu16 off, cu32 bytes)
{
    const unsigned char *src = (const unsigned char *)(((cu32)rm_seg << 4) + off);
    memcpy(dst, src, (size_t)bytes);
}

void *dpmi_map_physical(cu32 phys, cu32 bytes)
{
    union REGS r;
    cu32 lin;
    r.w.ax = 0x0800;              /* DPMI: physical address mapping         */
    r.w.bx = (unsigned)(phys >> 16);
    r.w.cx = (unsigned)(phys & 0xFFFF);
    r.w.si = (unsigned)(bytes >> 16);
    r.w.di = (unsigned)(bytes & 0xFFFF);
    int386(0x31, &r, &r);
    if (r.w.cflag) {
        SYS_LOGW("plat", "dpmi_map_physical(%08lX,%lu) failed",
                 (unsigned long)phys, (unsigned long)bytes);
        return NULL;
    }
    lin = ((cu32)r.w.bx << 16) | (cu32)r.w.cx;
    /* Zero-based flat model (DOS/4GW): the linear address is the near pointer. */
    return (void *)lin;
}

void dpmi_unmap_physical(void *linear)
{
    union REGS r;
    cu32 lin = (cu32)linear;
    if (linear == NULL) { return; }
    r.w.ax = 0x0801;             /* DPMI: free physical address mapping     */
    r.w.bx = (unsigned)(lin >> 16);
    r.w.cx = (unsigned)(lin & 0xFFFF);
    int386(0x31, &r, &r);
}

CResult dpmi_get_real_vector(int vec, cu16 *seg, cu16 *off)
{
    union REGS r;
    r.w.ax = 0x0200;             /* DPMI: get real-mode interrupt vector    */
    r.h.bl = (unsigned char)vec;
    int386(0x31, &r, &r);
    if (r.w.cflag) { return CE_FAIL; }
    if (seg) { *seg = r.w.cx; }
    if (off) { *off = r.w.dx; }
    return CE_OK;
}

#endif /* CASTALIA_DOS */
