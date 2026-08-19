; =====================================================================
;  stage2.asm - CastaliaOS kernel-lab: Stage-2, 32-bit protected mode with
;               PIC remap + PIT (IRQ0) + PS/2 keyboard (IRQ1) - milestone 4.
;
;  ISOLATED RESEARCH ARTIFACT. Not part of the CastaliaOS v1 product. Loaded at
;  physical 0x8000 by bootstage.asm while still in 16-bit real mode, it:
;    1. enables the A20 line (fast gate, port 0x92);
;    2. loads a flat 32-bit GDT (null / 4 GB code / 4 GB data);
;    3. sets CR0.PE and far-jumps into 32-bit code;
;    4. builds an IDT: the first 32 vectors point at an exception stub, and
;       vectors 0x20/0x21 point at the timer and keyboard interrupt handlers;
;    5. remaps the 8259 PICs so hardware IRQs arrive at 0x20..0x2F instead of
;       colliding with the CPU exception vectors, and unmasks IRQ0 + IRQ1;
;    6. programs PIT channel 0 for a ~100 Hz periodic tick;
;    7. enables interrupts (STI) and idles in HLT. The timer ISR increments a
;       live tick counter on screen; the keyboard ISR shows the last scancode.
;
;  This is the first point the lab kernel does real interrupt-driven I/O:
;  the on-screen tick counter advancing proves IRQ0 is firing and being
;  acknowledged (EOI), and typing in QEMU updates the scancode readout (IRQ1).
;
;  Next steps (README milestone plan): a physical memory map (INT 15h E820),
;  then a simple heap. Kept in pure NASM so the track needs only nasm + qemu.
;
;  Assembled with nasm and boot-tested in QEMU from this repo (headless VGA
;  text-memory dump confirms the banner, an advancing tick counter, and a
;  scancode appearing after an injected keypress). Inspect before relying on it.
; =====================================================================

BITS 16
ORG 0x8000

CODE_SEG equ 0x08                ; selector for gdt_code
DATA_SEG equ 0x10                ; selector for gdt_data

; VGA text-cell offsets (80 cols * 2 bytes/cell = 160 bytes per row).
VGA        equ 0xB8000
ROW0       equ VGA
ROW1       equ VGA + 160
ROW2       equ VGA + 320
ROW3       equ VGA + 480
TICK_CELL  equ ROW2 + 13*2        ; just past the "timer ticks: " label
SCAN_CELL  equ ROW3 + 15*2        ; just past the "last scancode: " label

stage2_start:
    cli

    ; --- enable A20 via the fast gate (port 0x92) ---
    in  al, 0x92
    or  al, 0x02
    and al, 0xFE                 ; make sure we don't accidentally reset
    out 0x92, al

    ; --- load the GDT and enter protected mode ---
    lgdt [gdt_descriptor]
    mov eax, cr0
    or  eax, 0x1                 ; set PE
    mov cr0, eax
    jmp CODE_SEG:pm_entry        ; far jump flushes the prefetch/CS

; ---------------------------------------------------------------------
BITS 32
pm_entry:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x90000             ; a stack well above our image
    cld                          ; string ops go upward (lodsb/stosd below)

    call clear_screen            ; wipe leftover BIOS/PXE text

    ; --- IDT: exceptions 0..31, then the two hardware IRQ handlers ---
    call build_idt
    mov eax, isr_timer
    mov ebx, 0x20                ; IRQ0 -> vector 0x20
    call set_gate
    mov eax, isr_keyboard
    mov ebx, 0x21                ; IRQ1 -> vector 0x21
    call set_gate
    lidt [idt_descriptor]

    ; --- program the interrupt controllers and the timer ---
    call remap_pic
    call init_pit

    ; --- static banners + labels ---
    mov ah, 0x1F                 ; bright white on blue
    mov esi, banner0
    mov edi, ROW0
    call print32
    mov ah, 0x0F                 ; bright white on black
    mov esi, banner1
    mov edi, ROW1
    call print32
    mov ah, 0x1F
    mov esi, lbl_ticks
    mov edi, ROW2
    call print32
    mov ah, 0x1F
    mov esi, lbl_key
    mov edi, ROW3
    call print32

    sti                          ; interrupts on: IRQ0 + IRQ1 now fire
