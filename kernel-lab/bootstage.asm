; =====================================================================
;  bootstage.asm - CastaliaOS kernel-lab: Stage-1 loader (milestone 2).
;
;  ISOLATED RESEARCH ARTIFACT. Not part of the CastaliaOS v1 product and not
;  referenced by the main build. Where boot16.asm only prints and halts, this
;  stage-1 loads Stage-2 from disk and hands control to it -- the first step of
;  the milestone plan in README.md.
;
;  It loads STAGE2_SECTORS sectors starting at LBA sector 2 (the sectors right
;  after this boot sector) to physical 0x0000:0x8000 via BIOS INT 13h, then
;  far-jumps into Stage-2. On any disk error it prints 'E' and halts.
;
;  Assemble/boot (needs nasm + qemu, which the main project does NOT):
;      nasm -f bin bootstage.asm -o bootstage.bin
;      (concatenate with stage2.bin into a floppy image; see Makefile)
;
;  UNVERIFIED from this repository's environment (no assembler here). Inspect
;  and test in QEMU/Bochs before relying on it.
; =====================================================================

BITS 16
ORG 0x7C00

STAGE2_SEG       equ 0x0000
STAGE2_OFF       equ 0x8000       ; Stage-2 loads here (physical 0x08000)
STAGE2_SECTORS   equ 16           ; must cover stage2.bin (8 KB here)

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00               ; stack grows down from just below us
    sti

    mov [boot_drive], dl         ; BIOS passes the boot drive in DL

    mov si, msg_load
    call print

    ; Reset the disk system first (AH=0).
    xor ax, ax
    mov dl, [boot_drive]
    int 0x13
    jc  disk_error

    ; Read STAGE2_SECTORS sectors, LBA 2 (CHS 0/0/2) -> STAGE2_SEG:STAGE2_OFF.
    mov ax, STAGE2_SEG
    mov es, ax
    mov bx, STAGE2_OFF
    mov ah, 0x02                 ; BIOS read sectors
    mov al, STAGE2_SECTORS
    mov ch, 0x00                 ; cylinder 0
    mov cl, 0x02                 ; start sector 2 (1-based)
    mov dh, 0x00                 ; head 0
    mov dl, [boot_drive]
    int 0x13
    jc  disk_error
    cmp al, STAGE2_SECTORS       ; AL = sectors actually read
    jne disk_error

    mov si, msg_go
    call print

    ; Hand off to Stage-2 (still real mode). It sets up protected mode.
    jmp STAGE2_SEG:STAGE2_OFF

disk_error:
    mov si, msg_err
    call print
.hang:
    hlt
    jmp .hang

; Print the NUL-terminated string at DS:SI via BIOS teletype.
print:
    push ax
    push bx
.loop:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp .loop
.done:
    pop bx
    pop ax
    ret

boot_drive db 0
msg_load   db "kernel-lab stage 1: loading stage 2...", 13, 10, 0
msg_go     db "ok, entering stage 2.", 13, 10, 0
msg_err    db "E: disk read failed.", 13, 10, 0

    times 510-($-$$) db 0
    dw 0xAA55
