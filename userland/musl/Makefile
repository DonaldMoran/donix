# userland/musl/Makefile
#
# Builds every .c under apps/ and tests/ into build/*.elf with one
# uniform rule.  Nothing is expected by name; whatever source files
# are present get built.
#
# Toolchain: ../toolchain/musl-gcc.sh (project-local musl 1.2.5).
#
# Phase B: busybox is built from ../../third_party/busybox/ (gitignored,
# configured with CONFIG_STATIC=y and a minimal applet set) and copied
# into build/ so the image Makefile can treat it like any other ELF.
# The stamp file makes the busybox build run once per clean build,
# and again if third_party/busybox/.config changes.

MUSL_GCC := ../../toolchain/musl-gcc.sh
CFLAGS   := -static -no-pie -O2 -mcmodel=large

BUSYBOX_DIR   := ../../third_party/busybox
BUSYBOX_BIN   := $(BUSYBOX_DIR)/busybox
BUSYBOX_STAMP := build/.busybox.stamp
BUSYBOX_ELF   := build/busybox.elf

APP_SRCS  := $(wildcard apps/*.c)
TEST_SRCS := $(wildcard tests/*.c)
ALL_SRCS  := $(APP_SRCS) $(TEST_SRCS)

APP_ELFS  := $(patsubst apps/%.c, build/%.elf, $(APP_SRCS))
TEST_ELFS := $(patsubst tests/%.c, build/%.elf, $(TEST_SRCS))
ALL_ELFS  := $(APP_ELFS) $(TEST_ELFS) $(BUSYBOX_ELF)

.PHONY: all clean print-elfs

all: $(ALL_ELFS)

build/%.elf: apps/%.c | build
	$(MUSL_GCC) $(CFLAGS) -o $@ $<

build/%.elf: tests/%.c | build
	$(MUSL_GCC) $(CFLAGS) -o $@ $<

# Busybox: run its own make once, drop a stamp, then copy the
# resulting binary into build/.  The stamp has no file prerequisite
# (only the order-only `build`), so make only rebuilds when the
# stamp itself is gone — i.e. after `make clean`.  Adding
# third_party/busybox/.config as a prerequisite forces a rebuild
# whenever the config changes; that's what we want during Phase B.
$(BUSYBOX_STAMP): $(BUSYBOX_DIR)/.config | build
	$(MAKE) -C $(BUSYBOX_DIR) CC=$(abspath $(MUSL_GCC))
	touch $@

$(BUSYBOX_ELF): $(BUSYBOX_STAMP)
	cp $(BUSYBOX_BIN) $@

build:
	mkdir -p build

clean:
	rm -rf build

print-elfs:
	@echo "Apps:"
	@for f in $(APP_ELFS); do echo "  $$f"; done
	@echo "Tests:"
	@for f in $(TEST_ELFS); do echo "  $$f"; done
	@echo "Busybox:"
	@echo "  $(BUSYBOX_ELF)"
