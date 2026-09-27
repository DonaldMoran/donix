> **Referenced by the README's "What you need" section.** This report
> documents the Clang/LLD bugs that require the cross-GCC workaround
> for FatFs. The bugs are still present in Clang 22.1.8 / LLD 22.1.8;
> the report is live, not frozen.

# Toolchain bug reports — tracking note

Three separate compiler/linker bugs in Fedora's Clang/LLD 22.1.8
affected `fatfs/ff.c` at every optimization level. All three are
worked around by compiling that one file (and `ffunicode.c`) with
`/opt/cross/bin/x86_64-elf-gcc`. This document is the reference for
all three, and for the earlier LLD 22.1.8 issue that was filed
upstream.

The bugs affect `fatfs/ff.c` in dons-os and in donix alike — the
FatFs source and the cross-GCC workaround carried forward unchanged
through the fork. The only file-local change since the fork is the
syscall number for `unlink` (see Bug 2); the underlying issue is
identical.

---

## Bug 1 — LLD 22.1.8 truncates instructions in linked kernel binary

### Filed upstream

`llvm/llvm-project` issue:

    https://github.com/llvm/llvm-project/issues/224938

Title: "LLD 22.1.8 produces truncated instructions in linked kernel
binary while standalone object is correct"

Date filed: 2026-09-20

### Symptom

Building the kernel with `-O2` for `fatfs/ff.c` produced a
`kernel.elf` in which two functions contained **truncated
instructions**. The bytes present in the standalone `.o` file were
missing from the linked binary. The CPU raised `#UD` at runtime when
it reached one of them.

### Faulting address (from QEMU serial output)

    *** UNHANDLED INTERRUPT: vector 6 ***
      error_code : 0x0000000000000000
      rip        : 0xFFFFFFFF80110002
      cs         : 0x0000000000000018
      rflags     : 0x0000000000010213
      rsp        : 0xFFFFFFFF8008FF50
      ss         : 0x0000000000000020
      rax        : 0x0000000000000004
      rbx        : 0xFFFFFFFF801269C8
      rcx        : 0x0000000000000000
      rdx        : 0x0000000000000000
      rsi        : 0x0000000000000000
      rdi        : 0xFFFFFFFF801269C8

Vector 6 is `#UD` (invalid opcode). rbx and rdi both hold `&boot_fs`,
so the fault is in `check_fs`, just after `move_window` returns.

### Disassembly of the faulting kernel.elf

    ffffffff80110019:	0f b6 43 30          	movzbl 0x30(%rbx),%eax
    ffffffff8011001d:	05 18 ff ff ff       	add    $0xffffff18,%eax
    ffffffff80110022:	44 0f b7 b3 2e 02 00 	movzwl 0x22e(%rbx),%r14d
    ffffffff80110029:	00
    ffffffff8011002a:	83 f8 03             	cmp    $0x3,%eax
    ffffffff8011002d:	0f                   	.byte 0xf
    ffffffff8011002e:	87                   	.byte 0x87
    ffffffff8011002f:	80                   	.byte 0x80

The instruction at `0xFFFFFFFF8011002D` should be a 6-byte `ja rel32`.
Only the first 3 bytes are present.

A second truncated instruction appears in `move_window`:

    ffffffff8010e0f8:	89 c1                	mov    %eax,%ecx
    ffffffff8010e0fa:	b8 01 00 00 00       	mov    $0x1,%eax
    ffffffff8010e0ff:	85                   	.byte 0x85

`0x85` is the opcode for `test r/m32, r32`, truncated to a single byte.

### Disassembly of the standalone object (correct)

Compiled with the same flags into `/tmp/ff_from_source.o`:

    2d29:	0f b6 43 30          	movzbl 0x30(%rbx),%eax
    2d2d:	05 18 ff ff ff       	add    $0xffffff18,%eax
    2d32:	44 0f b7 b3 2e 02 00 	movzwl 0x22e(%rbx),%r14d
    2d39:	00
    2d3a:	83 f8 03             	cmp    $0x3,%eax
    2d3d:	0f 87 80 00 00 00    	ja     2dc3 <check_fs+0xc3>

All instructions complete and correctly encoded.

### Environment

- Clang:  22.1.8 (Fedora 22.1.8-4.fc44)
- LLD:    22.1.8
- Target: x86_64-unknown-elf
- Host:   Fedora 44, x86_64

### The exact commands

