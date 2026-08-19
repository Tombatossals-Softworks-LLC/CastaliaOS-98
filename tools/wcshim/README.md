# tools/wcshim - stand-in headers for the Open Watcom DOS build

Nothing on a Linux runner can compile `src/platform/dos/`. Open Watcom is not
installed, and the files include `<i86.h>`, `<dos.h>`, `<conio.h>` and
`<direct.h>`, none of which exist here. So roughly 2,100 lines of the product
-- every interrupt call, the VESA driver, the mouse, the keyboard, the packet
driver, the Sound Blaster -- have been compiled by nothing, ever, on any
machine in this repository's history. They get one attempt to be right on the
target.

These headers declare just enough of Watcom's surface for `gcc -fsyntax-only`
to parse those files. That catches a typo, a missing declaration, a wrong
struct field, a C89 violation, an argument count. It is a real check and a
cheap one.

**What a pass does NOT mean.** It does not mean Open Watcom will accept the
file. The layouts here are written from Watcom's documented `union REGS` and
`struct find_t`; where they are approximate the difference does not show up in
a syntax check, which is exactly why the check is limited to syntax. Inline
assembly in `#pragma aux` is skipped entirely by gcc, so nothing here says a
word about whether those opcodes assemble.

**If this check reports an error, verify it against Watcom's documentation
before changing the source.** A shim that is wrong about a field name will
report a wrong error, and "fixing" correct code to satisfy a stub is a worse
outcome than the check not existing.
