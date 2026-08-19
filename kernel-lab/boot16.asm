; =====================================================================
;  boot16.asm - CastaliaOS kernel-lab: minimal Stage-1 boot sector.
;
;  ISOLATED RESEARCH ARTIFACT. Not part of the CastaliaOS v1 product and not
;  referenced by the main build. It prints a banner in 16-bit real mode via
;  BIOS teletype (INT 10h, AH=0Eh) and then halts. This is intentionally the
;  simplest possible starting point for the native-kernel track.
;
;  Assemble/boot (needs nasm + qemu, which the main project does not):
;      nasm -f bin boot16.asm -o boot16.bin
;      qemu-system-i386 -fda boot16.bin
;
;  UNVERIFIED from this repository's environment (no assembler here). Inspect
;  and test before relying on it. See README.md.
; =====================================================================

BITS 16
ORG 0x7C00                       ; BIOS loads the boot sector here

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00               ; stack just below the loaded sector
    sti

    mov si, msg
.print:
    lodsb                        ; AL = [DS:SI], SI++
    test al, al
    jz .done
    mov ah, 0x0E                 ; teletype output
    mov bh, 0x00                 ; page 0
    mov bl, 0x07                 ; light-gray attribute
    int 0x10
    jmp .print

.done:
    hlt
    jmp .done                    ; park the CPU

msg db "CastaliaOS kernel-lab: stage 1 boot OK.", 13, 10
    db "This is the research track, not the v1 desktop.", 13, 10, 0

    times 510-($-$$) db 0        ; pad to 510 bytes
    dw 0xAA55                    ; boot signature
