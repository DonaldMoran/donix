CC      := clang
LD      := ld.lld
AS      := nasm
OBJCOPY := objcopy
XXD     := xxd

# Cross-compiler for FatFs.
#
# Clang 22.1.8 (Fedora 22.1.8-4.fc44) miscompiles ff.c at every
# optimization level we have tried:
#
#   -O0  read paths break: f_readdir fills FILINFO incorrectly,
#        filenames decode as garbage, f_opendir returns FR_INT_ERR
#   -O1  f_unlink hangs in remove_chain
#   -O2  LLD 22.1.8 truncates instructions in the linked binary
#        (check_fs, move_window)
#
# The cross-GCC produces correct code at -O2. It uses the same
# x86-64 System V ABI as Clang, so a GCC-built ff.o links cleanly
# with the Clang-built kernel objects.
#
# See LLD_BUG_REPORT.md for the full history of both issues.
FATFS_CC := /opt/cross/bin/x86_64-elf-gcc

# Disk layout configuration. Selects which drive FatFs talks to and
# which image layout the boot chain expects.
#
#   FAT_CONFIG=dual   (default) hdd.img master + fat.img slave
#   FAT_CONFIG=single            hdd.img only, FAT partition at LBA 2048
#
# See include/fat_config.h for the layout descriptions.
FAT_CONFIG ?= dual

ifeq ($(FAT_CONFIG),single)
FAT_CFLAGS := -DFAT_CONFIG_SINGLE_DRIVE=1
else
FAT_CFLAGS := -DFAT_CONFIG_SINGLE_DRIVE=0
endif

# Kernel CFLAGS - Added -Ifatfs here so all files can parse ff.h natively
#
# -MMD -MP writes a .d file next to each .o listing the headers that
# .o depends on, and adds phony targets for those headers.  The .d
# files are pulled in by the -include at the bottom of this Makefile.
# Without this, changing a header requires `make clean`; a partial
# rebuild produces a kernel that is a mix of objects compiled against
# two different header versions.
CFLAGS  := -target x86_64-unknown-elf -ffreestanding -O2 -Wall -Wextra \
	-mcmodel=kernel -fno-pic -mno-red-zone -mno-sse -mno-sse2 \
	-mno-avx -mno-mmx -Iinclude -Ifatfs $(FAT_CFLAGS) \
	-MMD -MP

# FatFs CFLAGS - for the cross-GCC.  No -target flag; the cross-GCC
# is already configured for x86_64-unknown-elf.  Same ABI as the
# Clang kernel build, so the objects link together.
FATFS_CFLAGS := -ffreestanding -O2 -Wall -Wextra \
	-mcmodel=kernel -fno-pic -mno-red-zone -mno-sse -mno-sse2 \
	-mno-avx -mno-mmx -Iinclude -Ifatfs $(FAT_CFLAGS) \
	-MMD -MP

LDFLAGS := -nostdlib

# Two limits apply to kernel.bin:
#
# 1. KERNEL_STAGING_LIMIT — the ceiling imposed by stage2.asm's staging
#    layout. 128 KB staged at 0x80000 (PASS 1+2) plus 320 KB from
#    staging B at 0x20000..0x7FFFF (PASS 3..7), for 448 KB total.
#    Exceed this and stage2 silently truncates the tail and the kernel
#    boots to garbage. See stage2.asm.
#
# 2. KERNEL_DISK_LIMIT — the ceiling imposed by the disk layout. The
#    kernel starts at LBA 128; the FAT partition starts at LBA 2048.
#    The kernel region is therefore (2048 - 128) * 512 = 983040 bytes.
#    Exceed this and the kernel would be overwritten by the FAT volume.
#
# The staging limit is the binding one today at 448 KB. The disk limit
# (983 KB) becomes the binding one only if the staging architecture is
# changed again (Option 2 or Option 6 in the roadmap). Both are checked
# below. See MAINTENANCE.md item 1 for the full set of options.
KERNEL_STAGING_LIMIT := 458752
KERNEL_DISK_LIMIT    := 983040

.PHONY: all clean

# Build the kernel image.
all: kernel.bin

# ============================================================
# KERNEL OBJECTS
# ============================================================
OBJS = entry.o ring3_entry.o kmain.o pmm.o vmm.o heap.o idt.o interrupts.o \
	idt_load.o isr.o vga.o fb.o keyboard.o serial.o tss.o gdt.o \
	elf.o test_program_data.o \
	user_syscall.o user_syscall_entry.o process.o string.o scheduler.o \
	context_switch.o ata.o \
	fatfs/ff.o fatfs/ffunicode.o fatfs/diskio.o

kernel.elf: $(OBJS)
	$(LD) $(LDFLAGS) -T linker.ld -o kernel.elf $(OBJS)

