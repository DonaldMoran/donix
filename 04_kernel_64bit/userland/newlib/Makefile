# =======================================================================
# DonsDOS Newlib Userland Makefile
#
# Builds:
#   build/user_shell.elf    — embedded in the kernel image
#   apps/hello.elf          — standalone disk-loaded ELF
#   apps/memtest.elf        — standalone disk-loaded ELF
#   apps/fstest.elf         — standalone disk-loaded ELF
#   apps/multitest.elf      — standalone disk-loaded ELF
#   apps/bigtest.elf        — standalone disk-loaded ELF
#   apps/ls.elf             — standalone disk-loaded ELF
#   apps/cat.elf            — standalone disk-loaded ELF
#   apps/echo.elf           — standalone disk-loaded ELF
# =======================================================================

CC = /opt/cross/bin/x86_64-elf-gcc
LD = /opt/cross/bin/x86_64-elf-gcc
STRIP = /opt/cross/bin/x86_64-elf-strip

CFLAGS = -Iinclude -Iapps/include -O2 -Wall -Wextra -fno-pic -mno-red-zone \
         -mno-sse -mno-sse2 -mno-avx -mno-mmx -mcmodel=large -fno-common -g \
         -fno-stack-protector -Wno-missing-field-initializers -fno-builtin-memset

LDFLAGS = -T ../../user_newlib_linker.ld -nostdlib -mcmodel=large -Wl,--no-warn-rwx-segments -Llib

SHARED = build/arc2/crt0.o build/arc2/syscalls.o build/arc2/reent.o

ALL_APPS = apps/hello.elf apps/memtest.elf apps/fstest.elf \
           apps/multitest.elf apps/bigtest.elf apps/ls.elf \
           apps/cat.elf apps/echo.elf

all: build/user_shell.elf ../../user_shell_data.c $(ALL_APPS)

# -----------------------------------------------------------------------
# Directory creation.
#
# Every .o rule depends on `dirs` as an order-only prerequisite (| dirs),
# so a fresh checkout (or after `make clean`) recreates build/apps and
# build/arc2 before any compile.  Without this, `make memtest` from a
# clean tree fails with "can't create build/apps/memtest.o: No such
# file or directory".
# -----------------------------------------------------------------------
dirs:
	@mkdir -p build/arc2 build/apps

# -----------------------------------------------------------------------
# Shared objects (linked into every .elf)
# -----------------------------------------------------------------------
build/arc2/crt0.o: arc2/crt0.S | dirs
	$(CC) $(CFLAGS) -c arc2/crt0.S -o build/arc2/crt0.o

build/arc2/syscalls.o: arc2/syscalls.c | dirs
	$(CC) $(CFLAGS) -c arc2/syscalls.c -o build/arc2/syscalls.o

build/arc2/reent.o: arc2/reent.c | dirs
	$(CC) $(CFLAGS) -c arc2/reent.c -o build/arc2/reent.o

# -----------------------------------------------------------------------
# user_shell (embedded in the kernel image)
# -----------------------------------------------------------------------
build/apps/user_shell.o: apps/user_shell.c | dirs
	$(CC) $(CFLAGS) -c apps/user_shell.c -o build/apps/user_shell.o

build/user_shell.elf: build/apps/user_shell.o $(SHARED) lib/libc.a
	$(LD) $(LDFLAGS) -o build/user_shell.elf $(SHARED) build/apps/user_shell.o -lc

build/user_shell.elf.stripped: build/user_shell.elf
	$(STRIP) --strip-debug build/user_shell.elf -o build/user_shell.elf.stripped
	@echo "user_shell.elf          : $$(stat -c %s build/user_shell.elf) bytes"
	@echo "user_shell.elf.stripped : $$(stat -c %s build/user_shell.elf.stripped) bytes"

../../user_shell_data.c: build/user_shell.elf.stripped
	cp build/user_shell.elf.stripped build/user_shell.elf
	xxd -i build/user_shell.elf > ../../user_shell_data.c

# -----------------------------------------------------------------------
# Standalone apps (disk-loaded)
# -----------------------------------------------------------------------

build/apps/hello.o: apps/hello.c | dirs
	$(CC) $(CFLAGS) -c apps/hello.c -o build/apps/hello.o
apps/hello.elf: build/apps/hello.o $(SHARED) lib/libc.a
	$(LD) $(LDFLAGS) -o apps/hello.elf $(SHARED) build/apps/hello.o -lc
	$(STRIP) --strip-debug apps/hello.elf -o apps/hello.elf.tmp
	mv apps/hello.elf.tmp apps/hello.elf
	@echo "hello.elf               : $$(stat -c %s apps/hello.elf) bytes (disk-loaded)"

build/apps/memtest.o: apps/memtest.c | dirs
	$(CC) $(CFLAGS) -c apps/memtest.c -o build/apps/memtest.o
