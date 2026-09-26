AS := nasm

.PHONY: all clean run run-log run-debug run-verbose run-headless run-kvm run-telnet \
        run-single run-kvm-single watch-serial clean-logs \
        hello-elf memtest-elf fstest-elf multitest-elf bigtest-elf ls-elf cat-elf echo-elf user-elfs

all: boot.bin stage2.bin hdd.img

SINGLE_DRIVE_LBA := 2048
DISK_SECTORS     := 40960

USERLAND_DIR := ../04_kernel_64bit/userland/newlib
HELLO_ELF     := $(USERLAND_DIR)/apps/hello.elf
MEMTEST_ELF   := $(USERLAND_DIR)/apps/memtest.elf
FSTEST_ELF    := $(USERLAND_DIR)/apps/fstest.elf
MULTITEST_ELF := $(USERLAND_DIR)/apps/multitest.elf
BIGTEST_ELF   := $(USERLAND_DIR)/apps/bigtest.elf
LS_ELF        := $(USERLAND_DIR)/apps/ls.elf
CAT_ELF       := $(USERLAND_DIR)/apps/cat.elf
ECHO_ELF      := $(USERLAND_DIR)/apps/echo.elf
HELLO_MUSL    := /tmp/hello_musl
ECHO_MUSL     := /tmp/echo_musl
CAT_MUSL      := /tmp/cat_musl
MUSL_STAT     := /tmp/musl_stat
MUSL_MIN      := /tmp/musl_min
MUSL_PRINTF   := /tmp/musl_printf
MUSL_MALLOC   := /tmp/musl_malloc
PRINTNUM      := /tmp/printnum
BRK_VERIFY    := /tmp/brk_verify
BRKRAW        := /tmp/brkraw
BRKGROW       := /tmp/brkgrow
MUSL_FORK     := /tmp/musl_fork
MUSL_FORK_RAW := /tmp/musl_fork_raw
MUSL_EXEC     := /tmp/musl_exec
MUSL_WAIT     := /tmp/musl_wait
MUSL_READDIR  := /tmp/musl_readdir
MUSL_R10PROBE := /tmp/musl_r10probe
MUSL_SH       := /tmp/musl_sh

boot.bin: boot.asm
	$(AS) -f bin boot.asm -o boot.bin

stage2.bin: stage2.asm
	$(AS) -f bin stage2.asm -o stage2.bin

hello-elf:
	$(MAKE) -C $(USERLAND_DIR) hello
memtest-elf:
	$(MAKE) -C $(USERLAND_DIR) memtest
fstest-elf:
	$(MAKE) -C $(USERLAND_DIR) fstest
multitest-elf:
	$(MAKE) -C $(USERLAND_DIR) multitest
bigtest-elf:
	$(MAKE) -C $(USERLAND_DIR) bigtest
ls-elf:
	$(MAKE) -C $(USERLAND_DIR) ls
cat-elf:
	$(MAKE) -C $(USERLAND_DIR) cat
echo-elf:
	$(MAKE) -C $(USERLAND_DIR) echo

user-elfs: hello-elf memtest-elf fstest-elf multitest-elf bigtest-elf ls-elf cat-elf echo-elf

hdd.img: boot.bin stage2.bin ../04_kernel_64bit/kernel.bin
	rm -f hdd.img
	qemu-img create -f raw hdd.img 20M
	dd if=boot.bin of=hdd.img conv=notrunc
	dd if=stage2.bin of=hdd.img bs=512 seek=1 conv=notrunc
	dd if=../04_kernel_64bit/kernel.bin of=hdd.img bs=512 seek=128 conv=notrunc

hdd-single.img: boot.bin stage2.bin ../04_kernel_64bit/kernel.bin user-elfs
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
	        echo "WARN: $$src not found; $$dst will be missing from the image"; \
	    fi \
	}; \
	mcopy_one "$(HELLO_ELF)"     HELLO.ELF ; \
	mcopy_one "$(HELLO_MUSL)"    HELLO_MUSL.ELF ; \
	mcopy_one "$(ECHO_MUSL)"     ECHO_MUSL.ELF ; \
	mcopy_one "$(CAT_MUSL)"      CAT_MUSL.ELF ; \
	mcopy_one "$(MUSL_STAT)"     MUSL_STAT.ELF ; \
	mcopy_one "$(MEMTEST_ELF)"   MEMTEST.ELF ; \
	mcopy_one "$(FSTEST_ELF)"    FSTEST.ELF ; \
	mcopy_one "$(MULTITEST_ELF)" MULTITEST.ELF ; \
	mcopy_one "$(BIGTEST_ELF)"   BIGTEST.ELF ; \
	mcopy_one "$(LS_ELF)"        LS.ELF ; \
	mcopy_one "$(CAT_ELF)"       CAT.ELF ; \
	mcopy_one "$(ECHO_ELF)"      ECHO.ELF ; \
	mcopy_one "$(MUSL_MIN)"      MUSL_MIN.ELF ; \
	mcopy_one "$(MUSL_PRINTF)"   MUSL_PRINTF.ELF ; \
	mcopy_one "$(MUSL_MALLOC)"   MUSL_MALLOC.ELF ; \
	mcopy_one "$(MUSL_FORK)"       MUSL_FORK.ELF ; \
	mcopy_one "$(MUSL_FORK_RAW)"   MUSL_FORK_RAW.ELF ; \
	mcopy_one "$(PRINTNUM)"      PRINTNUM.ELF ; \
	mcopy_one "$(BRK_VERIFY)"    BRK_VERIFY.ELF ; \
	mcopy_one "$(BRKRAW)"        BRKRAW.ELF ; \
	mcopy_one "$(BRKGROW)"       BRKGROW.ELF ; \
	mcopy_one "$(MUSL_EXEC)"     MUSL_EXEC.ELF ; \
	mcopy_one "$(MUSL_WAIT)"     MUSL_WAIT.ELF ; \
	mcopy_one "$(MUSL_READDIR)"  MUSL_READDIR.ELF ; \
	mcopy_one "$(MUSL_R10PROBE)" MUSL_R10PROBE.ELF ; \
	mcopy_one "$(MUSL_SH)"       MUSL_SH.ELF

	@echo "--- FAT partition contents (hdd-single.img) ---"
	@mdir -i hdd-single.img@@$$(( $(SINGLE_DRIVE_LBA) * 512 )) ::/ || true
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
