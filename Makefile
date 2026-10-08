# Top-level Makefile for dons-os-x86_64

.PHONY: all clean \
	boot16 run16 \
	boot32 run32 \
	boot64 run64 \
	kernel64 \
	bootkernel64 \
	runkernel64 runkernel64-log runkernel64-debug runkernel64-verbose runkernel64-headless runkernel64-kvm runkernel64-telnet \
	runkernel64-single runkernel64-kvm-single \
	logkernel64 debugkernel64

# Disk layout configuration; propagated to sub-makes.
#   FAT_CONFIG=dual (default) or FAT_CONFIG=single
FAT_CONFIG ?= single

# -------------------------
# Default: build everything
# -------------------------
all: boot16 boot32 boot64 kernel64 bootkernel64

# -------------------------
# 16-bit boot demo (01)
# -------------------------
boot16:
	$(MAKE) -C 01_boot_16bit

run16:
	$(MAKE) -C 01_boot_16bit run

# -------------------------
# 32-bit boot demo (02)
# -------------------------
boot32:
	$(MAKE) -C 02_boot_32bit

run32:
	$(MAKE) -C 02_boot_32bit run

# -------------------------
# 64-bit boot demo (03)
# -------------------------
boot64:
	$(MAKE) -C 03_boot_64bit

run64:
	$(MAKE) -C 03_boot_64bit run64

# -------------------------
# 64-bit kernel build only (04)
# -------------------------
kernel64:
	$(MAKE) -C 04_kernel_64bit FAT_CONFIG=$(FAT_CONFIG)

# -------------------------
# Full 64-bit boot + kernel pipeline (05)
# -------------------------

# Build the kernel with the selected FAT config, then package it.
bootkernel64:
	$(MAKE) -C 04_kernel_64bit FAT_CONFIG=$(FAT_CONFIG)
ifeq ($(FAT_CONFIG),single)
	$(MAKE) -C 05_boot_kernel64 hdd-single.img
else
	$(MAKE) -C 05_boot_kernel64 hdd.img
endif

# ---- QEMU run targets ----
runkernel64:
	$(MAKE) -C 05_boot_kernel64 run

runkernel64-log:
	$(MAKE) -C 05_boot_kernel64 run-log

runkernel64-debug:
	$(MAKE) -C 05_boot_kernel64 run-debug

runkernel64-verbose:
	$(MAKE) -C 05_boot_kernel64 run-verbose

runkernel64-headless:
	$(MAKE) -C 05_boot_kernel64 run-headless

runkernel64-kvm:
	$(MAKE) -C 05_boot_kernel64 run-kvm

runkernel64-telnet:
	$(MAKE) -C 05_boot_kernel64 run-telnet

# ---- Single-drive targets ----
# These rebuild the kernel with FAT_CONFIG=single before running, so the
# diskio.c drive selection matches the single-drive image layout.
runkernel64-single:
	$(MAKE) -C 04_kernel_64bit FAT_CONFIG=single
	$(MAKE) -C 05_boot_kernel64 run-single

runkernel64-kvm-single:
	$(MAKE) -C 04_kernel_64bit FAT_CONFIG=single
	$(MAKE) -C 05_boot_kernel64 run-kvm-single

# ---- Legacy / debug targets ----
logkernel64:
	$(MAKE) -C 05_boot_kernel64 log

debugkernel64:
	$(MAKE) -C 05_boot_kernel64 debug

# -------------------------
# Clean everything
# -------------------------
clean:
	rm -f 04_kernel_64bit/.fat_config_single 04_kernel_64bit/.fat_config_dual
	$(MAKE) -C 01_boot_16bit clean
	$(MAKE) -C 02_boot_32bit clean
	$(MAKE) -C 03_boot_64bit clean
	$(MAKE) -C 04_kernel_64bit clean
	$(MAKE) -C 05_boot_kernel64 clean