apps/memtest.elf: build/apps/memtest.o $(SHARED) lib/libc.a
	$(LD) $(LDFLAGS) -o apps/memtest.elf $(SHARED) build/apps/memtest.o -lc
	$(STRIP) --strip-debug apps/memtest.elf -o apps/memtest.elf.tmp
	mv apps/memtest.elf.tmp apps/memtest.elf
	@echo "memtest.elf             : $$(stat -c %s apps/memtest.elf) bytes (disk-loaded)"

build/apps/fstest.o: apps/fstest.c | dirs
	$(CC) $(CFLAGS) -c apps/fstest.c -o build/apps/fstest.o
apps/fstest.elf: build/apps/fstest.o $(SHARED) lib/libc.a
	$(LD) $(LDFLAGS) -o apps/fstest.elf $(SHARED) build/apps/fstest.o -lc
	$(STRIP) --strip-debug apps/fstest.elf -o apps/fstest.elf.tmp
	mv apps/fstest.elf.tmp apps/fstest.elf
	@echo "fstest.elf              : $$(stat -c %s apps/fstest.elf) bytes (disk-loaded)"

build/apps/multitest.o: apps/multitest.c | dirs
	$(CC) $(CFLAGS) -c apps/multitest.c -o build/apps/multitest.o
apps/multitest.elf: build/apps/multitest.o $(SHARED) lib/libc.a
	$(LD) $(LDFLAGS) -o apps/multitest.elf $(SHARED) build/apps/multitest.o -lc
	$(STRIP) --strip-debug apps/multitest.elf -o apps/multitest.elf.tmp
	mv apps/multitest.elf.tmp apps/multitest.elf
	@echo "multitest.elf           : $$(stat -c %s apps/multitest.elf) bytes (disk-loaded)"

build/apps/bigtest.o: apps/bigtest.c | dirs
	$(CC) $(CFLAGS) -c apps/bigtest.c -o build/apps/bigtest.o
apps/bigtest.elf: build/apps/bigtest.o $(SHARED) lib/libc.a
	$(LD) $(LDFLAGS) -o apps/bigtest.elf $(SHARED) build/apps/bigtest.o -lc
	$(STRIP) --strip-debug apps/bigtest.elf -o apps/bigtest.elf.tmp
	mv apps/bigtest.elf.tmp apps/bigtest.elf
	@echo "bigtest.elf             : $$(stat -c %s apps/bigtest.elf) bytes (disk-loaded)"

build/apps/ls.o: apps/ls.c | dirs
	$(CC) $(CFLAGS) -c apps/ls.c -o build/apps/ls.o
apps/ls.elf: build/apps/ls.o $(SHARED) lib/libc.a
	$(LD) $(LDFLAGS) -o apps/ls.elf $(SHARED) build/apps/ls.o -lc
	$(STRIP) --strip-debug apps/ls.elf -o apps/ls.elf.tmp
	mv apps/ls.elf.tmp apps/ls.elf
	@echo "ls.elf                  : $$(stat -c %s apps/ls.elf) bytes (disk-loaded)"

build/apps/cat.o: apps/cat.c | dirs
	$(CC) $(CFLAGS) -c apps/cat.c -o build/apps/cat.o
apps/cat.elf: build/apps/cat.o $(SHARED) lib/libc.a
	$(LD) $(LDFLAGS) -o apps/cat.elf $(SHARED) build/apps/cat.o -lc
	$(STRIP) --strip-debug apps/cat.elf -o apps/cat.elf.tmp
	mv apps/cat.elf.tmp apps/cat.elf
	@echo "cat.elf                 : $$(stat -c %s apps/cat.elf) bytes (disk-loaded)"

build/apps/echo.o: apps/echo.c | dirs
	$(CC) $(CFLAGS) -c apps/echo.c -o build/apps/echo.o
apps/echo.elf: build/apps/echo.o $(SHARED) lib/libc.a
	$(LD) $(LDFLAGS) -o apps/echo.elf $(SHARED) build/apps/echo.o -lc
	$(STRIP) --strip-debug apps/echo.elf -o apps/echo.elf.tmp
	mv apps/echo.elf.tmp apps/echo.elf
	@echo "echo.elf                : $$(stat -c %s apps/echo.elf) bytes (disk-loaded)"

.PHONY: all dirs hello memtest fstest multitest bigtest ls cat echo clean
hello:     apps/hello.elf
memtest:   apps/memtest.elf
fstest:    apps/fstest.elf
multitest: apps/multitest.elf
bigtest:   apps/bigtest.elf
ls:        apps/ls.elf
cat:       apps/cat.elf
echo:      apps/echo.elf

clean:
	rm -rf build ../../user_shell_data.c \
	       apps/hello.elf apps/hello.elf.tmp \
	       apps/memtest.elf apps/memtest.elf.tmp \
	       apps/fstest.elf apps/fstest.elf.tmp \
	       apps/multitest.elf apps/multitest.elf.tmp \
	       apps/bigtest.elf apps/bigtest.elf.tmp \
	       apps/ls.elf apps/ls.elf.tmp \
	       apps/cat.elf apps/cat.elf.tmp \
	       apps/echo.elf apps/echo.elf.tmp
