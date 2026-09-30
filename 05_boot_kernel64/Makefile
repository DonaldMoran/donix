AS := nasm

.PHONY: all clean run run-log run-debug run-verbose run-headless run-kvm run-telnet \
        run-single run-kvm-single watch-serial clean-logs musl-userland

all: boot.bin stage2.bin hdd.img

SINGLE_DRIVE_LBA := 2048
DISK_SECTORS     := 40960

# ------------------------------------------------------------------
# musl userland.
#
# Sources live in ../userland/musl/{apps,tests}/.  Its Makefile
# builds them into ../userland/musl/build/*.elf.  This Makefile
# invokes that build before staging the FAT image, and copies the
# resulting ELFs onto the FAT.
#
# Replaces the old /tmp/*_musl staging, which depended on
# build_musl_tests.sh having been run by hand first.
# ------------------------------------------------------------------
USERLAND_DIR := ../userland/musl
USERLAND_BIN := $(USERLAND_DIR)/build

# Every ELF the musl userland Makefile produces, listed explicitly
# so the image build has real prerequisites.  If you add a new app
# or test, add it here and to the FAT-staging list below.
USERLAND_ELFS := \
	$(USERLAND_BIN)/hello.elf \
	$(USERLAND_BIN)/echo.elf \
	$(USERLAND_BIN)/cat.elf \
	$(USERLAND_BIN)/ls.elf \
	$(USERLAND_BIN)/memtest.elf \
	$(USERLAND_BIN)/musl_sh.elf \
	$(USERLAND_BIN)/musl_stat.elf \
	$(USERLAND_BIN)/musl_min.elf \
	$(USERLAND_BIN)/musl_printf.elf \
	$(USERLAND_BIN)/musl_malloc.elf \
	$(USERLAND_BIN)/musl_fork.elf \
	$(USERLAND_BIN)/musl_fork_raw.elf \
	$(USERLAND_BIN)/printnum.elf \
	$(USERLAND_BIN)/brk_verify.elf \
	$(USERLAND_BIN)/brkraw.elf \
	$(USERLAND_BIN)/brkgrow.elf \
	$(USERLAND_BIN)/musl_exec.elf \
	$(USERLAND_BIN)/musl_exec2.elf \
	$(USERLAND_BIN)/musl_wait.elf \
	$(USERLAND_BIN)/musl_dup2.elf \
	$(USERLAND_BIN)/musl_dupfd.elf \
	$(USERLAND_BIN)/musl_ids.elf \
	$(USERLAND_BIN)/musl_getcwd.elf \
	$(USERLAND_BIN)/musl_readdir.elf \
	$(USERLAND_BIN)/musl_r10probe.elf \
	$(USERLAND_BIN)/fault_pf.elf \
	$(USERLAND_BIN)/pipe_step1.elf \
	$(USERLAND_BIN)/pipe_step2.elf \
	$(USERLAND_BIN)/busybox.elf

boot.bin: boot.asm
	$(AS) -f bin boot.asm -o boot.bin

stage2.bin: stage2.asm
	$(AS) -f bin stage2.asm -o stage2.bin

# Build the musl userland.  Phony, so it always runs; the
# sub-Makefile's own dependency tracking decides what actually
# needs rebuilding.
musl-userland:
	$(MAKE) -C $(USERLAND_DIR)

hdd.img: boot.bin stage2.bin ../04_kernel_64bit/kernel.bin musl-userland $(USERLAND_ELFS)
	rm -f hdd.img
	qemu-img create -f raw hdd.img 20M
	dd if=boot.bin of=hdd.img conv=notrunc
	dd if=stage2.bin of=hdd.img bs=512 seek=1 conv=notrunc
	dd if=../04_kernel_64bit/kernel.bin of=hdd.img bs=512 seek=128 conv=notrunc

