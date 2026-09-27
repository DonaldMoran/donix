# MAINTENANCE
### donix (x86_64) — Known Debt, Latent Bugs, and Maintenance Work

This document lists everything that is *known to be unfinished, fragile,
or debt-laden* in the current codebase, but which is not a feature. It is
the counterpart to `ROADMAP.md` (which lists features) and
`OSDev_Checklist.md` (which tracks capability). If a thing is wrong, or
fragile, or will bite later, it belongs here.

Items are ordered by priority: do the ones at the top first. Effort is a
rough estimate, not a commitment.

Completed items are struck through (~~like this~~) and marked with ✅.
They are kept in place for history; do not delete them.

Items marked **🔵 dons-os era** were filed before the fork. They are
kept for history; the referenced code may have been renamed, renumbered,
or removed during the musl migration (A1–A5). See
[`handoff.md`](handoff.md) and [`ROADMAP.md`](ROADMAP.md) §0.

---

## 0. donix — musl migration open items

The A1–A6 migration (see [`ROADMAP.md`](ROADMAP.md) §0 and
[`OSDev_Checklist.md`](OSDev_Checklist.md) §0) fixed the six bugs it
surfaced (see §7 below for the record). The items in this section are
the loose ends the migration left behind, or the small syscall gaps
that the next milestone (busybox, Phase B) will hit.

Ordered by priority — do the ones at the top first.

### 0.1. `fcntl` (Linux 72) is unimplemented

**Status:** noisy; unblocks a common path when stubbed
**Effort:** 30 minutes

musl's `opendir()` calls `fcntl(fd, F_SETFD, FD_CLOEXEC)` after the
`open(O_DIRECTORY)`. `sys_fcntl` is not implemented, so the kernel
prints `Unknown syscall: 72` on serial and returns `-1`. musl ignores
the failure, so it does not break `opendir` today.

Busybox and most real Unix programs call `fcntl` for
`F_SETFD`/`F_GETFD`/`F_DUPFD`/`F_GETFL`/`F_SETFL`. The last two are
the ones that matter for stdio buffering and non-blocking I/O.

**Fix:** add a minimal `sys_fcntl(fd, cmd, arg)` that:
- returns 0 for `F_SETFD` and `F_GETFD` (with `F_GETFD` returning 0,
  i.e. no close-on-exec flag is tracked),
- returns `fd` for `F_DUPFD` (dup not implemented; the fd is returned
  unchanged, which is correct for programs that only use `fcntl` to
  set flags),
- returns 0 for `F_SETFL`, `F_GETFL` (with `F_GETFL` returning
  `O_RDWR`),
- returns `-EINVAL` for anything else.

Deferred until busybox needs it, but it should be a small, low-risk
commit.

### 0.2. `sys_newfstatat` (Linux 262) is unimplemented

**Status:** unreachable from the current canary; busybox will hit it
**Effort:** 45 minutes (mostly mirroring the existing `sys_stat`/`sys_fstat`)

`sys_stat` (4) and `sys_fstat` (5) are done. `fstatat(fd, path, st,
flags)` with a non-`AT_FDCWD` fd and a relative path routes to
`__syscall(SYS_newfstatat, ...)` = 262. Nothing in the current musl
canary reaches it (`ls` uses absolute `0:/` paths).

**Fix:** implement `sys_newfstatat(int dirfd, const char *path, void
*statbuf, int flags)`:
- if `dirfd == AT_FDCWD` or the path is absolute, delegate to
  `sys_stat`.
- otherwise, return `-ENOSYS` for now (there is no per-process cwd
  yet, and no directory fd to resolve relative paths against). This
  is enough to make busybox's `stat` calls succeed when they use
  absolute paths.
- the `AT_EMPTY_PATH` case (an empty path with `fstat`-like
  semantics) should delegate to `sys_fstat`.

The three-way delegation matches the existing `fstatat_kstat` routing
described in [`handoff.md`](handoff.md) under "musl `fstatat`
routing."

### 0.3. `sys_open` accepts non-directories when called with `O_DIRECTORY`

**Status:** cosmetic; only triggers on user error
**Effort:** 15 minutes

`ls 0:/hello-world.txt` prints `0 file(s), 0 directory(ies)` and
exits 0, instead of failing with "not a directory." FatFs's
`f_opendir` accepts a file path and yields a `DIR` whose `f_readdir`
immediately returns "no entries"; `sys_open`'s `f_opendir` fallback
path does not verify that the target is actually a directory.

**Fix:** after `f_opendir` succeeds, check the entry's
`fattrib & AM_DIR`; if not set, close and return `-ENOTDIR` (20).
One call to `f_stat` in the fallback path.

### 0.4. `isr14_handler` halts on user-mode faults

**Status:** latent; will bite the first time busybox segfaults
**Effort:** 30 minutes

The `#PF` handler checks only `g_expect_fault`; it does not look at
`error_code & 4` to distinguish a user-mode fault from a kernel-mode
one. Any unexpected user-mode fault kills the console instead of
terminating the faulting process.

**Fix:** in `isr14_handler`, if `(error_code & 4)` (user-mode) and
`g_expect_fault != 0x0E`, call `sys_exit(-1)` for the faulting
process instead of halting. `sys_exit` on a user-mode process halts
the CPU (there is no user shell to return to), so the practical
difference is: the offending user process dies *cleanly* and prints a
diagnostic, rather than taking the kernel down with a low-level
`#PF` dump. Whether the console recovers depends on whether the
faulting process was the shell — a future commit could restart the
shell or drop back to the kernel shell.

**Related:** item 3f (self-test fault triggers must be kernel-mode)
is the same coupling in a different guise. Fixing 0.4 does not fix
3f; both remain open.

### 0.5. `musl_sh` echoes garbage on lines containing backspaces

**Status:** cosmetic
**Effort:** 15 minutes

The kernel trace shows the argv that actually reaches `sys_execve` is
correct — `argc` matches the number of tokens typed, and the program
output is right — but the echoed input line is scrambled when the
typed line contains backspaces. `musl_sh`'s read loop handles `\b` /
`0x7f` by emitting `"\b \b"` (three bytes) and decrementing the
buffer index, so the *stored* line is correct. The garble is in the
display.

**Fix, if wanted:** emit `"\b \b"` only when stdout is a real tty,
or drop the erase-on-backspace entirely and just decrement `n`. The
second is simpler and matches the current architecture (no tty
concept); the echo simply stops pretending to erase.

### 0.6. `sys_execve` writes `%rdi`/`%rsi` unnecessarily

**Status:** code cleanliness; no behavioral effect
**Effort:** 15 minutes

`sys_execve` writes `argc` to the resume-frame slot that ends up in
`%rdi` (`ktop[-12]`) and the argv array base to the slot for `%rsi`
(`ktop[-13]`). This was done because donix's newlib-era
`arc2/crt0.S` read `argc`/`argv` from registers instead of the SysV
stack layout. musl's `_start` reads the SysV stack layout and ignores
`%rdi`/`%rsi`, so these two writes are now no-ops.

**Fix:** delete the two lines. Harmless to leave in place, but the
comment block above them is a long explanation of a bygone
compatibility requirement. Removing both keeps the reader from
wondering whether they are load-bearing.

**Related:** this is what §3m used to describe (as a *correctness*
problem under newlib). With musl, the SysV layout is what matters and
it is already written. See §3m for the historical entry.

### 0.7. `PMM_ALLOC_DIAG` is inert diagnostic code

**Status:** cosmetic
**Effort:** 5 minutes