Compile (single object):

    clang -target x86_64-unknown-elf -ffreestanding -O2 -Wall -Wextra \
          -mcmodel=kernel -fno-pic -mno-red-zone -mno-sse -mno-sse2 \
          -mno-avx -mno-mmx -Iinclude -Ifatfs -DFAT_CONFIG_SINGLE_DRIVE=1 \
          -c fatfs/ff.c -o fatfs/ff.o

Link (full kernel):

    ld.lld -nostdlib -T linker.ld -o kernel.elf <all objects>

Preprocess (for the report's attachment):

    clang -target x86_64-unknown-elf -ffreestanding -O2 -mcmodel=kernel \
          -fno-pic -mno-red-zone -mno-sse -mno-sse2 -mno-avx -mno-mmx \
          -Iinclude -Ifatfs -DFAT_CONFIG_SINGLE_DRIVE=1 \
          -E fatfs/ff.c -o ~/ff.i

The preprocessed output (~56 KB) was attached to the bug report.

### If LLVM responds

They will probably ask for one or more of the following. The answers
are all recorded above or are easy to regenerate:

1. **A minimal reproducer.** We do not have one — the bug does not
   reproduce when `ff.c` is compiled standalone to an object file. It
   only appears in the linked binary. If asked, say so, and offer the
   full kernel source tree.

2. **The preprocessed source.** `~/ff.i` (or regenerate with the
   command above).

3. **The linked `kernel.elf`.** The one that reproduced the fault is
   the build with Clang at `-O2` and LLD as the linker, without the
   GCC override for `fatfs/ff.o`.

4. **The full build log.** Show them the `ld.lld` invocation and the
   flags for each object.

5. **The linker script.** `04_kernel_64bit/linker.ld`.

6. **A `--reproduce` tarball.** LLD supports
   `ld.lld --reproduce=repro.tar` which captures every input for a
   self-contained reproduction. If they ask, re-link the faulting
   `kernel.elf` with that flag and upload the tarball.

### The one outstanding question

We never ran the diagnostic that would have distinguished
"linker bug" from "compiler bug" with certainty: relinking the same
objects with `ld.bfd` instead of `ld.lld`. If LLVM asks us to narrow
the fault further, that test is the next thing to do:

    # in 04_kernel_64bit/Makefile
    LD := ld.bfd

then `make clean && make FAT_CONFIG=single` and check
`/opt/cross/bin/x86_64-elf-objdump -d kernel.elf` around
`0xFFFFFFFF8011002D`.

If `ld.bfd` produces correct output and `ld.lld` does not, the bug is
in LLD and the report is correct as filed. If both produce the same
broken output, the bug is elsewhere (compiler, assembler, or the way
objects are being produced).

---

## Bug 2 — Clang 22.1.8 hangs `f_unlink` at `-O1`

Discovered 2026-09-20 after filing the LLD bug.

### Symptom

With `fatfs/ff.c` compiled at `-O1`, the `unlink` code path hangs —
the CPU is stuck in `f_unlink` or `remove_chain` and never returns.
No exception is raised; the machine simply stops executing after
printing "deleting 3 file(s)...".

Adding diagnostic `serial_print` calls to `f_unlink` and
`remove_chain` at `-O1` also makes the hang go away, which is
characteristic of a compiler code-generation bug that depends on
register allocation and instruction scheduling.

### Syscall number

In dons-os the calling path was `SYS_UNLINK` = 7. In donix it was
renumbered to Linux `unlink` = 87 during the A1 renumbering (see
[`handoff.md`](handoff.md)), and the dons-os-private number was
retired. The FatFs code path itself is unchanged, so the workaround
below is identical in both trees. Only the number the userland calls
differs.

### Not filed

This is a separate bug from the LLD issue. It has not been filed
upstream. To file it we would need a reduced test case, which we do
not have — the bug does not reproduce from `ff.c` compiled standalone.

### When to revisit

Whenever the toolchain is updated. If Clang 22.1.x is bumped or
if the kernel moves to a newer Clang, retry `-O1` for `ff.c` and
see if the hang returns.

---

## Bug 3 — Clang 22.1.8 breaks the FILINFO read path at `-O0`

Discovered 2026-09-20, immediately after Bug 2.

### Symptom

With `fatfs/ff.c` compiled at `-O0`, the FILINFO read path is broken.
In the kernel shell's `fatls` command:

- `f_readdir` returns `FR_OK`, but the filename field contains
  garbage bytes — reported as `@80(` in one capture.
- `f_opendir` on the root directory returns `FR_INT_ERR` (2) instead
  of `FR_OK`, which means FatFs's own internal sanity check fired
  during directory traversal.

The `sizeof(FILINFO)` reported by the compiler is correct (280 bytes
for `FF_USE_LFN = 2`, `TCHAR = char`, `FF_LFN_BUF = 255`). So the bug
is not a struct-size computation; it is either a field-offset
computation or a stack-frame layout issue.

Adding `serial_print` diagnostics around the FatFs calls did not hide
the bug in this case — the `[DBG]` output showed FatFs filling
`FILINFO` correctly at the raw byte level, but the surrounding kernel
code then misread the same bytes.

### Distinguishing from the other bugs

At `-O0`, no `#UD` and no hang — the kernel runs to the point of the
bad read and then returns an error code. This is different from the
`-O2` LLD truncation (which produces `#UD`) and the `-O1` hang (which
produces no output at all).

### Not filed

This has not been filed upstream. We do not have a reduced test case.
The bug does not reproduce when `ff.c` is compiled standalone with
Clang at `-O0` — it only appears when the object is linked with the
rest of the kernel. That is the same diagnostic problem as the LLD
issue.

### When to revisit

Same as the other two bugs. If Clang and LLD are both updated,
retry compiling `ff.c` with Clang at each optimization level. If all
three issues are gone, remove the GCC override.

---

## Workaround currently in place

`fatfs/ff.c` and `fatfs/ffunicode.c` are compiled with the cross-GCC
(`/opt/cross/bin/x86_64-elf-gcc`), not Clang. In `04_kernel_64bit/Makefile`:

    FATFS_CC := /opt/cross/bin/x86_64-elf-gcc

    FATFS_CFLAGS := -ffreestanding -O2 -Wall -Wextra \
        -mcmodel=kernel -fno-pic -mno-red-zone -mno-sse -mno-sse2 \
        -mno-avx -mno-mmx -Iinclude -Ifatfs $(FAT_CFLAGS) \
        -MMD -MP

    fatfs/ff.o: fatfs/ff.c
        $(FATFS_CC) $(FATFS_CFLAGS) -c fatfs/ff.c -o fatfs/ff.o

    fatfs/ffunicode.o: fatfs/ffunicode.c
        $(FATFS_CC) $(FATFS_CFLAGS) -c fatfs/ffunicode.c -o fatfs/ffunicode.o

`diskio.c` stays with Clang; it is small, hand-written, and does not
exhibit any of the bugs.

### Why GCC and Clang objects link together

Both compilers emit ELF64 relocatable objects using the x86-64 System
V ABI for the `x86_64-unknown-elf` target. The calling convention,
struct layout, object file format, and relocation types are
identical. There is no linker-level distinction between a GCC `.o`
and a Clang `.o`.

The kernel builds, boots, mounts the FAT volume, shows long filenames
correctly, and runs `unlink` without hanging.

### Sanity check

Confirm `ff.o` only has the expected undefined symbols:

    /opt/cross/bin/x86_64-elf-nm 04_kernel_64bit/fatfs/ff.o | grep " U " | sort -u

Expected: `memcpy`, `memset`, `strchr`, `strlen`, `memcmp`, `strcpy`,
and the `ff_*` functions from `ffunicode.o`. All of these are provided
by the kernel. If anything like `__stack_chk_fail`, `__memcpy_chk`,
or `printf` shows up, a flag is missing.

### When to revisit

If Clang and LLD are both updated and all three bugs are fixed,
remove the GCC override and move `ff.c` / `ffunicode.c` back to the
Clang default rule. Until then, `FATFS_CC` stays.

---

## Notes for future me

- The three bugs are not in our source. The same source compiles to
  correct object code under GCC at `-O2`.
- The `FATFS_CC` override is a workaround, not a fix. The bugs are
  still there in Clang and LLD; they just don't trigger when the file
  is compiled by GCC.
- If the workaround ever stops working (e.g., after a toolchain
  update or a source change to `ff.c`), the disassembly check is:
  `/opt/cross/bin/x86_64-elf-objdump -d kernel.elf | grep -B2 -A2 "\.byte"`
  If any `.byte` shows up in `.text`, the LLD truncation is back.
- The Makefile tracks header dependencies (`-MMD -MP`).  It did
  not when the LLD bug was first filed.  Header changes now trigger
  the correct rebuilds automatically.
- The bugs are toolchain-specific, not project-specific. They will
  affect any project that (a) targets `x86_64-unknown-elf` with
  Clang/LLD 22.1.8, and (b) compiles FatFs R0.16 or a similar
  source file with comparable register pressure.