.idle:
    hlt                          ; wake on every interrupt, then sleep again
    jmp .idle

; ---------------------------------------------------------------------
; clear_screen: fill the 80x25 text grid with spaces on blue (attr 0x1F).
clear_screen:
    push edi
    push ecx
    push eax
    mov edi, VGA
    mov eax, 0x1F201F20          ; two cells: space (0x20), white-on-blue (0x1F)
    mov ecx, 80 * 25 / 2         ; 2000 cells = 1000 dwords
    rep stosd
    pop eax
    pop ecx
    pop edi
    ret

; ---------------------------------------------------------------------
; print32: write NUL-terminated string ESI to VGA offset EDI, attribute AH.
print32:
    push eax
    push edi
    push esi
.pl:
    lodsb
    test al, al
    jz .pd
    mov [edi], al
    mov [edi + 1], ah
    add edi, 2
    jmp .pl
.pd:
    pop esi
    pop edi
    pop eax
    ret

; ---------------------------------------------------------------------
; build_idt: fill IDT entries 0..31 with 32-bit interrupt gates -> isr_common.
build_idt:
    mov ecx, 32
    mov edi, idt_table
.fill:
    mov eax, isr_common
    mov word [edi],     ax           ; offset 0..15
    mov word [edi + 2], CODE_SEG     ; selector
    mov byte [edi + 4], 0x00         ; reserved / IST
    mov byte [edi + 5], 0x8E         ; present, DPL0, 32-bit interrupt gate
    shr eax, 16
    mov word [edi + 6], ax           ; offset 16..31
    add edi, 8
    loop .fill
    ret

; set_gate: install a 32-bit interrupt gate. EAX=handler linear offset,
; EBX=vector number (0..255). Clobbers EAX.
set_gate:
    push edi
    mov edi, idt_table
    lea edi, [edi + ebx*8]
    mov word [edi],     ax
    mov word [edi + 2], CODE_SEG
    mov byte [edi + 4], 0x00
    mov byte [edi + 5], 0x8E
    shr eax, 16
    mov word [edi + 6], ax
    pop edi
    ret

; ---------------------------------------------------------------------
; remap_pic: move the master PIC to vectors 0x20..0x27 and the slave to
; 0x28..0x2F (standard ICW1..ICW4 init), then unmask IRQ0 (timer) and IRQ1
; (keyboard) on the master and mask everything else.
remap_pic:
    mov al, 0x11                 ; ICW1: begin init, expect ICW4
    out 0x20, al                 ;   master command
    out 0xA0, al                 ;   slave  command
    mov al, 0x20
    out 0x21, al                 ; ICW2 master: vector base 0x20
    mov al, 0x28
    out 0xA1, al                 ; ICW2 slave:  vector base 0x28
    mov al, 0x04
    out 0x21, al                 ; ICW3 master: slave is wired to IRQ2
    mov al, 0x02
    out 0xA1, al                 ; ICW3 slave:  cascade identity 2
    mov al, 0x01
    out 0x21, al                 ; ICW4 master: 8086/88 mode
    out 0xA1, al                 ; ICW4 slave
    mov al, 0xFC                 ; master mask: 1111_1100 -> IRQ0,IRQ1 enabled
    out 0x21, al
    mov al, 0xFF                 ; slave mask: all masked
    out 0xA1, al
    ret

; init_pit: PIT channel 0, mode 3 (square wave), reload divisor ~= 11932 so the
; 1.19318 MHz input yields about 100 interrupts per second.
init_pit:
    mov al, 0x36                 ; ch0, access lo/hi, mode 3, binary
    out 0x43, al
    mov ax, 11932                ; 1193182 / 100 ~= 11932 (0x2E9C)
    out 0x40, al                 ; low byte
    mov al, ah
    out 0x40, al                 ; high byte
    ret

