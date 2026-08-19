/* i86.h - Open Watcom stand-in. See README.md. Never compiled into anything. */
#ifndef CASTALIA_WCSHIM_I86_H
#define CASTALIA_WCSHIM_I86_H

/* Watcom's 32-bit register union: the 8/16/32-bit views overlay each other. */
struct DWORDREGS { unsigned long eax, ebx, ecx, edx, esi, edi, cflag; };
struct WORDREGS  {
    unsigned short ax, ax_hi, bx, bx_hi, cx, cx_hi, dx, dx_hi;
    unsigned short si, si_hi, di, di_hi;
    unsigned long  cflag;
};
struct BYTEREGS  {
    unsigned char  al, ah; unsigned short ax_hi;
    unsigned char  bl, bh; unsigned short bx_hi;
    unsigned char  cl, ch; unsigned short cx_hi;
    unsigned char  dl, dh; unsigned short dx_hi;
};
union REGS {
    struct DWORDREGS x;
    struct WORDREGS  w;
    struct BYTEREGS  h;
};
struct SREGS { unsigned short cs, ss, ds, es, fs, gs; };

int  int386(int intno, const union REGS *in, union REGS *out);
int  int386x(int intno, const union REGS *in, union REGS *out,
             struct SREGS *sregs);
void segread(struct SREGS *sregs);

/* Flat-model pointer decomposition. The real definitions go through Watcom's
 * _segment type; a syntax check only needs the shape and the result type. */
#define FP_OFF(p) ((unsigned)(unsigned long)(p))
#define FP_SEG(p) ((unsigned)(((unsigned long)(p)) >> 16))

int  inp(int port);
int  outp(int port, int value);
unsigned inpw(int port);
unsigned outpw(int port, unsigned value);
void delay(unsigned ms);

#endif