hdd-single.img: boot.bin stage2.bin ../04_kernel_64bit/kernel.bin musl-userland $(USERLAND_ELFS)
	rm -f hdd-single.img
	qemu-img create -f raw hdd-single.img 20M
	dd if=boot.bin of=hdd-single.img conv=notrunc
	dd if=stage2.bin of=hdd-single.img bs=512 seek=1 conv=notrunc
	dd if=../04_kernel_64bit/kernel.bin of=hdd-single.img bs=512 seek=128 conv=notrunc
	mkfs.vfat -F 16 -h $(SINGLE_DRIVE_LBA) -n DONSOS \
	    --offset=$(SINGLE_DRIVE_LBA) hdd-single.img
	@for f in ../test-files/*; do \
	    if [ -f "$$f" ]; then \
	        echo "mcopy $$f"; \
	        mcopy -i hdd-single.img@@$$(( $(SINGLE_DRIVE_LBA) * 512 )) "$$f" ::/ ; \
	    fi \
	done
	@mcopy_one() { \
	    local src="$$1"; local dst="$$2"; \
	    if [ -f "$$src" ]; then \
	        echo "mcopy $$src -> ::/$$dst"; \
	        mcopy -i hdd-single.img@@$$(( $(SINGLE_DRIVE_LBA) * 512 )) "$$src" "::/$$dst" ; \
	    else \
	        echo "ERROR: $$src missing — did the musl userland build fail?"; \
	        exit 1; \
	    fi \
	}; \
	mcopy_one "$(USERLAND_BIN)/hello.elf"          HELLO.ELF ; \
	mcopy_one "$(USERLAND_BIN)/echo.elf"           ECHO.ELF ; \
	mcopy_one "$(USERLAND_BIN)/cat.elf"            CAT.ELF ; \
	mcopy_one "$(USERLAND_BIN)/ls.elf"             LS.ELF ; \
	mcopy_one "$(USERLAND_BIN)/memtest.elf"        MEMTEST.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_stat.elf"      MUSL_STAT.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_min.elf"       MUSL_MIN.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_printf.elf"    MUSL_PRINTF.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_malloc.elf"    MUSL_MALLOC.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_fork.elf"      MUSL_FORK.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_fork_raw.elf"  MUSL_FORK_RAW.ELF ; \
	mcopy_one "$(USERLAND_BIN)/printnum.elf"       PRINTNUM.ELF ; \
	mcopy_one "$(USERLAND_BIN)/brk_verify.elf"     BRK_VERIFY.ELF ; \
	mcopy_one "$(USERLAND_BIN)/brkraw.elf"         BRKRAW.ELF ; \
	mcopy_one "$(USERLAND_BIN)/brkgrow.elf"        BRKGROW.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_exec.elf"      MUSL_EXEC.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_exec2.elf"     MUSL_EXEC2.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_wait.elf"      MUSL_WAIT.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_dup2.elf"      MUSL_DUP2.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_dupfd.elf"     MUSL_DUPFD.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_ids.elf"       MUSL_IDS.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_getcwd.elf"    MUSL_GETCWD.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_readdir.elf"   MUSL_READDIR.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_r10probe.elf"  MUSL_R10PROBE.ELF ; \
	mcopy_one "$(USERLAND_BIN)/fault_pf.elf"       FAULT_PF.ELF ; \
	mcopy_one "$(USERLAND_BIN)/pipe_step1.elf"     PIPE_STEP1.ELF ; \
	mcopy_one "$(USERLAND_BIN)/pipe_step2.elf"     PIPE_STEP2.ELF ; \
	mcopy_one "$(USERLAND_BIN)/musl_sh.elf"        MUSL_SH.ELF

	@echo "--- staging busybox at /bin/busybox ---"
	@mmd -i hdd-single.img@@$$(( $(SINGLE_DRIVE_LBA) * 512 )) ::/bin 2>/dev/null || true
	@if [ -f "$(USERLAND_BIN)/busybox.elf" ]; then \
	    echo "mcopy $(USERLAND_BIN)/busybox.elf -> ::/bin/busybox"; \
	    mcopy -i hdd-single.img@@$$(( $(SINGLE_DRIVE_LBA) * 512 )) \
	        "$(USERLAND_BIN)/busybox.elf" "::/bin/busybox" ; \
	else \
	    echo "ERROR: $(USERLAND_BIN)/busybox.elf missing — did the busybox build fail?"; \
	    exit 1; \
	fi

	@echo "--- FAT partition contents (hdd-single.img) ---"
	@mdir -i hdd-single.img@@$$(( $(SINGLE_DRIVE_LBA) * 512 )) ::/ || true
	@echo "--- /bin contents ---"
	@mdir -i hdd-single.img@@$$(( $(SINGLE_DRIVE_LBA) * 512 )) ::/bin || true
	@echo "--- verifying FAT boot sector at LBA $(SINGLE_DRIVE_LBA) ---"
	@dd if=hdd-single.img bs=512 skip=$(SINGLE_DRIVE_LBA) count=1 2>/dev/null | \
	    xxd | head -2
	@echo "--- hidden_sectors field (BPB offset 0x1C), expect 0008 0000 ---"
	@dd if=hdd-single.img bs=1 skip=$$(( $(SINGLE_DRIVE_LBA) * 512 + 28 )) \
	    count=4 2>/dev/null | xxd

run:
	qemu-system-x86_64 -cpu max \
		-drive file=hdd.img,format=raw,if=ide,index=0 \
		-drive file=fat.img,format=raw,if=ide,index=1 \
		-serial stdio -display sdl -monitor none
run-single: hdd-single.img
	qemu-system-x86_64 -cpu max \
		-drive file=hdd-single.img,format=raw,if=ide,index=0 \
		-serial stdio -display sdl -monitor none
run-log:
	qemu-system-x86_64 -cpu max -drive file=hdd.img,format=raw -serial file:serial.log -display sdl
run-debug:
	qemu-system-x86_64 -cpu max -drive file=hdd.img,format=raw -serial stdio -display sdl -s -S
run-verbose:
	qemu-system-x86_64 -cpu max -drive file=hdd.img,format=raw -serial stdio -display sdl \
		-d int,cpu_reset,guest_errors,page -no-reboot -no-shutdown -D qemu_debug.log
run-headless:
	qemu-system-x86_64 -cpu max -drive file=hdd.img,format=raw -serial stdio -nographic -monitor none
run-kvm:
	qemu-system-x86_64 -drive file=hdd.img,format=raw,if=ide,index=0 \
		-drive file=fat.img,format=raw,if=ide,index=1 \
		-serial stdio -display sdl -enable-kvm -cpu host
run-kvm-single: hdd-single.img
	qemu-system-x86_64 -drive file=hdd-single.img,format=raw,if=ide,index=0 \
		-serial stdio -display sdl -enable-kvm -cpu host
run-telnet:
	qemu-system-x86_64 -cpu max -drive file=hdd.img,format=raw -serial telnet::4444,server,nowait -display sdl
run-gdb-single: hdd-single.img
	qemu-system-x86_64 -cpu max \
		-drive file=hdd-single.img,format=raw,if=ide,index=0 \
		-serial stdio -display sdl -monitor none \
		-s -S
run-debug-log: hdd-single.img
	qemu-system-x86_64 -cpu max \
		-drive file=hdd-single.img,format=raw,if=ide,index=0 \
		-serial stdio -display sdl -monitor none \
		-d in_asm,cpu -D /tmp/qemu-log.txt
log:
	qemu-system-x86_64 -cpu max -drive file=hdd.img,format=raw \
		-d int,cpu_reset,guest_errors,unimp,page -no-reboot -no-shutdown \
		-serial file:qemu.log -D qemu_debug.log -monitor stdio
debug:
	qemu-system-x86_64 -cpu max -drive file=hdd.img,format=raw \
		-s -S -no-reboot -no-shutdown -d int,cpu_reset,guest_errors \
		-serial file:qemu.log -monitor stdio

watch-serial:
	tail -f serial.log
clean-logs:
	rm -f serial.log qemu.log qemu_debug.log

clean: clean-logs
	rm -f boot.bin stage2.bin hdd.img hdd-single.img
	$(MAKE) -C $(USERLAND_DIR) clean