; ---------------------------------------------------------------------
; isr_timer (IRQ0): bump the tick counter and paint its low 16 bits as four
; hex digits, then acknowledge the interrupt.
isr_timer:
    pushad
    inc dword [ticks]
    mov eax, [ticks]
    mov edi, TICK_CELL
    mov cx, 4                    ; 4 nibbles = low 16 bits, MSB first
    mov bx, ax
.th:
    rol bx, 4                    ; bring the next-highest nibble into bits 3..0
    mov dl, bl
    and dl, 0x0F
    add dl, '0'
    cmp dl, '9'
    jbe .th_put
    add dl, 7                    ; 'A'..'F'
.th_put:
    mov [edi], dl
    mov byte [edi + 1], 0x2F     ; bright white on green
    add edi, 2
    dec cx
    jnz .th
    mov al, 0x20
    out 0x20, al                 ; EOI to the master PIC
    popad
    iret

; isr_keyboard (IRQ1): read the scancode from the 8042 output port and paint it
; as two hex digits, then acknowledge the interrupt. (No scancode->ASCII map yet;
; showing the raw code already proves IRQ1 is delivered and read.)
isr_keyboard:
    pushad
    in  al, 0x60                 ; scancode (also clears the controller's IBF)
    mov bl, al
    mov edi, SCAN_CELL
    mov al, bl                   ; high nibble
    shr al, 4
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .k_hi
    add al, 7
.k_hi:
    mov [edi], al
    mov byte [edi + 1], 0x1E     ; yellow on blue
    mov al, bl                   ; low nibble
    and al, 0x0F
    add al, '0'
    cmp al, '9'
    jbe .k_lo
    add al, 7
.k_lo:
    mov [edi + 2], al
    mov byte [edi + 3], 0x1E
    mov al, 0x20
    out 0x20, al                 ; EOI to the master PIC
    popad
    iret

; isr_common: any of the first 32 CPU exceptions lands here. Mark the screen
; and park (better than a triple-fault reboot).
isr_common:
    mov edi, VGA + 158           ; row 0, last column
    mov byte [edi],     'X'
    mov byte [edi + 1], 0x4F     ; white on red
.hang:
    hlt
    jmp .hang

; ---------------------------------------------------------------------
; Global Descriptor Table: null, 4 GB ring-0 code, 4 GB ring-0 data.
align 8
gdt_start:
    dq 0x0000000000000000            ; null descriptor
gdt_code:
    dw 0xFFFF                        ; limit 0..15
    dw 0x0000                        ; base 0..15
    db 0x00                          ; base 16..23
    db 0x9A                          ; present, ring0, code, exec/read
    db 0xCF                          ; gran=4K, 32-bit, limit 16..19 = F
    db 0x00                          ; base 24..31
gdt_data:
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92                          ; present, ring0, data, read/write
    db 0xCF
    db 0x00
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1       ; limit
    dd gdt_start                     ; base (flat linear address)

idt_descriptor:
    dw 256 * 8 - 1                   ; limit
    dd idt_table                     ; base

ticks     dd 0

banner0   db "CastaliaOS kernel-lab: 32-bit PM + PIC remap + IRQ dispatch.", 0
banner1   db "PIT IRQ0 and PS/2 keyboard IRQ1 are live. Type in QEMU.", 0
lbl_ticks db "timer ticks: ", 0
lbl_key   db "last scancode: ", 0

; 256 zero-initialized IDT gate slots (filled at runtime for 0..31 and the two
; hardware IRQ vectors).
align 8
idt_table:
    times 256 * 8 db 0

    ; Pad Stage-2 to a whole number of 512-byte sectors so the loader's
    ; multi-sector read lands cleanly (STAGE2_SECTORS in bootstage.asm = 16).
    times 8192-($-$$) db 0
