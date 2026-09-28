### donix is not Linux.

It runs Linux binaries. It speaks the Linux syscall ABI. It runs
**busybox** — the shell, the line editor, the applets, the whole
thing — on top of a from-scratch x86_64 kernel. No kernel source from
Linus, no glibc, no distro. It is a small operating system that talks
to real, statically linked musl binaries as if they were natively
compiled for it.

It boots on bare metal through 16-bit real mode, 32-bit protected mode,
and into 64-bit long mode; starts a higher-half C kernel; brings up
paging, the heap, a preemptive scheduler, an ELF loader, an ATA driver,
and a FAT16 filesystem; and drops you at a prompt where you can run
user programs loaded off disk. Nothing is emulated, nothing is stubbed
out at the syscall layer that matters, and nothing is a subset of a
larger system.

It is a kernel. It is not Linux.

---

## Why this exists

Most hobby kernels make you choose. Either they're small and honest but
never run real software, or they run real software but hide the
interesting parts behind a mountain of scaffolding. donix tries to be
the third thing: **small enough to read end to end, complete enough to
run real Unix binaries, and honest enough that nothing important is
faked.**

It is for people who want to understand how an operating system
actually works. Not how one is configured, not how one is packaged, not
how one is deployed — how one is *built*. Every stage from the first
instruction off the boot sector to the moment a musl program prints
`hello from donix` is in this repository, in C and assembly, under a
few thousand lines. There is no hidden kernel, no borrowed scheduler,
no "and then a miracle happens." You can read the whole thing in a
weekend and understand every line.

It is also a working system, not a toy. It runs a shell. It forks,
execs, and waits. It reads directories and stats files. It allocates
memory with `malloc` and frees it. It does all of that through the same
syscall interface Linux does, using the same ABI, so that real,
unmodified musl binaries can run on a kernel that shares no code with
the one they were built for.

That's the point. That's the whole point.

---

## What it does

- **Boots from BIOS to a 64-bit shell** with no dependency on a
  bootloader like GRUB or Limine. Every stage — the boot sector, the
  long-mode entry, the kernel loading, the transition to ring 3 — is
  hand-written.
- **Runs static musl binaries** compiled against musl 1.2.5, built from
  source into the project. Programs are ordinary C, linked the way any
  Unix program is linked; they just happen to run on a kernel that
  isn't Unix.
- **Speaks Linux x86_64 syscalls.** `read`, `write`, `open`, `close`,
  `fork`, `execve`, `wait4`, `brk`, `mmap`, `getdents64`, `stat`,
  `fstat` — the numbers and semantics match Linux x86_64. musl's
  `printf`, `malloc`, and `opendir` work unmodified.
- **Has a working shell.** `musl_sh` forks, execs, waits, and drops you
  at a `donix> ` prompt. `cat hello-world.txt` prints the file.
  `echo hi` prints `hi`. `ls` lists the FAT volume. Real programs, real
  syscalls, real output.
- **Runs busybox.** A static musl-linked busybox 1.36.1 executes on
  donix: its `ash` shell is interactive (prompt, echo, backspace,
  line editing), it forks and execs external binaries via `PATH`,
  and its own applets (`busybox ls`, `busybox echo`) run in-process.
  This is the strongest evidence that the syscall ABI is right —
  busybox is a real, widely-deployed program that expects a real
  Unix kernel underneath it.
- **Is small enough to read.** The whole kernel is a few thousand lines
  of C and assembly. The boot chain is under 400 lines. The userland
  tree is 20 short C files. There is no build system you can't read in
  ten minutes. You can read it end to end in an evening.

