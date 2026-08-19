# =====================================================================
#  kernel-lab/Makefile - ISOLATED research track. Standalone; the main
#  CastaliaOS build never invokes this. Requires nasm + qemu, which the
#  flagship product does not depend on.
#
#  Targets:
#    make            boot16.bin  (Stage-1 print-only artifact)
#    make image      castalia-klab.img  (Stage-1 loader + Stage-2 PM/GDT/IDT)
#    make run        boot the print-only sector in QEMU
#    make run-image  boot the two-stage protected-mode image in QEMU
#    make clean
# =====================================================================
NASM ?= nasm
QEMU ?= qemu-system-i386

FLOPPY_BYTES = 1474560           # 1.44 MB, so BIOS/QEMU see a normal floppy

.PHONY: all image run run-image clean

all: boot16.bin

boot16.bin: boot16.asm
	$(NASM) -f bin $< -o $@

bootstage.bin: bootstage.asm
	$(NASM) -f bin $< -o $@

stage2.bin: stage2.asm
	$(NASM) -f bin $< -o $@

# Stage-1 in sector 1, Stage-2 in sectors 2.., padded to a 1.44 MB floppy.
image: castalia-klab.img
castalia-klab.img: bootstage.bin stage2.bin
	cat bootstage.bin stage2.bin > $@
	truncate -s $(FLOPPY_BYTES) $@

run: boot16.bin
	$(QEMU) -fda boot16.bin

run-image: castalia-klab.img
	$(QEMU) -fda castalia-klab.img

clean:
	rm -f boot16.bin bootstage.bin stage2.bin castalia-klab.img