`pmm.c` still has the `PMM_ALLOC_DIAG`-gated per-call allocation
counter that was added during the `20260924B` `brk` investigation
(see §3d and the handoff's "Resolved bugs"). It has been set to 0
ever since and never fires.

**Fix:** delete the gated block. If a future `brk` or allocation
investigation needs it, it is one `git show` away.

### 0.8. `musl_min` was built without `-no-pie` (resolved at A6) ✅

**Status:** ✅ **RESOLVED at A6** (tag `v0.6.0`, session 11).

The old `build_musl_tests.sh` built `musl_min` without the explicit
`-no-pie` flag while every other binary had it.  `musl_min` worked
(probably because the musl specs file was adding `-no-pie` implicitly,
or because the tiny program has no relocations that matter), but the
inconsistency was real.

The new `userland/musl/Makefile` builds every binary uniformly with
`-static -no-pie -O2 -mcmodel=large`.  `musl_min` is no longer a
special case.  Verified by:
- `readelf -h userland/musl/build/musl_min.elf` → `Type: EXEC`,
  `Entry point 0x40022b`
- booting the new `MUSL_MIN.ELF` from the FAT → `MUSL-START`

Kept here for the record.  The "preserve the special case" note in
the A6 plan is obsolete; the special case is gone.

### 0.9. `build_musl_tests.sh` duplicate comments and `/tmp` staging (resolved at A6) ✅

**Status:** ✅ **RESOLVED at A6** (tag `v0.6.0`, session 11).

`build_musl_tests.sh` had duplicate `# Test 4:` and `# Test 7:`
comments, and the whole `/tmp` staging model meant that a reboot
silently lost the musl binaries.  The script is deleted; the
duplicate comments are gone with it; the `/tmp` staging model is
gone with it.  `userland/musl/Makefile` is the build recipe and
`userland/musl/build/` is the output directory.

Kept here for the record.  The "preserve both during A6 unless you
want to fix them" note in the old cosmetic section is obsolete.

---

## 1. Kernel size ceiling ~~(176 KB staging limit)~~ ✅

**Status:** ✅ **DONE (v0.5.0 + commit 09a6f79).** The staging ceiling was
raised from 176 KB to 448 KB by adding PASS 4–7 and extending the
long-mode copy. The text below is kept for history: it describes the
176 KB state and the reasoning for raising it.

~~**Effort:** 30 minutes (Option 3), 2–3 hours (Option 2), a few days (Option 6)~~
~~**Risk if ignored:** the kernel grows past the ceiling and the Makefile's
tripwire fails the build. That is the *good* outcome. The bad outcome is
that someone removes the tripwire and the kernel silently truncates at
boot, producing a machine that boots to garbage.~~

### What the limits were

~~Two ceilings apply to `kernel.bin`:~~

~~1. **Staging ceiling: 176 KB.** `stage2.asm` reads the kernel from disk in
   three passes of 128 sectors each, staging it into low memory at
   `0x80000`, `0x90000`, and `0x20000`, then copies it to `0x100000` in
   long mode. That staging layout cannot exceed 176 KB (128 KB from the
   first two passes, plus 48 KB from the third). The tripwire in
   `04_kernel_64bit/Makefile` (`KERNEL_STAGING_LIMIT := 180224`) fails
   the build loudly if `kernel.bin` exceeds it.~~

~~2. **Disk-layout ceiling: 983 KB.** The kernel starts at LBA 128. The
   FAT partition starts at LBA 2048. So the kernel region is
   `(2048 - 128) × 512 = 983040` bytes. This ceiling is enforced by
   `KERNEL_DISK_LIMIT := 983040` in the same Makefile. It will not bind
   until the staging ceiling has been raised well past 176 KB.~~

~~The staging ceiling is the binding one today.~~

### How it was raised

~~**Option 3 — use the unused low memory (30 min, ceiling → 448 KB).**
`0x30000–0x7FFFF` (320 KB) is plain RAM, unused by the boot chain. Add
PASS 4–7 to `stage2.asm` reading into segments `0x3000` through `0x7000`,
and add a `rep movsq` in long mode to copy the new bytes to `0x12C000`
onward. Bump `KERNEL_TOTAL_BYTES` in `stage2.asm` to `448 * 1024` and
`KERNEL_STAGING_LIMIT` in the Makefile to `458752`.~~

~~Bonus: this also moves PASS 3 off `0x20000`, which is a robustness win
on real hardware. `0x20000–0x2FFFF` is conventional-free on QEMU/SeaBIOS
but not guaranteed by spec on bare metal. `0x30000–0x7FFFF` is safer.~~

### Remaining options if 448 KB is not enough

**Option 2 — stream reads (2–3 hours, ceiling → RAM limit).**
Read 128 KB into a single staging window, `rep movsq` it to its
destination, advance the destination pointer, read the next 128 KB into
the same window, repeat. Reuses one staging window indefinitely. Requires
the Makefile to write `kernel.bin`'s size somewhere stage2 can read (a
field in the boot sector, or a small header the Makefile emits). Stage2
then loops until that many bytes have been copied.

**Option 6 — replace stage2 with Limine (a few days, ceiling → none).**
Limine handles long-mode entry, the memory map, and page tables, and
hands the kernel a standard structure. Removes the staging architecture
entirely, turns the kernel into an ELF file loaded by the bootloader,
and gets you boot modules (the path to "user programs from disk") and a
framebuffer for free. Throws away `boot.asm`, `stage2.asm`, and the
BootInfo parsing in `entry.asm`. See ROADMAP §Long-term.

---

## 2. Documentation gaps ✅

**Status:** ✅ **DONE.** Item 2a is now covered by this file and by the
README's pointer to it. Items 2b and 2c were applied in commit `9b62d46`.
Text below kept for history.

~~**Effort:** ~2 hours total~~

~~### 2a. Kernel size limits are not documented in the README
See item 1 above. A reader has no idea the tripwire exists until it
fires. Add a short "Kernel size limits" section to the README under
Known Limitations, or fold it into the Build System notes.~~

