# donix
### A Unix-like x86_64 OS for static musl binaries — forked from dons-os (MIT Licensed)

**donix** is a fork of [dons-os](https://github.com/DonaldMoran/dons-os-x86_64),
re-targeted to run **static musl-linked binaries** and speak the **Linux
x86_64 syscall ABI natively**. It keeps the full boot chain — 16-bit real
mode → 32-bit protected mode → 64-bit long mode — and the C-based
higher-half kernel from dons-os: interrupts, timer, keyboard, PMM, VMM,
preemptive scheduler, ELF loader, blocking I/O, ATA PIO driver, and a
FAT16 filesystem with long filename support.

What changed since dons-os:

- Syscall numbers are Linux x86_64 numbers, not dons-os-private ones.
- The userland C library is **musl 1.2.5**, built from source into a
  project-local tree, not newlib.
- The default shell is `musl_sh`, a musl-linked static ELF loaded from
  `0:/MUSL_SH.ELF`. There is no embedded shell and no fallback.
- The newlib userland — the shell, the C library, the syscall shims,
  and the `arc2/` runtime — has been fully retired.

Related documents:

- [`handoff.md`](handoff.md) — project strategy and session-by-session history
- [`ROADMAP.md`](ROADMAP.md) — planned features and completed milestones
- [`OSDev_Checklist.md`](OSDev_Checklist.md) — capability tracking
- [`MAINTENANCE.md`](MAINTENANCE.md) — known debt, latent bugs, cleanup work
- [`LLD_BUG_REPORT.md`](LLD_BUG_REPORT.md) — toolchain bug reports and workarounds
- [`migration-tags.txt`](migration-tags.txt) — the A1–A5 working tag map (newlib → musl)

The dons-os repo remains intact as the recovery point for the pre-migration
state; its `v0.5.4` tag is the last version before donix began.

---

## Repository structure

### Bootloaders

- **01_boot_16bit** — BIOS boot sector, INT 0x10 text, INT 0x13 disk loading
- **02_boot_32bit** — A20 enable, GDT, protected mode, VGA text
- **03_boot_64bit** — PAE paging, PML4/PDPT/PD/PT, IA32_EFER.LME, long-mode entry

### Kernel

- **04_kernel_64bit** — Standalone 64-bit kernel (ELF → flat), IDT, ISR
  stubs, PIC remap, PIT timer, IRQ0 tick, IRQ1 keyboard, PMM, VMM, VGA,
  serial, kernel shell, heap allocator with validator and stress test,
  Linux x86_64 syscall dispatch, ELF loader, process system, preemptive
  scheduler, blocking I/O, ATA PIO driver, FatFs integration, GDT/TSS
  diagnostics.
- **04_kernel_64bit/fatfs** — Vendored FatFs R0.16 plus the `diskio.c`
  shim that maps FatFs onto the ATA PIO driver. `ff.c` and
  `ffunicode.c` are compiled with the cross-GCC (see Known Limitations).
- **04_kernel_64bit/include/fat_config.h** — The single compile-time
  switch that selects dual-drive vs single-drive storage.

### Userland

The musl userland is built from source against the project-local musl
tree. It lives in two places:

- **`build_musl_tests.sh`** — a tracked build script that compiles every
  musl-linked ELF the FAT image carries. Each program's source is a
  heredoc inside the script; the resulting binaries land in `/tmp/`,
  from which the image build copies them to the FAT partition.
- **`toolchain/`** — the tracked scripts that build and use musl:
  - `install_musl.sh` — clones upstream musl at v1.2.5 and installs it
    into `third_party/musl-install/` (gitignored).
  - `musl-gcc.sh` — a two-line wrapper that invokes `gcc -specs
    <repo>/third_party/musl-install/lib/musl-gcc.specs`.

There is **no `userland/` tree** in the donix tree today. The dons-os
newlib userland (`userland/newlib/`) was deleted at A5 step 7 as part
of retiring newlib. A proper `userland/musl/` tree is planned; the
project's current state and near-term plan are described in
[`handoff.md`](handoff.md).

### Other

- **05_boot_kernel64** — Full boot chain: stage2 loads the kernel via
  multi-pass segment incrementing, enters long mode, jumps to `_start`.
- **test-files** — Files copied into the single-drive FAT partition at
  image-build time.
- **`run`** — One-line-per-option QEMU launch script; uncomment the
  option you want and run `./run`.

The top-level Makefile builds and runs all components.

---

## Building & running

### Prerequisites

- `nasm` (assembler)
- `clang` and `ld.lld` (kernel)
- `/opt/cross/bin/x86_64-elf-gcc` (FatFs — see Known Limitations)
- `qemu-system-x86_64`
- `mtools` (`mcopy`, `mdir`, `mkfs.vfat`)
- `xxd` (for the embedded test program)
- Network access on first setup, to clone musl

### One-time setup

```
./toolchain/install_musl.sh       # clone and build musl 1.2.5
./build_musl_tests.sh             # build all musl userland binaries
```

Both are idempotent. `install_musl.sh` bails if `third_party/musl-src/`
is not exactly at `v1.2.5`; delete that directory and re-run if you want
to force a rebuild. `build_musl_tests.sh` writes its outputs to
`/tmp/`, which Fedora does **not** persist across reboots — re-run it
after any reboot before booting the OS if the musl binaries are missing.

### Build and run

Build everything:

```
make all
```

Build and run the full long-mode OS (dual-drive, the default):

```
make bootkernel64
make runkernel64
```

Build and run the single-drive layout:

```
make clean && make FAT_CONFIG=single && make runkernel64-kvm-single
```

Or, with the convenience script at the repo root:

```
./run
```

The script parses its own `menu()` function and executes the single
uncommented line. To switch tasks, move the `#` from one line to another.
Never uncomment two lines at once. It runs a full `make clean && make`
and boots the selected target — one edit, one command.

Run individual boot demos:

```
make run16
make run32
make run64
```

Run with QEMU debug logging:

```
make logkernel64
```

This boots:

1. BIOS → stage1
2. stage1 loads stage2
3. stage2 builds page tables with recursive mapping
4. stage2 reads the 64-bit kernel off disk in safe 128-sector chunks
   (64 KB steps) to completely bypass real-mode address wrap-around
   ceilings
5. stage2 enters long mode
6. stage2 jumps to kernel at `0xFFFFFFFF80100000` (higher-half)
7. kernel executes `_start` → `kmain`
8. kernel initializes IDT, PIC, PIT, keyboard, PMM, VMM, Heap, ATA,
   FatFs, syscalls, ELF loader, process system, preemptive scheduler
9. kernel presents the boot-time shell choice (see below)
10. either the kernel shell or the user shell starts, depending on the
    operator's key

---

## Boot flow

On boot, the kernel prints a boot prompt:

```
Press 'k' for kernel shell, else launching user shell...
```

- **Press `k`** within ~2 seconds → the **kernel shell** starts. It is
  a diagnostic console: it can list processes, run scheduler tests,
  dump the GDT and TSS, list the FAT volume, and run the self-test
  suite.
- **Any other key, or no key within 2 seconds** → the **user shell**
  starts directly.

The user shell is `musl_sh`, a static musl-linked ELF loaded from
`0:/MUSL_SH.ELF` on the FAT partition. It runs as an ordinary Ring 3
process (its own page tables, its own user and kernel stacks). Once
the user shell is running, **the kernel shell is not reachable again
without a reboot**. This is intentional: after boot, the user shell is
the only interactive console.

If `MUSL_SH.ELF` cannot be read from the FAT, or the ELF fails to load,
the kernel prints a serial PANIC and halts. There is no embedded
fallback shell any more; the newlib shell and its `build_user_shell_elf`
blob were removed at A5 step 6.

If the user shell exits, `sys_exit` halts the CPU. Kernel diagnostic
processes spawned by the kernel shell (`runproc`, `testyield`, the
self-test's fault triggers) return to the kernel shell on exit, because
that is where the operator needs to be.

---

## Two storage configurations

The kernel supports two disk layouts. They are mutually exclusive at
build time; the choice is a single make variable.

### Dual-drive (default)

```
hdd.img (master)              fat.img (slave)
───────────────               ───────────────
LBA 0..127   boot chain       LBA 0..      FAT16 volume
LBA 128..    kernel
```

The boot chain and kernel occupy the master disk. FatFs lives on a
separate slave disk, a whole-disk FAT16 volume with no partition table.
This layout is easy to debug because the OS disk and the data disk are
physically separate.

```
make clean && make && make runkernel64-kvm
```

### Single-drive

```
hdd.img (master only)
─────────────────────
LBA 0..127       boot chain
LBA 128..2047    kernel (up to 960 KB)
LBA 2048..       FAT16 partition (hidden_sectors = 2048)
```

Everything lives on one disk. The FAT16 partition starts at LBA 2048.
FatFs talks to the master drive and `diskio.c` adds the partition
offset to every sector number it hands to the ATA layer.

```
make clean && make FAT_CONFIG=single && make runkernel64-kvm-single
```

### The switch

`FAT_CONFIG=dual` (default) or `FAT_CONFIG=single` selects the
configuration. The Makefile passes `-DFAT_CONFIG_SINGLE_DRIVE=0|1`,
and `include/fat_config.h` turns that into:

- `FAT_DRIVE` — `ATA_DRIVE_SLAVE` in dual, `ATA_DRIVE_MASTER` in single.
- `FAT_PARTITION_OFFSET` (in `diskio.c`) — 0 in dual, 2048 in single.
- `FAT_VOLUME_SECTORS` — the size FatFs reports for free-space accounting.

The kernel source is identical between the two configurations. Only the
drive selection and partition offset differ.

### Why the offset lives in `diskio.c`, not the BPB

`FF_MULTI_PARTITION` is 0 in `ffconf.h`, which means FatFs is not
partition-aware. It treats the FAT volume as starting at LBA 0 of the
physical drive and does not consult the partition table or the BPB's
`hidden_sectors` field. `diskio.c` is therefore the correct place to
translate volume-relative sector numbers into device LBAs.

An earlier iteration assumed FatFs would read `hidden_sectors` and
skipped the offset. The mount then failed with `FR_NO_FILESYSTEM` (13),
because FatFs read the boot sector at LBA 0 (which is the boot chain,
not a FAT boot sector) instead of the FAT boot sector at LBA 2048. The
offset must be applied in `diskio.c` for a partitioned volume with
`FF_MULTI_PARTITION = 0`.

### Design note: why the kernel isn't in the FAT filesystem

A reader may wonder why the kernel lives at a fixed LBA rather than as a
file in the FAT partition.

The boot chain predates FatFs by roughly ten commits. Adding a FAT
reader to `stage2.asm` would have meant writing a FAT12/16/32 reader in
16-bit real mode, in a 34 KB budget, without a heap. That was a
multi-week detour for zero new features. The kernel is at a fixed LBA
because the boot chain loads it before any driver exists — this is what
every BIOS-boot OS does. FatFs is for data: test files, user programs,
and the shell.

If the project ever moves to Limine or GRUB2, the kernel becomes a file
loaded by the bootloader, and this distinction disappears. It is on the
roadmap but not urgent.

---

## Userland C library (musl)

donix ships with **musl 1.2.5**, built from source into
`third_party/musl-install/`. User programs are ordinary C, compiled
with the project-local musl toolchain, statically linked against
`libc.a`, and loaded from the kernel as ELF64 binaries.

This is a substantial capability: user programs can use the standard C
library rather than a hand-rolled mini-libc. `printf`, `malloc`/`free`,
`memcpy`, `str*`, `atoi`, and the rest are available in Ring 3.

### How it is wired up

- **`third_party/musl-src/`** — a git clone of upstream musl at v1.2.5.
  Gitignored. Created by `toolchain/install_musl.sh`.
- **`third_party/musl-install/`** — the install tree: headers,
  `libc.a`, `crt1.o` and the other `crt*.o` files, and the
  `musl-gcc.specs` file. Also gitignored.
- **`toolchain/install_musl.sh`** — clones and builds musl. See the
  script header for the flags and the reason they are all required.
- **`toolchain/musl-gcc.sh`** — a two-line wrapper around the specs
  file. Every musl binary is built with this wrapper.
- **`build_musl_tests.sh`** — the build recipe for every musl ELF in
  the tree. Sources are heredocs; outputs go to `/tmp/`.

### What works end-to-end

- ✅ `printf` — output reaches VGA and serial from Ring 3
- ✅ `malloc` / `free` — backed by `brk` → page mapping (see `memtest`)
- ✅ `memcpy`, `memset`, `strcmp`, and the rest of the string functions
- ✅ `errno`
- ✅ `open` / `close` / `read` / `write` / `unlink` on FAT files from
  Ring 3
- ✅ `fork` / `execve` / `wait4` — the full shell execution path
- ✅ `opendir` / `readdir` / `closedir` via Linux `open(O_DIRECTORY)` +
  `getdents64`
- ✅ `stat` / `fstat`
- ✅ `brk` / `mmap` / `munmap`
- ✅ Static linking; no dynamic linking

### How to build a user program

There is no separate userland tree today. To add a program:

1. Add a heredoc to `build_musl_tests.sh` alongside the existing tests
   and apps.
2. Add an `mcopy_one` line to `05_boot_kernel64/Makefile` naming the
   binary's FAT destination.
3. Run `./build_musl_tests.sh` and then `./run`.

Any program that reads a filename argument should take a **bare
filename** on `argv[1]` (e.g. `hello-world.txt`) and prepend `0:/`
itself, because `musl_sh` only normalizes `argv[0]`. See `cat_musl` in
`build_musl_tests.sh` for the pattern.

### What's not there (yet)

- **No dynamic linking.** Programs are statically linked against
  `libc.a`. A shared library / dynamic loader would be a separate
  project.
- **No search path.** Programs are loaded from `0:/NAME.ELF` on the
  FAT volume at runtime; there is no `PATH` variable and no shebang
  support.
- **No `pthread`, no signals, no `select`/`poll`.** The kernel does not
  implement the syscalls those need. Busybox is the next big target and
  will surface the actual gaps.
- **`/proc`, `/dev`, `/sys` are absent.** Programs that read them, or
  that rely on `/dev/tty` or `/dev/null`, will not find them.

---

## ELF loader

The ELF loader is fully functional and can execute user programs loaded
from the FAT volume at runtime:

- ✅ Parses ELF64 headers and program headers
- ✅ Maps LOAD segments with correct permissions (Read, Write, Execute, User)
- ✅ Allocates and maps user stack pages
- ✅ Transitions to user mode via `iretq` with proper selectors (CS=0x33, SS=0x2B)
- ✅ Sets `IOPL=3` for user I/O access
- ✅ Page table execute permissions at all levels (PML4 → PDPT → PD → PT)
- ✅ Uses HHDM for safe user-space memory access from kernel
- ✅ **User programs loaded into a dedicated user address space** (linked at `0x400000`)
- ✅ Works reliably on first boot (bootloader identity-mapping handled)
- ✅ **Linux `execve(59)`** — replaces the calling process's address space in place
- ✅ **Linux `wait4(61)`** — blocking wait, reap, parent-pid preserved across `execve`
- ✅ **argv passing** — `argc` and `argv` reach `main()` in the child
- ✅ **`fork(57)`** — a child resumes at the parent's user RIP with `%rax = 0`

**Where programs live:**

- The **shell** is `MUSL_SH.ELF` on the FAT partition. Every other
  user program is a standalone ELF on the FAT partition, loaded by name
  at runtime.

---

## Kernel shell

Reached by pressing `k` at the boot prompt. It provides a diagnostic
console with a `>` prompt.

| Command | Description |
|---------|-------------|
| `help` | Show available commands |
| `clear` | Clear the screen |
| `version` | Show version information |
| `info` | Display system information (PML4, kernel addresses, E820 entries) |
| `mem` | Display memory information (usable/reserved RAM) |
| `reboot` | Reboot the system (Ring 0 supervisor sequence) |
| `pmmtest` | Test Physical Memory Manager |
| `test` | Test exception handlers (#DE, #PF, #GP) |
| `vmmtest` | Test Virtual Memory Manager with HHDM |
| `serialtest` | Test serial output debugging |
| `heapstat` | Show heap statistics (used/free/total memory) |
| `maptest` | Test page mapping |
| `testrec` | Test recursive mapping address |
| `heaptest` | Test heap allocator with memory reuse |
| `heapcheck` | Walk the heap block list and check every invariant |
| `heapstress` | Deterministic alloc/free pattern that forces multiple extensions |
| `nxtest` | Verify NX (No Execute) bit support |
| `syscall` | Test system call interface |
| `elfload` | Load and run an embedded ELF test program from user mode Ring 3 |
| `proclist` | List all processes (idle + created) |
| `proccreate` | Create a test process (PCB infrastructure) |
| `vmmclone` | Clone the current page table (test process isolation) |
| `runproc` | Create and execute a test kernel process; returns to the kernel shell on exit |
| `schstat` | Show scheduler statistics |
| `testyield` | Cooperative yield test: two kernel processes alternate via `process_yield` |
| `gdtdump` | Decode and print the current GDT descriptors |
| `tssdump` | Print current TSS fields |
| `selftest` | Run the kernel self-test suite (17 tests) and print a pass/fail summary |
| `atatest` | Read-only ATA diagnostic: MBR signature, kernel header, model strings |
| `fatmount` | Mount the FAT volume and report status |
| `fatls` | List the root directory of the FAT volume |
| `fatcat <file>` | Dump the contents of a FAT file to VGA and serial |

Note that the dons-os `usershell` command is gone. The user shell is
launched automatically at boot, not from the kernel shell; a read or
load failure is a hard serial PANIC.

---

## User shell (`musl_sh`)

Launched by the boot-time default (no `k` key). It is a musl-linked
static ELF loaded from `0:/MUSL_SH.ELF`.

`musl_sh` is a simple REPL:

- Reads a line from stdin byte-at-a-time and echoes as typed.
- Tokenizes the line on whitespace.
- Normalizes the first token to `0:/NAME.ELF` unless it already
  contains `:/`, then `execve`s it.
- Passes the remaining tokens as `argv[1..n]` verbatim. It does **not**
  normalize file arguments — a program that takes a filename must
  prepend `0:/` itself. See `cat_musl` in `build_musl_tests.sh`.
- Blocks in `wait4` after the `execve`, so parent and child output are
  serialized.

There are no built-in commands. Every command is an external ELF on the
FAT volume.

**Example session:**

```
donix> hello
hello from donix (musl)
donix> echo hi
hi
donix> cat hello-world.txt
Hello from the single-drive FAT partition.
This file was copied in by mcopy at image-build time.
If fatcat can read this, the partition offset and hidden_sectors
are both correct.
donix> ls
FILE   HELLO-WORLD.TXT  (180 bytes)
FILE   HELLO.ELF  (18752 bytes)
FILE   ECHO.ELF  (12864 bytes)
...

21 file(s), 0 directory(ies)
donix> memtest
[memtest] musl malloc/free via mmap
  malloc(4096) = 0x8010000020
  wrote 4096 bytes
  readback OK
  free() returned
  second malloc(8192) = 0x8010000030
  wrote 8192 bytes to second buffer
[memtest] PASS
donix>
```

**What's not there:**

- **No arguments to built-ins** (there are no built-ins).
- **No quoting or globbing.** The tokenizer splits on spaces and tabs
  only. `cat "file with spaces.txt"` does not work as written.
- **No pipes or redirection.** `cat file | grep foo` needs `pipe(2)`
  and `dup2(2)`, which are not implemented.
- **No `cd` or relative paths.** All paths are absolute `0:/...`.
- **No command history.** The line editor handles backspace only.
- **No signals or Ctrl-C.** A hung external program blocks the shell
  in `wait4` forever; the only recovery is a reboot.
- **Backspace echo bug.** When a typed line contains backspaces, the
  *stored* line is correct (the kernel trace shows the right argv
  reaches `execve`), but the display can be scrambled. Cosmetic.

---

## Debug mode (QEMU)

Debugging early boot code is notoriously difficult. QEMU's built-in
logging makes it dramatically easier to diagnose faults, paging issues,
and incorrect mode transitions.

Quick debug run:

```
make logkernel64
```

Manual debug command:

```
qemu-system-x86_64 \
  -drive file=hdd.img,format=raw \
  -serial stdio \
  -d int,cpu_reset \
  -no-reboot \
  -no-shutdown
```

### What this enables

- **`-d int`** — logs all CPU interrupts (hardware + software)
- **`-d cpu_reset`** — logs CPU resets (critical for diagnosing triple faults)
- **`-d guest_errors`** — logs guest errors (page faults, etc.)
- **`-d page`** — logs page faults
- **`-no-reboot`** — prevents QEMU from instantly restarting on a fault
- **`-no-shutdown`** — keeps QEMU open so you can read the debug output
- **`-serial file:qemu.log`** — serial output saved to file
- **`-D qemu_debug.log`** — all debug output saved to file
- **`-serial stdio`** — real-time serial debug output in your terminal

### Useful for diagnosing

- invalid far jumps
- incorrect segment selectors
- paging faults
- triple faults
- CR0/CR4/EFER misconfiguration
- long-mode entry failures

### Additional run modes

Run with serial output to terminal (default):

```
make runkernel64
```

With serial output saved to file:

```
make runkernel64-log
```

Run with GDB debug server:

```
make runkernel64-debug
```

Run with verbose debug logging:

```
make runkernel64-verbose
```

Run headless (no VGA window):

```
make runkernel64-headless
```

Run with KVM acceleration (faster):

```
make runkernel64-kvm
```

Run single-drive with KVM:

```
make runkernel64-kvm-single
```

Run single-drive under TCG (no KVM):

```
make runkernel64-single
```

Telnet serial console (connect with `telnet localhost 4444`):

```
make runkernel64-telnet
```

---

## Purpose

This project is designed to be:

- **Readable** — minimal, clean assembly and C
- **Incremental** — each stage builds on the last
- **Accurate** — follows x86_64 architectural rules
- **Practical** — boots in QEMU with simple commands
- **Educational** — a reference for anyone learning OS development

donix specifically is about **running real Unix userspace on a
from-scratch kernel**: not a POSIX subset, not a libc of our own design,
but static musl binaries talking to a kernel that speaks the Linux
x86_64 syscall ABI natively.

---

## Tags & milestones

The **dons-os** tags (v0.0.1 through v0.5.4) are inherited from the
parent project and point at the pre-migration history. They mark the
boot chain, the interrupt-driven kernel, PMM, VMM, heap, userland,
ELF loader, newlib, storage, and the REPL shell as they existed before
donix forked.

The **donix** completion tag is:

- `v0.5.5` — musl migration complete. The kernel now speaks Linux
  x86_64 syscalls, the shell is `musl_sh`, the userland C library is
  musl 1.2.5, and the newlib userland has been fully retired. See the
  tag message for the full changelog.

The 56 working tags created during the A1–A5 migration
(`20260922A`–`20260926Z` and `20260926-01`–`20260926-09`) are recorded
in [`migration-tags.txt`](migration-tags.txt) with their commit SHAs.
They may be deleted from the local repository before any public push;
the file preserves their names either way.

For the session-by-session story of the migration, see
[`handoff.md`](handoff.md).

---

## Project status

donix currently runs a musl-linked userland against a Linux-x86_64-ABI
kernel, with a working `fork`/`execve`/`wait4` shell, a FAT filesystem,
and a functioning storage layer.

A detailed capability checklist is maintained in
[`OSDev_Checklist.md`](OSDev_Checklist.md), and the plan for what comes
next is in [`ROADMAP.md`](ROADMAP.md) and [`handoff.md`](handoff.md).

The next milestone is **Phase B** — running a static busybox binary
against the musl userland, which will surface the next batch of
unimplemented syscalls.

---

## Known limitations

- **`sys_newfstatat` (262) is not implemented.** `sys_fstat` (5) and
  `sys_stat` (4) are. Programs that call `fstatat(fd, path, st, flags)`
  with a non-`AT_FDCWD` fd and a relative path will get `Unknown
  syscall: 262` on serial and an error return.
- **`fcntl` (72) is not implemented.** musl's `opendir` calls
  `fcntl(fd, F_SETFD, FD_CLOEXEC)` and ignores the failure, so it is
  visible as `Unknown syscall: 72` noise on serial, not a functional
  problem today.
- **`sys_open` accepts non-directories when called with
  `O_DIRECTORY`.** `ls_musl 0:/hello-world.txt` prints an empty
  listing and exits 0 instead of failing. Fix deferred; see
  `handoff.md`.
- **`isr14_handler` halts on user-mode faults.** The `#PF` handler
  does not distinguish a user-mode fault from a kernel-mode one. An
  unexpected user-mode fault kills the console instead of terminating
  the faulting process. Fix deferred.
- **Page tables are not freed on process exit.** `process_exit`
  reclaims the PCB slot and its data pages, but the process's page
  tables leak. Bounded by the 32-slot PCB pool. See
  [`MAINTENANCE.md`](MAINTENANCE.md) §3c.
- **`fatfs/ff.o` and `fatfs/ffunicode.o` are compiled with
  `/opt/cross/bin/x86_64-elf-gcc`, not Clang.** Clang 22.1.8
  miscompiles `ff.c` at every optimization level we tried. The cross-GCC
  produces correct code at `-O2` and links cleanly with the
  Clang-built kernel objects. See [`LLD_BUG_REPORT.md`](LLD_BUG_REPORT.md).
- **Single-drive vs dual-drive is a build-time choice.** One kernel
  binary cannot serve both layouts.
- **The shell does not implement quoting, globbing, pipes,
  redirection, `cd`, environment variables, command history, or
  signals.** See the "User shell" section.
- **`musl_sh` echoes garbage on lines containing backspaces.**
  Cosmetic. The stored line is correct.

---

## Next steps

The immediate next milestone is **Phase B — busybox / coreutils
against musl**. The plan and the ordering of work are in
[`handoff.md`](handoff.md) and [`ROADMAP.md`](ROADMAP.md).

---

## License

This project is licensed under the **MIT License**.
Use freely, modify freely, credit appreciated.