donix is a fork of [dons-os](https://github.com/DonaldMoran/dons-os-x86_64).
The kernel infrastructure — boot chain, PMM, VMM, heap, scheduler, ATA
driver, FatFs — carried forward intact. What changed is the syscall ABI
(Linux numbers, not dons-os-private ones) and the userland C library
(musl 1.2.5, not newlib).

---

## What you need

Tested on **Fedora 44**. Other distributions should work but are
untested.

**Host tools:**

- `nasm` — assembler for the boot chain and kernel stubs
- `clang` and `ld.lld` — kernel C compiler and linker
- `qemu-system-x86_64` — the emulator donix runs in
- `mtools` — `mcopy`, `mdir`, `mkfs.vfat`, for building the FAT image
- `xxd` — for embedding the kernel-side test program
- `gcc` — the *host* compiler, used to build musl from source
- `git` and `make` — obvious

**`/opt/cross/bin/x86_64-elf-gcc`** — a cross-compiler used only for
FatFs (`fatfs/ff.c` and `fatfs/ffunicode.c`). Clang 22.1.8 miscompiles
`ff.c` at every optimization level; the cross-GCC produces correct code.
If it isn't installed, the kernel build will fail on those two files.

The toolchain is not packaged; the copy in use is GCC 15.2.0, built for
`x86_64-elf` and installed under `/opt/cross/`. See
[`docs/LLD_BUG_REPORT.md`](docs/LLD_BUG_REPORT.md) for why it is needed
and how it was built.

**musl 1.2.5** — do **not** install your distro's musl package. donix
builds musl 1.2.5 from source into a project-local tree, because the
project needs the exact version and install layout. Step 2 below does
this for you.

**busybox 1.36.1** — do **not** install your distro's busybox. The
project builds busybox from source the first time you build a disk
image, driven by `configs/busybox.config`. The source is cloned into
`third_party/busybox/` (gitignored). **Network access is required on
first run** — it clones from `https://git.busybox.net/busybox`. The
tracked config sets `CONFIG_STATIC=y` and `CONFIG_FEATURE_EDITING=y`.

---

## Getting started

### 1. Install the host tools

On Fedora 44:

```sh
sudo dnf install nasm clang lld qemu-system-x86 mtools xxd gcc git make
```

(`xxd` became its own package in Fedora 44; on older Fedora it was part
of `vim-common`.)

You also need `/opt/cross/bin/x86_64-elf-gcc` (see above). If it is not
already on your system, see
[`docs/LLD_BUG_REPORT.md`](docs/LLD_BUG_REPORT.md).

### 2. Build musl 1.2.5 from source

```sh
./toolchain/install_musl.sh
```

This clones upstream musl at `v1.2.5`, configures it for
`x86_64-linux-musl`, builds it, and installs it into
`third_party/musl-install/` (gitignored). It also writes a
`musl-gcc.specs` file that `toolchain/musl-gcc.sh` uses as the compiler
wrapper.

**Network access is required on first run** — it clones from
`https://git.musl-libc.org/git/musl`.

Idempotent: safe to re-run. It bails if `third_party/musl-src/` is
already at a different version rather than overwriting.

If you ever need to force a rebuild, delete `third_party/musl-src/` and
`third_party/musl-install/` and re-run.

### 3. Build and run

```sh
./run
```

The `run` script is a menu. Open it in an editor, uncomment exactly one
line inside the `menu()` function, and execute the script. It will:

1. `make clean`
2. `make FAT_CONFIG=single` — build the kernel, single-drive layout
3. `make -C 05_boot_kernel64 hdd-single.img` — build the FAT image,
   which includes compiling every musl userland binary from source
   and building busybox (first time only; the source is cached in
   `third_party/busybox/` thereafter)
4. Boot QEMU and capture the serial output to `capture.txt`

The default uncommented line is the single-drive TCG configuration.

**Interact with the OS in the QEMU window**, not the terminal. The
terminal shows the kernel's serial log; the shell prompt (`donix> `) is
on the emulated VGA console.

To stop QEMU, close the window or press `Ctrl-C` in the terminal.

### Building the kernel alone

If you just want the kernel binary, without the FAT image or a QEMU run:

```sh
make
```

This produces `04_kernel_64bit/kernel.bin`. The userland is not built
by this step; it is built by the image Makefile when you make a disk
image.

### Debugging

```sh
make logkernel64
```

Boots QEMU with `-d int,cpu_reset,guest_errors` and writes the log to
`qemu_debug.log`. See the `run-*` and `logkernel64` targets in the
top-level `Makefile` for other modes.

---

## What's in the repo

```
01_boot_16bit/          16-bit BIOS boot sector
02_boot_32bit/          32-bit protected mode demo
03_boot_64bit/          long-mode entry, PAE paging
04_kernel_64bit/        64-bit kernel source
  fatfs/                vendored FatFs R0.16 + ATA shim
  include/              kernel headers
05_boot_kernel64/       boot chain assembly, image builder
userland/musl/          musl userland source tree
  apps/                 real userland programs (hello, echo, cat, ls,
                        memtest, musl_sh)
  tests/                diagnostic binaries (musl_min, musl_fork, etc.)
  Makefile              builds every .c into build/*.elf; also builds
                        busybox from source
configs/                tracked build configs (busybox.config)
third_party/            source trees and build prefixes (gitignored):
                          musl-src/, musl-install/, busybox/, busybox-install/
toolchain/              musl build and wrapper scripts
test-files/             files copied into the FAT image
run                     QEMU launch menu
docs/                   historical record (see below)
```

The kernel and the userland are separate layers. The kernel build does
not reach into `userland/musl/`; the image Makefile invokes
`make -C ../userland/musl` as a prerequisite.

---

## Where to look for more

- [`handoff.md`](handoff.md) — **the current state of the project.**
  Session log, canary state, open issues, current gotchas. Updated
  every session; this is the file to read if you want to know what
  is true right now.
- [`ROADMAP.md`](ROADMAP.md) — what's next. Future work only;
  completed milestones are in the handoff's session history.
- [`docs/`](docs/) — historical record:
  - `docs/migration-history.md` — the A1–A6 migration from dons-os
    (newlib) to musl, step by step.
  - `docs/dons-os-history.md` — the pre-fork dons-os version-by-version
    story.
  - `docs/CHECKLIST.md` — capability checklist, frozen at v0.6.0.
  - `docs/MAINTENANCE.md` — known debt and latent bugs, frozen at
    v0.6.0. Open items are lifted into `handoff.md`.
  - `docs/LLD_BUG_REPORT.md` — Clang/LLD toolchain bugs and the
    FatFs workaround.

---

## License

MIT License. Use freely, modify freely, credit appreciated.

Note on third-party components: donix builds against musl (MIT-style)
and busybox (GPLv2). Neither is vendored into the donix source tree —
both are fetched from upstream by the build system and live under
`third_party/` (gitignored). The donix source itself remains MIT.
