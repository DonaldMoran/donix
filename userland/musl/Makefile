# userland/musl/Makefile
#
# Builds every .c under apps/ and tests/ into build/*.elf with one
# uniform rule.  Nothing is expected by name; whatever source files
# are present get built.
#
# Toolchain: ../toolchain/musl-gcc.sh (project-local musl 1.2.5).

MUSL_GCC := ../../toolchain/musl-gcc.sh
CFLAGS   := -static -no-pie -O2 -mcmodel=large

APP_SRCS  := $(wildcard apps/*.c)
TEST_SRCS := $(wildcard tests/*.c)
ALL_SRCS  := $(APP_SRCS) $(TEST_SRCS)

APP_ELFS  := $(patsubst apps/%.c, build/%.elf, $(APP_SRCS))
TEST_ELFS := $(patsubst tests/%.c, build/%.elf, $(TEST_SRCS))
ALL_ELFS  := $(APP_ELFS) $(TEST_ELFS)

.PHONY: all clean print-elfs

all: $(ALL_ELFS)

build/%.elf: apps/%.c | build
	$(MUSL_GCC) $(CFLAGS) -o $@ $<

build/%.elf: tests/%.c | build
	$(MUSL_GCC) $(CFLAGS) -o $@ $<

build:
	mkdir -p build

clean:
	rm -rf build

print-elfs:
	@echo "Apps:"
	@for f in $(APP_ELFS); do echo "  $$f"; done
	@echo "Tests:"
	@for f in $(TEST_ELFS); do echo "  $$f"; done