~~### 2b. The `run` script is documented in one line
The README mentions it in the repository structure and one sentence in
Building & Running. The menu design ("uncomment one line in `menu()`,
then run `./run`") deserves a short paragraph. Otherwise a reader sees a
shell script with a function that's never called and has no idea how
it's supposed to be used.~~

~~### 2c. The user shell table is out of date
Options 1–7 are listed. Option 8 (list known files) and option 9
(reboot) are not. If the reboot change is kept, update the table.~~

---

## 3. Latent bugs (things that will bite eventually)

### 3a. ~~Hardcoded boot stack in `process_exit`~~ ✅

**Status:** ✅ **DONE (commit e246db6).** The kernel shell is now a real
process with its own PCB and kernel stack from the pool. It is created
by `kmain` on the `k` branch, resumed by `process_exit`'s fallback via
`scheduler_switch_to`, and suspended by every command handler that
yields to a diagnostic. The hardcoded `0xFFFFFFFF8008FF00` is gone, as
is the `kernel_shell_stack_top()` helper. Text below kept for history.

~~**Effort:** 1–2 hours~~

~~`process_exit`'s fallback to `kmain_shell_loop` sets
`rsp = 0xFFFFFFFF8008FF00`. That address is in the low 1 MB region,
currently mapped because the bootloader identity-maps it. Nothing in the
kernel guarantees it stays mapped. If you ever change the identity map,
this becomes a crash.~~

~~**Fix:** allocate the kernel shell's stack from the kernel stack pool
(`kernel_stack_alloc()`), set the shell up on that stack, free it on
exit. The fallback path in `process_exit` then sets `rsp` to the pool
stack instead of a hardcoded address.~~

### 3b. ~~No IST for `#DF`~~ ✅

**Status:** ✅ **DONE (commit f4d7df3).** A dedicated 4 KB IST1 stack is
allocated in `.bss`, `TSS.IST1` is populated with its top, and the `#DF`
gate's IST field is set to 1 in `idt.c`. The CPU now switches to the IST
stack before pushing the `#DF` frame, so `isr8_handler` prints RIP, CS,
RSP, SS, and the error code even if the original kernel stack was the
thing that faulted. Text below kept for history.

~~**Effort:** ~1 hour~~

~~Double faults currently produce a triple fault, which resets the CPU
without printing anything. QEMU shows the reset; on real hardware you'd
get a silent reboot. A small IST stack and an IST entry on the `#DF` IDT
gate turns this into a printed diagnostic.~~

~~**Fix:** add an IST stack (4 KB is plenty) to the TSS, set the `#DF`
gate's IST field to the new slot in `idt.c`, and have `isr8_handler`
print RIP, CS, and the error code before halting. The setup is very
similar to the current TSS `rsp0` handling.~~

### 3c. Page tables leak on process exit

**Status:** latent; a few pages per process, accumulates over many
`runproc` / `testyield` / `selftest` cycles in one boot
**Effort:** 2–3 hours

`process_exit` reclaims the PCB slot, the ELF segment pages, and the
user stack pages. It does not free the process's page tables (`cr3`).

**Fix:** walk the PML4 → PDPT → PD → PT chain in `process_reclaim` and
free every page table that is not shared with the kernel or another
process. Careful: `stage2.asm` shares `pt_low` between `pd[0]` and
`pd_hhdm[0]`, and the kernel's own mappings must not be touched. The
safe approach is to walk the user-space portion of the tree (the PML4
entries that correspond to the user address space, roughly PML4[0])
and free only those tables.

**Interaction with selftest (5a-ii).** Each `selftest` run spawns three
faulttest children, each of which calls `process_create` and therefore
`vmm_clone_page_table`. `process_reclaim` does not free the cloned
tables. So each `selftest` run leaks three page-table sets (a PML4,
a PDPT, a PD, and a PT per clone, since `vmm_clone_page_table` appears
to allocate a fresh tree). Repeated `selftest` runs in one boot will
show the PMM's free count dropping by a few pages per run.

**Interaction with v0.5.3 (the deep clone).** Since v0.5.3,
`vmm_clone_page_table` performs a *deep* copy of the low-half page-table
hierarchy: it allocates fresh PDPT/PD/PT pages at every level below the
PML4 for PML4 entries 0..255. Each clone therefore owns more page-table
pages than it did before v0.5.3 — a PML4 plus a handful of PDPT/PD/PTs
per clone, rather than just a PML4. The leak per process is still small
(a few pages) and is bounded by the 32-slot PCB pool, but the total
leak across a full pool of dead-but-unreclaimed processes is larger than
it was before the fix. The fix for this is the same as the fix for the
underlying issue: walk the tree in `process_reclaim` and free the
user-space portion.

This is acceptable for now — the leak is small and bounded by the 32-slot
PCB pool — but the "run selftest twice and compare" idempotency check
that a reader might reach for will *fail* on the free-page count until
this item is closed. The check that works today is: the `pmm` test's own
`Free Pages` line is stable across runs, because `pmm` runs before any
of the exception tests.

Defer this until you have multiple user processes. It's not a problem
with one.

### 3d. `brk`'s `heap_base` is a single constant

**Status:** code cleanliness, not functional
**Effort:** 15 minutes

Every process's heap starts at the same virtual address
(`0x8000200000`) and grows in its own address space (different `cr3`),
so there's no address conflict. But the shared constant is ugly. A
future refactor would move it into the PCB. **dons-os-era:** the
syscall is now Linux `brk` = 12 with the absolute-address ABI.

### 3e. `vmm_map_page_in_cr3` does not flush the TLB

**Status:** documented, callers handle it
**Effort:** none unless a new caller forgets

Callers must `invlpg` after mapping if the address may have a stale
translation. `sys_brk` does this. New callers should too. The fix would
be to make `vmm_map_page_in_cr3` optionally flush, but that has its own
tradeoffs. Leave as-is; it's documented.

### 3f. Self-test fault triggers must be kernel-mode functions

**Status:** documented constraint; holds today
**Effort:** n/a now; ~1 hour if a user-mode fault test is ever wanted
**Introduced by:** 5a-i (commit `f132903`)

`fault_kill_current` (in `interrupts.c`) terminates the current process
by calling `process_exit`. `process_exit` uses `exiting->entry_point >=
KERNEL_BASE` to decide between "resume the kernel shell" (kernel-mode
diagnostic) and "halt" (user-mode process). The self-test's three fault
triggers (`fault_de_trigger`, `fault_pf_trigger`, `fault_gp_trigger` in
`kmain.c`) are kernel text, so they take the resume-the-shell path, and
`selftest` gets control back after each fault.

**The constraint:** if a fault trigger is ever made a user-mode
function (e.g. to test a user-mode `#PF` from the self-test), the child
will *halt* on exit instead of resuming the shell, and the self-test
will never get control back. The result is a hang, not a FAIL, because
`g_fault_observed` is set but never read.

**Why this is fine today:** `selftest` is a kernel-shell command, and
all three triggers are kernel-mode by construction. The comment block
above the triggers in `kmain.c` says so.

**What a future refactor would need:** a per-PCB `expected_fault` field
that `process_exit` checks *before* the `entry_point >= KERNEL_BASE`
policy. That decouples "the process terminated by design" from "the
process terminated because it was a kernel diagnostic," which are
different concepts that the current code conflates. Not urgent — no
user-mode fault test exists or is planned.

**Related:** item 0.4 (isr14_handler halts on user-mode faults) is the
same coupling from a different direction.

### 3g. ~~`EFER.NXE` is not enabled~~ ✅

**Status:** ✅ **DONE (commit `028da72`).** `enable_nx()` in `kmain.c`
now sets EFER.NXE (bit 11 of MSR `0xC0000080`) before `sti`, guarded by
a CPUID.80000001H:EDX.NX check.  `test_vmm` now asserts on NXE rather
than printing it as an observation.  Verified end-to-end on single-drive
under KVM: `NX Active: Yes`, `15 passed, 0 failed`, and the user shell's
malloc test (option 3) still works with NXE on.

Text below kept for history: it describes the state before the fix.

~~**Effort:** ~15 minutes to set the bit, ~1 hour to verify safely~~
~~**Found by:** 5a-ii (`test_vmm`'s NXE assertion, commit `182c1ef`)~~

~~`vmm_map_page` and `vmm_map_page_in_cr3` write bit 63 of a PTE when
called with the `PT_NX` flag, and `test_nx` confirms the bit lands in
the PTE.  But `EFER.NXE` (bit 11 of MSR `0xC0000080`) is clear:~~

```
=== VMM Dashboard ===
  CR3 Root : 0x00000000002FB000
  WP Active: No
  NX Active: No
```

~~`user_syscall_init` sets `EFER.SCE` (bit 0) but not `EFER.NXE`.  Nothing
else in the boot path enables it.  So every page the kernel marks
non-executable is still executable.~~

~~**What this actually meant, mode by mode.**~~

~~- **Under KVM.**  Worked, by accident.  KVM's shadow page-table
  implementation enables NX on the host side regardless of the guest's
  `EFER.NXE` value.  The host MMU honored bit 63 in the guest's PTEs as
  if it were valid, so NX was genuinely enforced at runtime.  This was
  not a property of the guest kernel; it was a property of KVM's shadow
  MMU being more forgiving than the architecture requires.~~

~~- **Under TCG.**  QEMU's software MMU may or may not have emulated the
  reserved-bit rule faithfully.  The observed behavior (no fault on
  `test_nx`'s PTE) suggested TCG was lenient in the QEMU version used.
  Not guaranteed across QEMU releases.~~

~~- **On bare metal.**  The SDM is explicit: "If IA32_EFER.NXE = 0 and
  the P flag of a PDE or a PTE is 1, the XD flag (bit 63) is
  reserved," and a reference using such an entry causes a page-fault
  exception.  A real CPU would `#PF` the moment the kernel touched an
  NX-marked page.~~

~~**The fix (now applied).**  In `kmain`, before `sti`:~~

```c
/* EFER.NXE requires CPUID.80000001H:EDX.NX (bit 20). */
uint32_t eax, ebx, ecx, edx;
__asm__ volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(0x80000001));
if (edx & (1u << 20)) {
    uint64_t efer = rdmsr(0xC0000080);
    wrmsr(0xC0000080, efer | (1ULL << 11));
    PRINT_BOTH("CPU: EFER.NXE enabled\n");
} else {
    PRINT_BOTH("WARN: CPU lacks NX; PT_NX mappings will #PF\n");
}
```

~~The CPUID guard is required, not optional: the SDM says processors
that do not support CPUID leaf `0x80000001` do not allow
`IA32_EFER.NXE` to be set, and `wrmsr` on such a CPU would `#GP`.~~

~~**Why the fix was safe for the kernel.**  No page mapped by `pmm`,
`vmm`, the heap, the kernel stacks, the page tables, or the ELF loader
was mapped with `PT_NX` at the time of the fix.  `elf_load_into_process`
maps every segment with `0x1F`, which is a separate bug (data segments
should be NX), but it meant no currently-mapped page had bit 63 set.
The only pages that ever got `PT_NX` were the ones `test_nx` and
`nxtest` map, and both unmap before returning.  Enabling NXE changed
nothing that ran; it made the bit honest.~~

**Follow-up once NXE is on.**  `elf_load_into_process` should map
non-executable segments (data, BSS, user stack) with `PT_NX`.  With
NXE now on, that change becomes possible; it was not before.  This is a
natural companion to the fix, not a prerequisite — the fix is done and
verified, the ELF-loader change is a separate, optional enhancement.

**Claims in the other docs.**  `README.md`, `ROADMAP.md`, and
`OSDev_Checklist.md` describe NX as supported.  With `enable_nx()` in
place, that is now accurate at the hardware level, not just at the
software level.

### 3h. ~~The GDT lives in low memory~~ ✅

**Status:** ✅ **DONE (commit `a3ee0d2`).** `gdt_init` in `gdt.c` now
builds a kernel-owned GDT (`kernel_gdt[16]` in `.bss`) and `lgdt`s it
before `idt_init` runs.  `gdt_set_tss` writes the TSS descriptor
directly into `kernel_gdt[7..8]` instead of through `sgdt`.
`gdt_fix_user_segments` is now a no-op (the user descriptors are
written correctly by `gdt_init` on the first pass).  The call from
`kmain` is removed.

Verified end-to-end on single-drive:
- `gdtdump` reports `GDT base=0xFFFFFFFF80271540`, not `0x101DC`.
- `selftest` reports `15 passed, 0 failed`.
- `test_gdt`'s strict assertion (`base >= 0xFFFFFFFF80000000ULL`) is
  back in place and passes.
- The user shell's SYSRET path (CS=0x33, SS=0x2B) still works.

Text below kept for history: it describes the state before the fix.

~~**Effort:** 2–3 hours to rebuild the GDT at a higher-half address~~
~~**Found by:** 5a-ii (`test_gdt`'s base assertion, commit `182c1ef`)~~

~~`sgdt` reported the GDT base as `0x101DC`, inside the first 64 KB of
physical memory:~~

```
GDT base=0x00000000000101DC limit=0x0000000000000047 entries=9
```

~~This was the bootloader's GDT.  `gdt_init` in `gdt.c` was a no-op
("Using the bootloader's GDT — nothing to build here"), and
`entry.asm` never relocated it into the higher half.  The kernel read
the GDT through the bootloader's identity map.~~

~~**Why this was fragile:** the same reason item 3a was fragile before
it was fixed.  The identity map was currently stable, but nothing
guaranteed it.  If it were ever narrowed — e.g. to reclaim low memory
for the PMM — `sgdt` would still have returned `0x101DC`, and the
first `ltr`, `lldt`, `lgdt`, or segment load that consulted the GDT
would have faulted with a non-obvious cause.  The failure would have
appeared at an unrelated site (wherever the next descriptor load
happened), not at the boot.~~

~~**The fix (now applied).**  `gdt_init` builds the kernel's own GDT in
a static `kernel_gdt[16]` array in `.bss`, at a higher-half address
the kernel controls, and `lgdt`s it.  Every consumer — `tss_init`'s
`ltr`, `gdt_dump`, `isr13_handler`'s descriptor walk, the user
segments used by SYSRET — reads from that table.~~

~~**The self-test's strict assertion is back.**  `test_gdt` now
requires `base >= 0xFFFFFFFF80000000ULL`.  Before the fix it was
relaxed to "base must be non-zero" so the self-test could pass while
the GDT lived in low memory.~~

### 3i. Clang 22.1.8 miscompiles FatFs (`ff.c`) at every optimization level 🔵 dons-os era

**Status:** ✅ **WORKED AROUND (v0.5.2).** `fatfs/ff.o` and
`fatfs/ffunicode.o` are compiled with `/opt/cross/bin/x86_64-elf-gcc`
at `-O2` instead of Clang. GCC produces correct code and links cleanly
with the Clang-built kernel objects. **The workaround carried forward
into donix unchanged.**

**Effort:** none until both Clang and LLD handle `ff.c` correctly

Three separate toolchain bugs affect this file. They were discovered
in order, and each was initially treated as a separate issue. They are
grouped here because they all have the same fix — do not use Clang for
this file.

**Bug 1 — LLD 22.1.8 truncates instructions at `-O2`.**
At `-O2`, linking `kernel.elf` with `ld.lld` 22.1.8 produces truncated
instructions in `check_fs` and `move_window`. The standalone `.o` file
is correct; the truncation appears only in the linked binary.

```
ffffffff8011002a:	83 f8 03             	cmp    $0x3,%eax
ffffffff8011002d:	0f                   	.byte 0xf
ffffffff8011002e:	87                   	.byte 0x87
ffffffff8011002f:	80                   	.byte 0x80
```

The instruction at `0xFFFFFFFF8011002D` should be a 6-byte `ja rel32`.
Only the first 3 bytes are present. Runtime effect: `#UD` at the
truncated instruction.

**Bug 2 — Clang 22.1.8 hangs `f_unlink` at `-O1`.**
At `-O1`, `unlink` hangs. The CPU enters `f_unlink` or
`remove_chain` and never returns. Adding `serial_print` calls to
`f_unlink` and `remove_chain` makes the hang go away, which is
characteristic of a register-allocation-dependent codegen bug.

**Bug 3 — Clang 22.1.8 breaks the FILINFO read path at `-O0`.**
At `-O0`, `f_readdir` fills `FILINFO` incorrectly: filenames decode as
garbage (e.g. `@80(`), and `f_opendir` returns `FR_INT_ERR`. The
`sizeof(FILINFO)` reported by the compiler is correct (280 bytes), so
this is a struct field offset or stack-frame computation bug, not a
size computation bug.

**The shared fix.** Compile `fatfs/ff.c` and `fatfs/ffunicode.c` with
the cross-GCC:

```
FATFS_CC := /opt/cross/bin/x86_64-elf-gcc

FATFS_CFLAGS := -ffreestanding -O2 -Wall -Wextra \
	-mcmodel=kernel -fno-pic -mno-red-zone -mno-sse -mno-sse2 \
	-mno-avx -mno-mmx -Iinclude -Ifatfs $(FAT_CFLAGS) \
	-MMD -MP

fatfs/ff.o: fatfs/ff.c
	$(FATFS_CC) $(FATFS_CFLAGS) -c fatfs/ff.c -o fatfs/ff.o

fatfs/ffunicode.o: fatfs/ffunicode.c
	$(FATFS_CC) $(FATFS_CFLAGS) -c fatfs/ffunicode.c -o fatfs/ffunicode.o
```

`diskio.c` stays with Clang; it is small, hand-written, and does not
exhibit any of the bugs above.

**Verified.** With GCC-compiled `ff.o` and `ffunicode.o`:
- `fatmount` and `fatls` show `HELLO-WORLD.TXT` (long name).
- `unlink` completes and does not hang.
- The multi-file test in the user shell creates, verifies, deletes,
  and confirms deletion of three files.
- `selftest` passes 17/17.

**Why GCC and Clang objects link together.** Both emit ELF64
relocatable objects using the x86-64 System V ABI for the
`x86_64-unknown-elf` target. The calling convention, struct layout,
object file format, and relocation types are identical. There is no
linker-level distinction between a GCC `.o` and a Clang `.o`.

**Report.** See `LLD_BUG_REPORT.md` for the LLD issue (filed upstream
as `llvm/llvm-project#224938`) and for the two Clang issues (not yet
filed; no reduced test case).

**When to revisit.** If Clang and LLD are both updated, retry
compiling `ff.c` with Clang. If all three bugs are gone, remove the
GCC override. Until then, `FATFS_CC` stays.

### 3j. ~~`SYS_EXEC` bring-up surfaced three latent memory-safety bugs~~ ✅ 🔵 dons-os era

**Status:** ✅ **DONE (v0.5.3).** All three are fixed. This entry is
kept for history and to document the *specific symptom* each bug
produced, because each symptom was subtle enough that a reader who
sees it recur will want the fingerprint.

**Effort:** each was a small fix, but collectively this was a multi-day
debugging session because the three bugs were entangled and each one's
symptom masked the others.

#### Bug A — PMM allocator reentrancy

**Symptom:** `sys_exec`'s path-string argument arrived at the kernel
*corrupted*, with individual bytes replaced by values that depended on
kernel layout. The corruption varied between rebuilds. Early in the
session this looked like a compiler bug or a `safe_copy_from_user`
issue; it was neither.

**Root cause:** `pmm_alloc_page` did a non-atomic read-modify-write on
the shared bitmap:

```c
if (!bitmap_test(page)) {   // read
    bitmap_set(page);       // write
    return page * PAGE_SIZE;
}
```

Between the read and the write, a timer IRQ could fire. If the timer's
handler (or the process it dispatched) also called `pmm_alloc_page`,
the second call would see the same page as free, mark it used, and
return it. Two callers would then own the same physical page. When one
of them was later freed, the other's data was silently lost.

**Fix:** wrap the entire critical section in `cli`/`sti` in
`pmm_alloc_page`, `pmm_free_page`, `pmm_reserve_page`, and
`pmm_unreserve_page`. Use `pushfq`/`pop` to save and restore the
caller's IF so the allocator remains callable from IRQ context (where
IF is already clear) as well as from normal kernel code.

**Note on SMP:** `cli`/`sti` is a uniprocessor fix. On SMP this would
need a spinlock instead. The kernel is currently single-core.

#### Bug B — page-table aliasing in `vmm_clone_page_table`

**Symptom:** the user shell's `.text`, `.rodata`, and user stack were
silently replaced by the child process's contents. The shell would
start executing the child's code with the child's stack. Symptoms
included user-mode `#PF` at RIP=0 and RIP=1, "hello world" banner
prints appearing in the shell's own output, and the shell's
`spawn("0:/HELLO.ELF")` argument being read as the child's `.rodata`
bytes.

**Root cause:** `vmm_clone_page_table` copied the source PML4's
entries into the new PML4 verbatim. That shares every PDPT, PD, and PT
between parent and child. Any subsequent `vmm_map_page_in_cr3` on the
child walked the shared leaf PT and overwrote the *parent's* PTE. So
mapping the child's ELF segments or user stack also remapped the
parent's code, `.rodata`, and stack.

**Fix:** deep-copy the low-half page-table hierarchy (PML4 entries
0..255), allocating fresh PDPT/PD/PT pages at each level. Copy the
leaf PTEs verbatim — they still point at the same physical data pages,
which is correct for shared kernel mappings and harmless for user
mappings because the child's `elf_load_into_process` overwrites the
entries it cares about. PML4 entries 256..511 (HHDM and kernel) are
shallow-copied: they are shared by design and are never remapped
per-process.

**Note:** with the deep copy in place, each process owns its full
low-half page-table hierarchy. The page-table teardown story in §3c
now needs to walk and free these tables, not just the leaf data pages.
Each clone is currently a PML4 + a few PDPT/PD/PTs, so the leak is
slightly larger than it was before v0.5.3 — still bounded by the
32-slot PCB pool.

#### Bug C — `context_switch` resumed processes by `entry_point`

**Symptom:** after `wait4` returned, the user shell *restarted from
its entry point* instead of resuming where it had blocked. The restart
cleared `.bss`, reinitialized the C library, and re-entered `main`.
Global state was not designed to survive a second initialization, so
`printf` misbehaved for some format strings — the shell accepted some
keystrokes (the ones that use the syscall path directly) but ignored
others (the ones that go through `printf`).

**Root cause:** the resume side of `context_switch` chose between the
user and kernel resume paths by comparing `next->entry_point` against
`KERNEL_BASE`:

```asm
    mov rax, [r12 + 0x38]              ; next->entry_point
    cmp rax, 0xFFFFFFFF80000000
    jae .kernel_task
    ; --- User process path ---
    ; rebuild iretq frame from entry_point and user_stack_top
```

This is correct for the *first* dispatch of a fresh process, because
`process_create` builds the frame with `entry_point` in the RIP slot.
But it is wrong for resuming a process that was preempted or blocked
in kernel mode: such a process has `entry_point = 0x400000` (a user
address) but its saved frame has `CS = 0x18` and a kernel RIP.
The old code took the user branch and *rebuilt* the frame from
`entry_point` and `user_stack_top`, silently restarting the process
at `_start`.

**Fix:** the resume side now trusts the saved frame verbatim and
chooses the validation rule from the frame's own CS:

```asm
    mov rsp, [r12 + 0x110]
    mov rbx, [rsp + 0x80]               ; saved CS
    cmp rbx, 0x18
    je .frame_cs_kernel
    cmp rbx, 0x33
    je .frame_cs_user
    ; unknown CS: emit 'C' and halt
.frame_cs_kernel:
    ; RIP must be >= KERNEL_BASE
.frame_cs_user:
    ; RIP must be <  KERNEL_BASE
.pop_frame:
    ; pop 15 GPRs, iretq
```

This handles all three cases — fresh-start, preempted-user, and
preempted-kernel — with one code path. The diagnostic marker bytes
('C' for bad CS, 'K' for kernel-frame-with-user-RIP, 'U' for
user-frame-with-kernel-RIP) go to COM1 and are documented in the
`.skip_save` comment block.

**Related:** the earlier `mov r12, [r12 + 0x0B0]` register-clobber
bug in the same file was fixed in the same session. The user path used
to load `r12` from the PCB *before* building the iretq frame, which
destroyed the PCB pointer before the two `push [r12 + offset]` reads
that followed. Fixed by reordering the loads so `r12` is clobbered
only after the frame is complete.

### 3k. `sys_execve` cannot write to another process's address space via the standard helpers

**Status:** fixed in v0.5.4 (`safe_copy_to_user_cr3`). Kept for history.
**Effort:** 45 minutes to diagnose, 20 minutes to fix.

**Symptom:** with argv passing enabled, the child received the correct
`argc` but every `argv[i]` was NULL or garbage. `cat HELLO-WORLD.TXT`
printed `cat: 0:/(null): cannot open`; `echo one` printed a single
garbage byte.

**Root cause:** `safe_copy_to_user` and `safe_copy_from_user` resolve
their user address against `process_get_current()->cr3`. That is
correct for a normal syscall, where the caller and the target are the
same process. `sys_execve` is unusual: it writes into a *child*
process whose `cr3` is different from the caller's. The first attempt
switched `cr3` to the child's around the write, but `safe_copy_to_user`
still read `current->cr3` (the caller's) and therefore resolved the
child's virtual addresses against the caller's page tables. Because
both processes use the same user-stack virtual addresses
(`0x80000F0000..0x8000100000`), the write succeeded — into the
*caller's* stack, not the child's. The child then read its own
(freshly zeroed) stack page and found the argv array full of zeros.

**Fix:** add `safe_copy_to_user_cr3(uint64_t cr3, ...)`, which takes
the target `cr3` explicitly and calls `vmm_get_phys_from_cr3(cr3, ...)`.
No `cr3` switch is required: `vmm_get_phys_from_cr3` walks the target
page tables via the HHDM, and `HHDM_START + phys` is mapped in every
address space (it's in the shared kernel higher half).

**Lesson:** when writing to a process other than the current one, use
the `_cr3` variant. The default helpers silently assume "current
process is the target."

### 3l. The child's argv region shares the top of the user stack

**Status:** works for present programs; a latent landmine
**Effort:** 30 minutes if it ever bites; ~2 hours to do properly
**Introduced by:** v0.5.4 Stage 4

`sys_execve` reserves 4 KB at the top of the child's user stack for
the argv region. Strings are placed at the **bottom** of that region
(growing up), the pointer array sits just below the strings, and the
top of the region is left free as a gap between argv and the child's
own downward-growing stack frames.

**The landmine:** the free gap is what protects argv from the child's
stack. For the current programs the gap is ~3.5 KB (4 KB region minus
array bytes minus string bytes), and `echo`/`cat`/`ls` use at most a
few hundred bytes of stack. A program that recurses deeper than the
gap will overwrite its own argv silently. Nothing detects this today.

**When it will bite:** the first nontrivial recursive user program —
a tree walker, a recursive descent parser, `find`. Or an argv with
many long arguments (16 args × 256 bytes = 4 KB of strings alone,
plus the 136-byte array, plus any slack the caller passes).

**Options if it becomes a problem, in order of preference:**

1. **Grow the reservation.** Reserve 8 KB or 16 KB instead of 4 KB.
   The user stack region is 16 pages (64 KB) total; reserving 16 KB
   for argv still leaves 48 KB for the child. This is the smallest
   change and covers all realistic argv sizes.
2. **Move argv to its own page.** Map a page at a fixed address above
   the stack (e.g. `0x8000100000`) with `PT_USER | PT_WRITE`, put
   argv there, and set `rdi`/`rsi` accordingly. The stack has no
   interaction with argv at all. More invasive but the cleanest.
3. **Copy argv into the child's BSS.** Requires cooperation from the
   libc's `_start` and a linker section; ugly.

**Why it is fine for now:** every userland program in the tree today
uses a shallower stack than the gap. The bug is real but cannot be
triggered by anything currently on the FAT image.

**Related:** the same 4 KB region is also where a *future*
`sys_execve` caller might want to place `envp`. If `envp` is added
before this item is fixed, the gap shrinks further. Fix this before
adding `envp`.

### 3m. ~~The initial child `rsp` is not SysV-compliant~~ ✅ — *for musl*

**Status:** ✅ **RESOLVED for musl.** The original complaint was about
the newlib-era `arc2/crt0.S`, which read `argc`/`argv` from `%rdi`/
`%rsi` instead of the SysV stack layout. With newlib retired (A5 step
7), all userland programs use musl's `_start`, which reads the SysV
stack layout. `sys_execve` writes that layout correctly.

Text below kept for history: it describes the state before the
migration and the concern that motivated the SysV layout work.

~~**Effort:** n/a now; significant if a prebuilt binary is ever linked~~
~~**Introduced by:** v0.5.4 Stage 4~~

~~In standard SysV, `_start` reads `argc`, `argv`, and `envp` off the
initial stack. Our `crt0.S` reads them from `rdi` and `rsi` instead:~~

~~- `sys_exec` writes `argc` to resume-frame slot 9 (rdi) and the
  `argv` array pointer to slot 10 (rsi).~~
~~- `crt0.S` stashes `rdi` and `rsi` in a `.data` slot at the very top
  of `_start` — *before* the BSS-clear loop, which clobbers `rdi`
  and `rsi` via `rep stosb` — and reads them back just before
  `call main`.~~

~~**Why this was fine:** every userland program in the tree linked our
own `crt0.S`. The convention was internal to dons-os.~~

~~**When it would have broken:** the day someone tried to link a
prebuilt static binary (a compiler's `crt1.o`, a precompiled test
suite, a third-party program) that expects the stack-based convention.
That program's `_start` would read garbage for `argc`/`argv` and would
almost certainly crash or behave nonsensically.~~

~~**If SysV compliance were needed:** write the argv array and strings
onto the stack in the SysV layout (argc at `rsp`, then `argv[]`, then
a NULL, then `envp[]`, then a NULL, then strings), and set the initial
`rsp` to point at `argc` rather than at `user_stack_top`.~~

**What actually happened:** this item was resolved as a side effect of
the musl migration. Item A7.5 in [`OSDev_Checklist.md`](OSDev_Checklist.md)
records the `%rdi`/`%rsi` write; item A7.6 records the argv layout
correction. See §0.6 for the follow-up (removing the now-unnecessary
`%rdi`/`%rsi` writes).

---

## 4. Code hygiene

**Status:** ✅ **DONE for 4a–4d.** Items 4e, 4f, 4g, 4h are architectural,
documentation-only, or code-review discipline and deferred. Item 4i is
open. Item 4j is new.

### 4a. ~~Dead declarations~~ ✅

**Status:** ✅ **DONE (commit 20260919F).** Removed the orphaned
`sys_reboot` and `sys_proclist` declarations from `include/syscall.h`.
`sys_reboot` is defined in `arc2/syscalls.c` (userland) and the
kernel-side handler is `kernel_do_reboot` (static in `user_syscall.c`);
nothing in the kernel called the declared prototype. `sys_proclist`
was not defined anywhere — the shell's `proclist` command calls
`process_dump_all` directly.

~~`include/syscall.h` declares `void sys_reboot(void);`. The kernel-side
handler is now `kernel_do_reboot` (static, in `user_syscall.c`). The
declaration in `syscall.h` is likely unused. Check and remove.~~

### 4b. ~~The double-build in `run`~~ ✅

**Status:** ✅ **DONE (commit 20260919F).** The single-drive menu line
now calls `make -C 05_boot_kernel64 run-single` directly instead of
`make runkernel64-single`, so the kernel is built once, not twice.

~~The single-drive menu line rebuilds the kernel twice: once explicitly
in the line, and once via `runkernel64-single`, which itself does
`make -C 04_kernel_64bit FAT_CONFIG=single`. Two-line fix: either
drop the explicit rebuild from the menu line, or drop it from the
target. The `run` script's echoed `run: <cmd>` makes this visible in
the log.~~

### 4c. ~~Stale comments~~ ✅

**Status:** ✅ **DONE (commit 20260919F).** Removed the two stale
`FIXED:` markers from `arc2/syscalls.c`. Deleted the commented-out
`file_exists` helper from `user_shell.c`. Tidied the header comment
block in `stage2.asm` (consolidated the low-memory layout and history
into one readable block, removed the mid-thought narrative about
`KERNEL_TOTAL_BYTES`).

~~- `arc2/syscalls.c` has several `FIXED:` comments from earlier sessions.
  They were fixed long ago; the labels are noise now. Remove them.
- `user_shell.c` has a `file_exists` helper that is currently unused.
  Delete it.
- `stage2.asm`'s comments about "was 64 sectors" are accurate but
  accumulate cruft every time the layout changes. Consider a single
  "history" section rather than inline archaeology.~~

**Note for donix:** the `arc2/` tree and `user_shell.c` were removed
at A5 step 7; this item is closed by deletion.

### 4d. ~~Serial output is not atomic~~ ✅

**Status:** ✅ **DONE (commits 20260919G, 20260919H).** Two commits:

- `20260919G` wrapped multi-part boot messages in `serial_lock()` /
  `serial_unlock()` and gated the ATA read-path prints behind
  `#define ATA_DEBUG 0`.
- `20260919H` made the lock shared between the serial and VGA drivers,
  made `serial_print` / `serial_print_hex` / `serial_print_dec` /
  `serial_write` take the lock around their whole operation, and did
  the same for `vga_print` and friends. `serial_lock` now saves
  `RFLAGS` on the outermost acquisition and `serial_unlock` restores
  it, so the lock is safe inside interrupt context.

The result: `vga_print("DonsDOS v0.5.0\n> ")` is atomic against timer
preemption. The `DonsDOS v` truncation is fixed. `serial_print` is
atomic; `**RING** 3 syscalls active` no longer gets split by the
timer's boot trace.

~~`PRINT_BOTH(str)` does `vga_print(str); serial_print(str);` as two
separate calls. If a timer tick fires between them (or between two
adjacent `PRINT_BOTH` calls in a multi-part message), the timer's own
serial output interleaves. Observed as `Storage: single-drive, FAT@Å
Prompt: press 'k' ...` and `ATA: probe driveTIMER[2] ...` in the
boot log. Cosmetic, but a real exception dump interleaved with another
process's output is very confusing.~~

### 4e. Print lock is a pragmatic fix, not the console design

**Status:** works for boot messages and single-console operation; not
the right shape for a tty
**Effort:** 1–2 hours to replace with a ring buffer
**When:** before the console subsystem (tty, per-user output routing)
is built

The shared print lock (item 4d) is a `cli`/`sti` critical section with a
nesting counter and RFLAGS save/restore. It is simple, correct, and
composes properly with interrupt context. But it is a **global** lock,
held with interrupts disabled, and this has two real costs:

1. **Scheduling fairness.** While any kernel code is printing, the
   timer does not fire. A 40-char line at 115200 baud is ~3.5 ms with
   interrupts off; a round-robin quantum of 10 ms (100 Hz tick) is
   silently stretched by that much for whichever process was running
   when the print started. Fine during boot. Not fine if the kernel
   ever prints from a scheduled context during normal operation.

2. **SMP and multi-user scale.** `cli`/`sti` is meaningless across
   CPUs, and a single global lock serializes output from unrelated
   users. Once the kernel supports multiple users or cores, this
   needs to become a per-console lock, or a proper tty.

The right design is a **ring buffer with a console task**:

- `serial_print` / `vga_print` memcpy their bytes into a fixed-size
  FIFO under a very short lock (microseconds, not milliseconds).
- A drain routine — a low-priority kernel task, a serial TX-ready
  IRQ handler, or a periodic tick — pulls bytes out of the FIFO and
  writes them to the UART / VGA.
- Producers never wait on the hardware. Interrupts are off for the
  memcpy only.
- Ordering is preserved by the FIFO's single consumer.

Once per-user ttys exist, each tty gets its own ring and its own
drain path, and the output of user A's process cannot block or
interleave with user B's.

This is not urgent — nothing in the current code prints during
normal operation after boot, now that the ATA read-path prints are
gated off. But it is the design the console subsystem should be
built on, and it should be in place before per-user tty support is
added.

**Observed symptom, pre-fix, that this design addresses:** in an
early capture, the user shell's banner appeared twice, with the
second copy truncated mid-word. The kernel's `PRINT_BOTH` output and
the user shell's `printf` (via `sys_write`) were both writing to the
same console, and the kernel's prompt logic raced with the user
shell's banner. The print lock covers kernel-side prints; it does not
cover user-mode `sys_write` output. This is the class of interleaving
the ring buffer design fixes once a tty layer exists. Not urgent.

**Related:** item 4i is the specific, ongoing symptom. This item is
the architectural fix.

### 4f. Print functions must remain leaf functions

**Status:** invariant; not yet violated
**Effort:** n/a (documentation and code review discipline)

The shared print lock is safe because nothing inside the print path
takes another lock. If a future `vga_print` implementation decides to
allocate a line buffer with `kmalloc`, and `kmalloc` takes a heap lock,
and some other path takes the heap lock then calls `print`, you have a
lock-order inversion.

Keep the invariant:

> **Print functions (`vga_*` and `serial_*`) may not take any other
> lock.** If a print routine needs to allocate, it must do so before
> acquiring the print lock, or use a fixed-size stack buffer.

Violating this creates the possibility of a lock-order inversion with
the heap lock (or any other lock introduced later). Nothing in the
current code violates it; this entry exists to keep it that way.

### 4g. Diagnostic for print-lock hold duration

**Status:** future diagnostic, not needed yet
**Effort:** ~30 minutes when it's needed

If the print lock ever starts holding interrupts off long enough to
matter (see item 4e, cost 1), the way to know is a small counter:

- In `timer_preempt_handler`, on each tick, compute the drift between
  `g_ticks` and `expected_ticks`, and record the maximum.
- Expose it via a `printstat` shell command alongside `schstat`.

A boot log or `schstat` output that shows the max drift creeping above
1 tick is the signal that the print path is starting to matter and the
ring buffer (item 4e) is due.

This is not something to build now. It's the tool you'll reach for if
you ever wonder "is printing slow?"

### 4h. Comments that name functions by role

**Status:** ongoing discipline; two instances found in an earlier session
**Effort:** n/a (code review discipline)

When a function changes role — `gdt_fix_user_segments` going from
load-bearing to a no-op, `gdt_init` going from a no-op to load-bearing,
a test program's tail comment describing an exit path that was
never taken — comments elsewhere that *name* those functions as "the
thing that does X" become silently wrong.  There is no mechanical way
to catch this: the compiler cannot see it, the linker cannot see it,
and no test in the tree covers documentation.

Two instances surfaced in an earlier session:

1. **`test_gdt`'s header comment in `kmain.c`** still named
   `gdt_fix_user_segments` as the function that builds the user
   descriptors.  After 3h, `gdt_init` is the authoritative builder;
   `gdt_fix_user_segments` is a no-op.  Caught during code review of
   the 3h patch.
2. **A test program's header comment** described a "must terminate
   via `SYS_EXIT`" contract that was never true for a user-mode
   process in this kernel — user processes halt on exit by design.
   The comment was removed rather than corrected.

**Discipline:** before committing a change that alters a function's
role, `grep` the tree for the function name and update every comment
that names it as the doer.  This is cheap (a `grep -rn` takes seconds)
and catches the class of drift that no automated check can.

Not urgent, and there is nothing to build here.  This entry exists so
that a future reader knows the class of bug is known and that reading
comments near touched code is part of the review discipline, not an
afterthought.

**Relevance for donix:** the A1–A6 migration renamed or removed many
functions (`sys_spawn`, `sys_sbrk`, `sys_arch_set_fs`,
`sys_opendir`/`sys_readdir`/`sys_closedir`, `build_user_shell_elf`,
`user_shell_data.c`, `build_musl_tests.sh`). Any comment that still
names them as "the thing that does X" is now silently wrong. A
full-tree grep for the removed names is a reasonable cleanup commit at
some point; the doc pass at A6.25 removed the ones in the docs.

### 4i. Kernel log and userland output share the same console and interleave

**Status:** cosmetic; visible in every `musl_sh` session
**Effort:** ~1 hour (route kernel logs to serial only, or add a
`klog_enable` syscall)
**Introduced by:** the REPL shell that started spawning many
short-lived processes, each producing a kernel log line on `execve`

Every `execve` prints `sys_execve: pid=N entry=0x... argc=N rsp=0x...
(NAME)` to the console via `serial_print`. `sys_open` prints a line
for every `f_open` failure. These kernel lines interleave with
userland output:

```
donix> musl_sh
sys_execve: pid=3 entry=0x400221 argc=1 rsp=0x80000FEFD8 (hello.ELF)
hello from donix (musl)
```

The kernel's `sys_execve: ...` line was emitted mid-line with the
child's own `printf` output spliced in. Cosmetic only — nothing is
corrupted, the program runs correctly — but it makes the shell output
hard to read, and it will get worse as more processes run.

`musl_readdir` produces `Unknown syscall: N` lines for several
unimplemented syscalls (see item 0.1 for `fcntl`; the others are
`lstat`, `mkdir`, `creat`, `rt_sigreturn`, `pread64`). Those lines are
correct in content but noise on the console.

**Options, in order of preference:**

1. **Route kernel logs to serial only.** `serial_print` writes to
   COM1; `vga_putc` writes to the console userland also uses. Today
   both `serial_print` and `vga_putc` write to the console (the
   Makefile wires `-serial stdio`, and the shell reads/writes
   fd 0/1 which the kernel routes to both). If `execve`'s log and
   `open`'s failure log go to `serial_print` only (which under
   `-serial stdio` is the same terminal as the console, but
   *asynchronously* on the host's terminal side), the interleaving
   may or may not persist depending on how QEMU multiplexes.
   Honest answer: this needs testing.
2. **Add a verbosity syscall.** `SYS_KLOG(0)` disables kernel logs to
   the console; the shell calls it on startup. Kernel logs still go
   to serial. Userland decides when to silence the kernel.
3. **Gate the logs behind `#define`.** `#define EXEC_TRACE 0`,
   `#define OPEN_TRACE 0` in `user_syscall.c`. Simplest; loses the
   logs entirely, even for bring-up.

**Recommendation:** option 2, once a real console/tty subsystem
exists (item 4e). For now, option 3 if the noise becomes distracting
during development.

**Related:** item 4e is the underlying architectural fix; this entry
is the specific symptom that made it visible.

### 4j. `sys_execve` writes `%rdi`/`%rsi` unnecessarily

Duplicated from §0.6 for the hygiene-tracking view. See §0.6.

---

## 5. Testing infrastructure

**Status:** partial; fault-path and non-fault coverage exist, context-switch
coverage does not
**Effort:** ~3 hours remaining (5b, 5c, 5e)

### 5a. Kernel-shell self-test

**5a-i and 5a-ii are done.  5a-iii is declined.  5a-iv is done.**

- **5a-i ✅ DONE (commit `f132903`).**  Expected-fault protocol
  (`g_expect_fault` / `g_fault_observed` / `fault_kill_current`) and
  a `selftest` command that runs the three exception tests.  Each
  test spawns a kernel-mode child whose entry point is a small
  trigger function, sets the expected vector, yields to the child,
  and checks `g_fault_observed` when the shell resumes.  The three
  exception handlers (`isr0_handler`, `isr13_handler`,
  `isr14_handler`) gained a guard at the top: if `g_expect_fault`
  matches their vector, they call `fault_kill_current` instead of
  the diagnostic-and-halt path.  Verified end-to-end on
  single-drive: `3 passed, 0 failed`.

  This is the first time the exception handlers have been exercised
  by anything.  Prior to this commit, a broken `isr14_stub` frame
  offset or a mis-wired IDT gate would only have been visible by
  typing `test` and reading the dump by hand.

  Files touched: `interrupts.h`, `interrupts.c`, `kmain.c`.  No
  changes to `process.c`, `scheduler.c`, `context_switch.asm`,
  `isr.asm`, or `process.h`.  The `_Static_assert` offsets in
  `process.c` remain valid because `pcb_t` is untouched.

  **What is not covered by 5a-i:** the handlers' *diagnostic* path
  (`g_expect_fault == -1`) is untested — that's the path the `test`
  command takes.  User-mode faults are untested; see items 3f and
  0.4 for the coupling that makes this non-trivial.

- **5a-ii ✅ DONE (commit `182c1ef`).**  Every non-fault test that
  had an inline body in `handle_command` was refactored into a
  `static int test_xxx(void)` that returns `SELFTEST_PASS` or
  `SELFTEST_FAIL`.  The shell commands became one-line wrappers
  around those functions; `selftest` calls the same functions and
  reads the return value.  Verified end-to-end on single-drive:
  `15 passed, 0 failed`.

  Coverage added:
  - `test_gdt` decodes the GDT and asserts: null descriptor is zero;
    kernel code (`0x18`) present, DPL=0, code, L=1; kernel data
    (`0x20`) present, DPL=0, not code; user data (`0x28`) present,
    DPL=3, not code; user code (`0x30`) present, DPL=3, code, L=1;
    TSS (`0x38`) present, system, type 0x9 or 0xB.
  - `test_tss` asserts: TR == 0x38; RSP0 and IST1 are non-zero,
    16-byte aligned, in the higher half; `iopb_base == sizeof(tss_t)`;
    `g_syscall_stack_top == tss->rsp0` (the v0.4.9 lockstep invariant).
  - `test_pmm`, `test_map`, `test_nx` now free the physical page they
    allocate, so `selftest` is idempotent across runs (modulo item 3c).
  - `test_ata` asserts the 0xAA55 MBR signature in addition to the
    read succeeding.
  - `test_nx` walks the page tables via HHDM and asserts the NX bit
    (bit 63) landed in the final PTE.

  Findings filed during this commit:
  - **Item 3g.** `test_vmm`'s NXE check failed on first run: `EFER.NXE`
    is not set.  The test was relaxed to a printed observation; the
    underlying gap is filed as item 3g.
  - **Item 3h.** `test_gdt`'s base-address check failed on first run:
    the GDT lives at `0x101DC`, in low memory, read through the
    bootloader's identity map.  The test was relaxed to "base must be
    non-zero"; the underlying fragility is filed as item 3h.

  Files touched: `kmain.c` only.  `interrupts.c` and `interrupts.h`
  unchanged from 5a-i.

- **5a-iii — DECLINED.**  "Add `elfload` to `selftest`" was the
  original plan.  It is not being done, and the reason is a design
  decision, not a bug.

  `elfload` launches a *user-mode* process.  When a user process
  exits — by `exit` or by fault — `process_exit` halts the CPU.
  That is the intended behavior: the user shell is the terminal
  interactive console, and there is nothing behind it to return to.
  The kernel shell is a boot-time choice, not a persistent fallback.

  `selftest` is a two-way operation: it runs a test and reads back a
  result.  `elfload` is a one-way operation by design.  Putting it in
  `selftest` would mean the test never completes — not because the
  loader is broken, but because "launch a user process" and "return to
  the caller" are mutually exclusive in this kernel.

  `elfload` is already exercised indirectly: `musl_sh` uses the same
  `process_create` + `elf_load_into_process` + `scheduler_switch_to`
  path on every boot.  If the loader regressed, the shell would fail
  to start — a much louder signal than a self-test line.

  The expected-fault tests in 5a-i work because their children are
  *kernel-mode* processes (`entry_point >= KERNEL_BASE`), which
  `process_exit` resumes the shell for.  That is a property of the
  test harness, not a general rule about user processes.  See item 3f.

  Nothing here needs fixing.  If direct loader coverage is ever
  wanted, the route is a kernel-mode ELF whose entry point is in
  kernel text — which tests a different scenario than `musl_sh`
  exercises, and is not worth building just for `selftest`.

- **5a-iv ✅ DONE (v0.5.2).** Multi-file delete test. The multi-file
  test now exercises the full file lifecycle: create 3 files, write
  to each, close, reopen, read back, close, `unlink`, then confirm
  each file is gone by attempting to reopen it. Passes end to end.

  This requires `unlink` (Linux 87) and the musl wrapper's use of it.
  Both were present by v0.5.2.

  **Interaction with 3i.** This test surfaced the Clang 22.1.8
  miscompile at `-O1`. With `ff.c` at `-O1`, the delete phase hangs;
  with `ff.o` compiled by the cross-GCC, it passes.

### 5b. Boot-time self-test mode

A build flag (`-DSELFTEST`) that makes the kernel run a fixed test
sequence at boot and print results to serial, then halt. Useful for
regression checks after a change.  Unlike the interactive `selftest`
command, 5b's whole purpose is "run and stop," so it *can* include a
final user-mode step (launch `musl_sh`, let it run, halt) — that is
a design question for 5b, not part of the interactive command.

### 5c. `make test` target

A Makefile target that boots QEMU headless, runs the self-test, captures
the serial output, and reports pass/fail based on the output. Automatable
with a serial-to-file and a grep. Would let you catch regressions without
watching the boot.

### 5d. Spawn regression test

**Status:** not yet built
**Effort:** ~1 hour, once 5a-i's infrastructure is understood
**Priority:** after 5b and 5c, but before the next feature

`execve` and `wait4` are the most complex features in the tree. They
exercise three separate subsystems (`pmm_alloc_page`,
`vmm_clone_page_table`, `context_switch`) that all had memory-safety
bugs during bring-up (see §3j), and the bugs were subtle enough that
they manifested only under specific timing. A future regression in any
of the three would be caught by running a command in `musl_sh`
promptly, but only if someone runs it.

A test that runs the same path from the self-test would give a
deterministic signal. The shape:

- A kernel-mode child process whose entry point calls `sys_execve` on
  a known ELF (`0:/HELLO.ELF`), reads the returned pid, and then
  blocks in `sys_wait4` on that pid.
- The child's exit status is 0 if the spawn+wait sequence completes
  and the reaped pid matches, non-zero otherwise.
- The kernel shell's `selftest` command runs the child, waits for it
  to exit, and reads back the status.

**The complication:** `sys_execve` spawns a *user-mode* process, and
user-mode processes halt the CPU when they exit (see 5a-iii). So the
inner spawned child cannot be the one whose exit status the test
reads. The outer child (the one that called `sys_execve`) must be
kernel-mode, so its exit resumes the kernel shell. This is the same
"expected-fault triggers must be kernel-mode" constraint as item 3f,
in a different guise.

Concretely, the test would need:

1. A kernel-mode child whose entry point runs the spawn+wait+assert
   sequence and returns 0 or 1.
2. A way to communicate "this child is a self-test, don't halt on
   its inner user-mode child's exit." The mechanism for the inner
   child's exit is the standard parent-wake path: `process_exit`
   wakes the parent and the parent's `sys_wait4` returns. Only the
   *outer* child's exit is special.
3. `HELLO.ELF` present on the mounted FAT volume. This is guaranteed
   in the single-drive build (the Makefile `mcopy`s it in), but not
   necessarily in the dual-drive build unless the `fat.img` in the
   tree has it. Guard the test accordingly, or make it conditional
   on the storage configuration.

This is worth building. It closes the loop on the three fixes in §3j
by making them regression-detectable. It is not blocking any feature
work, but it should be done before the next feature lands.

### 5e. argv / REPL regression tests

**Status:** not yet built
**Effort:** ~1 hour
**Priority:** alongside 5d

The musl era added argv passing, the `echo` and `cat` programs, the
`ls` directory-listing program, and the Linux directory syscalls
(`open(O_DIRECTORY)` + `getdents64`). Several bugs found during that
work were caught by manual inspection of a serial capture, not by an
automated test.

Natural regression checks, in order of value:

1. **`fstest --verify` cross-boot persistence.** Run `fstest`,
   reboot, then run `fstest --verify`. It should print `[fstest] PASS
   (file survived reboot, byte-exact)`. This works today because
   argv is passed; before argv existed, the `--verify` argument could
   not reach the child. There is no automated way to test the cross-
   boot path — it needs two boots — but a scripted QEMU run in 5c
   could do it.
2. **argv round-trip.** `echo hello world` should print `hello world`;
   `echo one two three` should print `one two three`; `cat
   HELLO-WORLD.TXT` should print the file contents. These are the
   tests that caught the argv-layout bug. A `make test` target (5c)
   could spawn these from a scripted session and grep the output.
3. **`ls` output stability.** After a fresh boot, `ls` should list
   exactly the files on the FAT image, with correct sizes. An
   automated comparison against the `mdir` output from image-build
   time is possible but fiddly.
4. **Empty-argv path.** `echo` with no arguments should print a blank
   line; `cat` with no arguments should print `usage: cat FILE`. These
   exercise the `argc == 0` and `argc == 1` paths in `sys_execve`.

None of these are hard, but they need a test harness that can feed
input to a running QEMU and read its output. That is 5c.

### When to do the rest

5b and 5c build on 5a-i and 5a-ii.  Neither is blocked by the remaining
items on this list.  They can be done in either order; 5c is more
valuable because it enables `make test` in a loop, but 5b is a
prerequisite for the headless part of 5c.

5d depends on 5a-i (the expected-fault protocol's infrastructure) but
is otherwise independent. 5e depends on 5c. Together 5d and 5e cover
the newest and least-exercised code paths in the tree.

---

## 6. Priorities, one more time

The list below is the "do these, in this order" view.  Section 0 has
the donix-migration items; sections 3–5 have the historical items.

| # | Item | Effort | Status |
|---|------|--------|--------|
| 0.1 | `fcntl` (Linux 72) stub | 30 min | Next (busybox needs it) |
| 0.2 | `sys_newfstatat` (Linux 262) | 45 min | Next (busybox needs it) |
| 0.3 | `sys_open` `O_DIRECTORY` fix | 15 min | Whenever |
| 0.4 | `isr14_handler` user-mode fault handling | 30 min | Before busybox |
| 0.5 | `musl_sh` backspace echo | 15 min | Cosmetic |
| 0.6 | `sys_execve` `%rdi`/`%rsi` writes | 15 min | Cleanup |
| 0.7 | `PMM_ALLOC_DIAG` removal | 5 min | Cleanup |
| 0.8 | `musl_min` `-no-pie` inconsistency | — | ✅ Done (A6, v0.6.0) |
| 0.9 | `build_musl_tests.sh` cosmetic / `/tmp` staging | — | ✅ Done (A6, v0.6.0) |
| 1 | Kernel size Option 3 | 30 min | ✅ Done (09a6f79) |
| 2 | Documentation gaps | 2 hrs | ✅ Done (9b62d46) |
| 3a | Boot stack off hardcoded address | 1–2 hrs | ✅ Done (e246db6) |
| 3b | IST for `#DF` | 1 hr | ✅ Done (f4d7df3) |
| 3c | Page table teardown | 2–3 hrs | Defer until multiple processes |
| 3d | `heap_base` per-process | 15 min | Whenever |
| 3e | TLB flush in VMM | — | Leave as-is, documented |
| 3f | Self-test fault-trigger constraint | — | Documented (f132903) |
| 3g | `EFER.NXE` not enabled | 15 min + 1 hr verify | ✅ Done (028da72) |
| 3h | GDT lives in low memory | 2–3 hrs | ✅ Done (a3ee0d2) |
| 3i | Clang/LLD miscompile ff.c at all -O levels | — | ✅ Worked around (v0.5.2: cross-GCC for ff.o) |
| 3j-a | PMM allocator reentrancy | 30 min | ✅ Done (v0.5.3) |
| 3j-b | `vmm_clone_page_table` shallow copy | 1 hr | ✅ Done (v0.5.3) |
| 3j-c | `context_switch` resume-by-entry-point | 1 hr | ✅ Done (v0.5.3) |
| 3k | `safe_copy_to_user_cr3` for cross-process writes | 20 min | ✅ Done (v0.5.4) |
| 3l | argv region shares user stack top | 2 hrs if needed | Open; safe today |
| 3m | initial child rsp not SysV-compliant | — | ✅ Resolved for musl |
| 4a | Dead declarations | 15 min | ✅ Done (20260919F) |
| 4b | Double-build in `run` | 15 min | ✅ Done (20260919F) |
| 4c | Stale comments | 30 min | ✅ Done (20260919F); closed by A5 step 7 |
| 4d | Serial/VGA output atomicity | 1 hr | ✅ Done (20260919G, 20260919H) |
| 4e | Print lock → ring buffer | 1–2 hrs | Before tty/per-user console |
| 4f | Print functions as leaf functions | — | Documented invariant |
| 4g | Print-lock hold diagnostic | 30 min | Build when needed |
| 4h | Comments that name functions by role | — | Ongoing discipline |
| 4i | Kernel log and userland output interleave | 1 hr | Cosmetic; fix with tty |
| 4j | `sys_execve` `%rdi`/`%rsi` writes | 15 min | Same as 0.6 |
| 5a-i | Self-test: exception path | 2 hrs | ✅ Done (f132903) |
| 5a-ii | Self-test: non-fault tests | 1.5 hrs | ✅ Done (182c1ef) |
| 5a-iii | `elfload` in selftest | — | Declined (see §5a) |
| 5a-iv | Multi-file delete test | — | ✅ Done (v0.5.2) |
| 5b | Boot-time self-test mode | 1 hr | After busybox |
| 5c | `make test` target | 1 hr | After 5b |
| 5d | Spawn regression test | 1 hr | Before next feature |
| 5e | argv/REPL regression tests | 1 hr | After 5c |

The Clang/LLD workaround (item 3i) is temporary and depends on the
upstream fixes landing. It is not a finding in this project's code, but
it should be revisited whenever the toolchain is updated. If a future
Clang or LLD version still miscompiles `ff.c`, the report has the
reproducers.

---

## 7. Resolved bugs from the migration (for the record)

The A1–A6 migration fixed six bugs that would not have been visible on
the dons-os-only kernel. Each is documented in
[`handoff.md`](handoff.md) with the exact symptom, root cause, and
fix. This section is a short index; the handoff is the source of
truth.

| Bug | Fixed at | Summary |
|-----|----------|---------|
| `%r10` clobbered by syscall return path | `20260924L` | Linux ABI clobbers only `%rax`, `%rcx`, `%r11`. musl's stdio keeps a live pointer in `%r10` across `writev`. |
| `sys_read` on fd 0 blocked until `count` | `20260926A` | POSIX terminal semantics: return on the first available byte. |
| `brk` increment ABI vs Linux absolute-address ABI | `20260924B` | musl's `brk(0x26c41000)` was interpreted as "add", causing an unbounded page-mapping loop. |
| `MSR_FS_BASE` not saved/restored/inherited per process | `20260926E` | musl reads `%fs:0` on the child's first instruction after `fork`; the MSR was effectively per-CPU. |
| `execve` did not pass `argc`/`argv` in `%rdi`/`%rsi` | `20260926G` | newlib's `crt0.S` read them from registers; musl reads the SysV stack layout. Both were satisfied during the transition. |
| `execve`'s argv layout zeroed `argv[1]` | `20260926H` | The envp NULL terminator's slot was on top of `argv[0]`'s string. Fixed by putting `strings_start` 8 bytes higher. |

See [`handoff.md`](handoff.md) "Resolved bugs" for the full entries
and [`OSDev_Checklist.md`](OSDev_Checklist.md) §0.7 for the itemized
list.

---

## Notes

This file is not a wish list. Everything in it is either:

- **Known debt:** the design has an acknowledged limitation (the size
  ceiling, the page table leak, the print lock).
- **Missing documentation:** the code is correct but the reader can't
  find out what it does.
- **Cosmetic:** it works, it's just ugly.

If you fix something, strike it through and mark it with a ✅ rather
than deleting it. The history of what was wrong is useful to a reader.

If you find a new problem, add it here with the same format. The point
is to keep the list of "things we know we're wrong about" honest and short.

Feature work goes in `ROADMAP.md`. Capability tracking goes in
`OSDev_Checklist.md`. Debt and maintenance go here.

*Last Updated: September 2026 (donix v0.6.0)*