kernel.bin: kernel.elf
	$(OBJCOPY) -O binary kernel.elf kernel.bin
	@sz=$$(stat -c %s kernel.bin); \
	if [ $$sz -gt $(KERNEL_STAGING_LIMIT) ]; then \
		echo "============================================================"; \
		echo "ERROR: kernel.bin is $$sz bytes"; \
		echo "       staging limit is $(KERNEL_STAGING_LIMIT) bytes"; \
		echo "       overflow: $$((sz - $(KERNEL_STAGING_LIMIT))) bytes"; \
		echo ""; \
		echo "  stage2.asm can only stage 448 KB (128 KB at 0x80000 plus"; \
		echo "  320 KB at 0x20000..0x7FFFF). The kernel image will be"; \
		echo "  silently truncated and boot to garbage."; \
		echo ""; \
		echo "  Shrink the kernel, or restructure stage2.asm to stream"; \
		echo "  reads (see MAINTENANCE.md item 1)."; \
		echo "============================================================"; \
		exit 1; \
	fi; \
	if [ $$sz -gt $(KERNEL_DISK_LIMIT) ]; then \
		echo "============================================================"; \
		echo "ERROR: kernel.bin is $$sz bytes"; \
		echo "       disk layout limit is $(KERNEL_DISK_LIMIT) bytes"; \
		echo "       (kernel region is LBA 128..2047)"; \
		echo "       overflow: $$((sz - $(KERNEL_DISK_LIMIT))) bytes"; \
		echo ""; \
		echo "  The kernel would overlap the FAT partition at LBA 2048."; \
		echo "  Move the partition start in 05_boot_kernel64/Makefile"; \
		echo "  and update FAT_DISK_TOTAL_SECTORS in include/fat_config.h."; \
		echo "============================================================"; \
		exit 1; \
	fi; \
	echo "kernel.bin: $$sz bytes / staging $(KERNEL_STAGING_LIMIT) / disk $(KERNEL_DISK_LIMIT) (staging headroom: $$(($(KERNEL_STAGING_LIMIT) - sz)) bytes)"

# ============================================================
# TEST PROGRAM
# ============================================================
test_program_data.o: test_program_data.c
	$(CC) $(CFLAGS) -c test_program_data.c -o test_program_data.o

test_program_data.c: test_program
	$(XXD) -i test_program > test_program_data.c

test_program: test_program.o user_linker.ld
	$(LD) -T user_linker.ld -o test_program test_program.o

test_program.o: test_program.asm
	$(AS) -f elf64 test_program.asm -o test_program.o

# ============================================================
# ASSEMBLY RULES
# ============================================================
context_switch.o: context_switch.asm
	$(AS) -f elf64 context_switch.asm -o context_switch.o

entry.o: entry.asm
	$(AS) -f elf64 entry.asm -o entry.o

ring3_entry.o: ring3_entry.S
	$(AS) -f elf64 ring3_entry.S -o ring3_entry.o

idt_load.o: idt_load.asm
	$(AS) -f elf64 idt_load.asm -o idt_load.o

isr.o: isr.asm
	$(AS) -f elf64 isr.asm -o isr.o

user_syscall_entry.o: user_syscall_entry.asm
	$(AS) -f elf64 user_syscall_entry.asm -o user_syscall_entry.o

# ============================================================
# C COMPILATION RULES
# ============================================================
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ============================================================
# FATFS — compiled with the cross-GCC
# ============================================================
#
# These two files are compiled with /opt/cross/bin/x86_64-elf-gcc
# instead of Clang.  See the FATFS_CC comment near the top of this
# Makefile for the full rationale.
#
# The objects use the same x86-64 System V ABI as the Clang-built
# kernel objects, so they link cleanly.  This is a per-file override
# of the default %.o: %.c rule; the specific rule takes precedence.
#
fatfs/ff.o: fatfs/ff.c
	$(FATFS_CC) $(FATFS_CFLAGS) -c fatfs/ff.c -o fatfs/ff.o

fatfs/ffunicode.o: fatfs/ffunicode.c
	$(FATFS_CC) $(FATFS_CFLAGS) -c fatfs/ffunicode.c -o fatfs/ffunicode.o

# diskio.c stays with the default rule above (Clang).
# It is small, hand-written, and does not exhibit the ff.c bug.

# ============================================================
# FILE-SPECIFIC CFLAGS
# ============================================================
vga.o: vga.c
	$(CC) $(CFLAGS) -I. -c vga.c -o vga.o

elf.o: elf.c
	$(CC) $(CFLAGS) -I. -c elf.c -o elf.o

user_syscall.o: user_syscall.c
	$(CC) $(CFLAGS) -I. -c user_syscall.c -o user_syscall.o

# ============================================================
# CLEAN
# ============================================================
clean:
	rm -f *.o *.bin *.elf *.d test_program test_program.bin test_program_data.c
	rm -f fatfs/*.o fatfs/*.d

# ============================================================
# HEADER DEPENDENCY TRACKING
# ============================================================
# Pull in the .d files emitted by -MMD.  If a header changes, make
# sees the dependency and rebuilds the affected .o files.  Without
# this, `make clean` was required after every header change, and a
# partial rebuild produced a kernel that mixed objects compiled
# against two different header versions.
-include $(OBJS:.o=.d)
