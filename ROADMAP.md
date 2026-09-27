# ROADMAP
### donix (x86_64) — Project Roadmap

This roadmap outlines the evolution of **donix**, from its origin as
**dons-os** through the musl migration to the current state and beyond.

Section **0** describes the **donix milestone** (`v0.5.5`) — the
newlib-to-musl migration that produced this project. Sections 1–4 and
the per-version milestone entries through `v0.5.4` describe **dons-os
history**: the kernel that donix inherited. They are preserved here
verbatim because the milestone-level narrative — what each version
fixed, and why — is useful context for anyone reading the current
code. The kernel carried forward intact; only the syscall ABI and the
userland library changed.

Section **6** describes the next milestone, **Phase B** (busybox
against musl). The **Status Summary** has been updated for the
post-migration state.

For the session-by-session story, see [`handoff.md`](handoff.md).
For the capability checklist, see
[`OSDev_Checklist.md`](OSDev_Checklist.md). For debt and latent bugs,
see [`MAINTENANCE.md`](MAINTENANCE.md).

---

## 0. donix — the musl migration — v0.5.5 (completed)

donix was forked from dons-os at its `v0.5.4` tag. The goal was to
re-target the OS to **static musl-linked binaries** and speak the
**Linux x86_64 syscall ABI natively**, without disturbing the kernel
infrastructure that dons-os had built.

This is the work that produced donix, and it is captured by the
completion tag **`v0.5.5`**. It was done in five phases, all complete.

### A1 — pure syscall renumbering

Change syscall numbers to Linux x86_64 numbers. No semantics change, no
new syscalls. Newlib's existing userland stayed as a passive regression
canary after every subsequent change. Complete at `20260922H`.

### A2 — add the syscalls musl needs

Added, one at a time, each its own commit, tested with a tiny musl
binary and the newlib canary re-run after each:

- `arch_prctl(ARCH_SET_FS)` = 158
- `set_tid_address` = 218
- `rt_sigaction` = 13 (stub)
- `rt_sigprocmask` = 14
- `set_robust_list` = 273
- `ioctl` = 16 (`TCGETS`, `-ENOTTY` otherwise)
- `brk` = 12 (Linux absolute-address ABI)
- `mmap` = 9, `munmap` = 11, `mprotect` = 10
- `getrandom` = 318, `rseq` = 334 (stubs)
- `fork` = 57
- `execve` = 59 (in-place; spawn moved off 59 to a donix-private 507
  and later deleted at A5 step 2)
- `wait4` = 61
- `getdents64` = 217
- `stat` = 4, `fstat` = 5 (added at the A4 `ls` port)

Complete at `20260924K`, extended with `stat`/`fstat` at `20260926T`.

### A3 — musl shell

A minimal musl shell (`musl_sh`) using `fork` + `execve` + `wait4`
replaced the newlib shell as the default boot shell. Path normalization
and tokenization were added incrementally. Complete at `20260926J`.

### A4 — userland apps ported to musl

`hello`, `echo`, `cat`, `ls`, `memtest`. Each was built in parallel
alongside its newlib original first, tested individually, then cut over
in a single Makefile change. The parallel-then-cut-over pattern kept the
newlib canary green throughout. Complete at `20260926Y`.

### A5 — retire newlib

The newlib userland, build rules, and libraries were removed in eight
steps, each its own commit and canary run:

1. newlib `*-elf` targets and `user-elfs` aggregate
2. `SYS_DONIX_SPAWN` (507) and `sys_spawn`
3. donix-private directory syscalls (500–502)
4. `SYS_ARCH_SET_FS` (504)
5. `SYS_DONIX_SBRK` (505)
6. the embedded newlib shell and its fallback
7. the entire `userland/newlib/` tree
8. leftover dead code

Complete at `20260926-08`.

### What `v0.5.5` means

At `v0.5.5`, the kernel speaks Linux x86_64 syscalls, the shell is
`musl_sh`, the userland C library is musl 1.2.5 built from source into a
project-local tree, and the newlib userland has been fully retired.

**What remains in the tree after the migration:**

- `SYS_REBOOT` (503) is the only 500+ syscall number left in
  `syscall_dispatch`.
- The only userland is musl: the FAT has `HELLO.ELF`, `ECHO.ELF`,
  `CAT.ELF`, `LS.ELF`, `MEMTEST.ELF`, `MUSL_SH.ELF`, and the musl
  test binaries, all built against the project-local musl 1.2.5.
- The only boot shell is `musl_sh`, loaded from `0:/MUSL_SH.ELF`.
- `make` produces `kernel.bin` only. No recursive userland build.

The 56 working tags used during the migration (`20260922A` through
`20260926-09`) are recorded in
[`migration-tags.txt`](migration-tags.txt) with their commit SHAs. The
release tags inherited from dons-os (`v0.0.1` through `v0.5.4`) remain
as real git tags; `v0.5.5` is the donix completion tag.

### Bugs found and fixed during the migration

The A1–A5 work surfaced a batch of Linux-ABI mismatches and one
ordering bug that would not have been visible on the dons-os-only
kernel. Each is documented in [`handoff.md`](handoff.md) under
"Resolved bugs":

- **Syscall return path clobbered `%r10`** — the Linux ABI says only
  `%rax`, `%rcx`, `%r11` are clobbered by `syscall`. musl's stdio keeps
  a live pointer in `%r10` across `writev`. Fixed at `20260924L`.
- **`sys_read` on fd 0 blocked until `count`** — POSIX returns on the
  first available byte. Fixed at `20260926A`.
- **`brk` used increment semantics** instead of Linux's absolute
  address. Fixed at `20260924B`; newlib's increment-based `sbrk` moved
  to a private syscall (later deleted at A5 step 5).
- **`MSR_FS_BASE` was per-CPU, not per-process** — musl reads `%fs:0`
  on the child's first instruction after `fork`. Fixed by saving,
  restoring, and inheriting it. Fixed at `20260926E`.
- **`execve` did not pass `argc`/`argv` in `%rdi`/`%rsi`** — newlib's
  `crt0.S` reads them from registers. Fixed at `20260926G`.
- **`execve`'s argv layout zeroed `argv[1]`** for short `argv[0]`
  values. Fixed at `20260926H`.

The migration also produced a working `fork`, a working `execve`, a
working `wait4`, and a working `getdents64` — each of which the A2
phase developed against a specific musl test binary, with the newlib
canary re-run after every change.

**Design notes.** The original design plan for the dons-os-era
`SYS_EXEC` feature — spawn vs. execve, parent/child tracking, the
reap-vs-zombify decision, the kmalloc-whole-file load approach — is
preserved in git history at `ecc8ade`:

```
git show ecc8ade:SYS_EXEC_PLAN.md
```

It describes the newlib-era implementation (syscall numbers 8 and 9,
the `arc2/syscalls.c` shims) and is **not** updated for donix's Linux
ABI (`execve` = 59, `wait4` = 61).

---

## 1. Boot Chain (Completed)

### ✔ 1.1 — 16-bit Real Mode
- BIOS boot sector (`0x7C00`)
- INT 0x10 text output
- INT 0x13 disk loading
- Stage2 loader

### ✔ 1.2 — 32-bit Protected Mode
- A20 enable
- GDT setup
- CR0.PE → protected mode
- VGA text output

### ✔ 1.3 — 64-bit Long Mode
- PAE paging (PML4 → PDPT → PD → PT)
- IA32_EFER.LME set
- CR0.PG → paging enabled
- Far jump into 64-bit code
- 64-bit VGA text output

Boot chain is complete and stable.

---

## 2. Kernel Foundations (Completed)

### ✔ 2.1 — 64-bit IDT + Exceptions
- IDT structure in long mode
- ISR stubs in assembly
- Basic exception handlers (Divide, Debug)

### ✔ 2.2 — PIC Remap
- Master → 0x20
- Slave → 0x28
- IRQs mapped to vectors 32–47

### ✔ 2.3 — PIT Timer (IRQ0)
- PIT programmed to 100 Hz
- Global tick counter
- On-screen tick display

### ✔ 2.4 — Keyboard IRQ1
- IRQ1 handler
- Raw scancode reader
- Verified interrupt flow

### ✔ 2.5 — Physical Memory Manager (PMM)
- Parse E820 map
- Page allocator (bitmap-based)
- Page allocation test
- Supports up to 512 MiB

### ✔ 2.6 — VGA Console with Scrolling
- Correct scrolling behavior
- Hardware cursor control
- Cursor shape control (block/underline)
- Clean boot screen with system info

### ✔ 2.7 — Command Shell
- Command parser
- Built-in commands: help, clear, info, mem, version, reboot, pmmtest,
  test, vmmtest, serialtest, heapstat, maptest, testrec, heaptest,
  heapcheck, heapstress, nxtest, syscall, **elfload**, **proclist**,
  **proccreate**, **vmmclone**, **runproc**, **schstat**, **testyield**,
  **gdtdump**, **tssdump**, **atatest**, **fatmount**, **fatls**,
  **fatcat**, **selftest**
- Command history with backspace
- Interactive prompt `>`

---

## 3. Core Kernel Features (Completed)

### ✔ 3.1 — Higher-Half Kernel
- Map kernel to `0xFFFFFFFF80100000`
- Updated linker script
- Clean identity map and higher-half mapping working

### ✔ 3.2 — Exception Handlers
- Page Fault handler (dump CR2, error code, RIP)
- Divide by Zero handler
- General Protection Fault handler (ERR, RIP, CS)
- Double Fault handler registered
- Test command (`test`) for triggering all exceptions

### ✔ 3.3 — Virtual Memory Manager (VMM)
- CR3/PML4 read and displayed at boot
- HHDM_START defined and printed
- vmmtest command implemented
- Serial debug output (COM1)
- **Recursive paging** at PML4[510]
- PML4 read/write from higher-half kernel
- Dynamic page table allocation (PDPT, PD, PT)
- HHDM mapping (PML4[256])
- User-space page mapping with PT_USER flag
- **NX (No Execute) bit support** via PT_NX flag
- `nxtest` command for verifying NX functionality
- **Dynamic HHDM mapping** via `ensure_hhdm_mapped()`
- **Deep page table cloning** via `vmm_clone_page_table()` (v0.5.3)
- **`vmmclone` command** for testing process isolation

### ✔ 3.4 — Kernel Heap Allocator ⭐ v0.5.2
- `kmalloc()`/`kfree()` with free list
- Automatic heap expansion
- 1 MB initial heap, expanding up to 24 MB
- `heapstat` debugging command
- `heaptest` allocation/reuse verification
- **v0.5.2 rewrite:** `heap_extend` places new blocks at the start of
  the newly mapped region. The old version placed the new block at
  `heap_brk - requested_size`, which lost up to `PAGE_SIZE - 1` bytes
  per extension and put blocks at the wrong end of the mapped range.
- **`heap_header_t` padded to 48 bytes** so payloads are 16-aligned
  when the allocation size is a multiple of `HEAP_ALIGNMENT`.
- **`heap_validate()`** walks the block list and checks every
  invariant, including that the last block's end equals `heap_brk`.
- **`heap_stress()`** allocates across 512 slots with per-block
  patterns, forcing multiple extensions. The run grows the heap from
  1 MB to ~4.15 MB with zero leak.
- **`heapcheck` and `heapstress` shell commands.** Both also run from
  `selftest`.

### ✔ 3.5 — User Mode (Ring 3)
- GDT with user segments (DPL=3)
- User code (0x33) and user data (0x2B) segments
- TSS initialization for stack switching
- `iretq` transition from kernel to user mode
- User memory mapped with PT_USER flag
- `create_user_process()` for launching user code

### ✔ 3.6 — NX Bit Support
- NX bit enabled via PT_NX flag
- NX flag handling in `vmm_map_page()`
- `nxtest` command for verifying NX functionality
- WRITE bit fix for heap pages
- Keyboard buffer corruption resolved
- `EFER.NXE` enabled in v0.5.1 so the CPU enforces the bit

### ✔ 3.7 — System Calls
- `syscall` instruction setup via MSRs (IA32_STAR, IA32_LSTAR,
  IA32_FMASK)
- System call handler with register preservation
- SYS_WRITE (syscall #1)
- SYS_EXIT (syscall #2)
- SYS_READ (syscall #3)
- SYS_OPEN (syscall #4)
- SYS_CLOSE (syscall #6)
- SYS_UNLINK (syscall #7)
- **SYS_EXEC (syscall #8)** — see §3.26
- **SYS_WAITPID (syscall #9)** — see §3.26
- SYS_BRK (syscall #10)
- **SYS_OPENDIR (syscall #12)** — see §3.27
- **SYS_READDIR (syscall #13)** — see §3.27
- **SYS_CLOSEDIR (syscall #14)** — see §3.27
- SYS_REBOOT (syscall #25)
- Syscall dispatcher with x86_64 ABI
- `syscall` test command
- SYSRET returns to user mode
- **Safe user-space memory access** via `safe_copy_from_user()` and
  `safe_copy_to_user()` using HHDM
- **`safe_copy_to_user_cr3()`** for writing into a different process's
  address space (v0.5.4)

**Note for donix:** §3.7 describes the dons-os syscall table. In donix,
all syscalls except `SYS_REBOOT` (503) have been renumbered to Linux
x86_64 numbers; see section 0 above.

### ✔ 3.8 — ELF Loader ⭐ FINALIZED
- Parses ELF64 headers and program headers
- Maps LOAD segments with correct permissions (Read, Write, Execute,
  User)
- Allocates and maps user stack pages
- Transitions to user mode via IRETQ (CS=0x33, SS=0x2B)
- Sets IOPL=3 for user I/O access
- Page table execute permissions at all levels
- **`elfload` command** for running embedded ELF programs
- **Works reliably on first boot** (handles bootloader
  identity-mapping conflict)
- **Safe HHDM-based copying** of program segments and stack
- Tested with "Hello from Userland!" via serial

### ✔ 3.9 — Process Foundation ⭐ COMPLETE
- Process Control Block (PCB) structure
- Process creation (`process_create`)
- Process listing (`proclist`)
- Page table cloning (`vmm_clone_page_table`)
- **`vmmclone` command** for testing page table isolation
- Ready queue infrastructure (foundation for scheduler)

### ✔ 3.10 — Process Stack Setup
- Static kernel stack pool for processes
- Process creation with dedicated user and kernel stacks
- Process execution via direct function call (kernel mode)
- Process cleanup with `process_destroy()` (frees user stack, marks PCB
  unused)
- **`runproc` command** to create and execute a test process
- Shell returns properly after process execution
- All previous features remain fully functional

### ✔ 3.11 — Cooperative Scheduler
- Ready queue with round-robin scheduling
- `process_yield()` for voluntary context switching
- `process_exit()` for clean process termination
- Assembly-level context switching (`context_switch.asm`)
- **`testyield` command** for testing cooperative scheduling
- **`schstat` command** for scheduler statistics
- `runproc` now uses the scheduler
- All previous features remain fully functional

### ✔ 3.12 — Preemptive Scheduler
- PIT timer (IRQ0, 100 Hz) preempts user-mode and kernel-mode
  processes
- Quantum-based task slicing (`SCHED_QUANTUM = 2`)
- Timer saves the interrupted frame into the PCB and resumes via
  `irq0_stub`
- Kernel-mode preemption: a process blocked in `sys_read` does not hold
  the CPU
- **`testyield` now alternates cleanly** between both test processes

### ✔ 3.13 — Userland C Library (newlib 4.x) — *dons-os era; removed in donix*
- newlib 4.x statically linked into user programs (`libc.a`, `libm.a`)
- `crt0.S`, `syscalls.c`, `reent.c` provide startup, syscall shims, and
  reentrancy
- **`_impure_ptr = &_impure_data`** in `reent.c` — without this, the
  first `printf` faults
- `printf`, `malloc`/`free`, `memcpy`, `str*`, `setvbuf`, `errno` all
  work from Ring 3
- User programs written in ordinary C, compiled with `x86_64-elf-gcc`
- Loaded as ELF64 binaries embedded in the kernel image

**donix note:** the newlib userland was removed at A5 step 7. It is
replaced by musl 1.2.5, built from source into a project-local tree.
See section 0 above.

### ✔ 3.14 — Blocking I/O
- `sys_read` on fd 0 no longer spins the CPU
- Blocked process sets `PROC_STATE_BLOCKED`, executes `sti; hlt`
- Timer skips blocked processes (narrowed re-add condition to
  `state == RUNNING`)
- `irq1_handler` wakes all blocked processes on a key
- Timer resumes the process via its saved kernel frame
- Multiple processes can be blocked; whichever wakes first consumes the
  byte

### ✔ 3.15 — Boot-Time Shell Choice
- 2-second window at boot: `k` → kernel shell, any other key or timeout
  → user shell
- Kernel shell is a diagnostic console (not reachable after user shell
  starts)
- User shell is the default interactive console
- `sys_exit` on a user process halts the CPU; kernel diagnostics return
  to the kernel shell
- `gdtdump` and `tssdump` added as on-demand kernel shell diagnostics

### ✔ 3.16 — Process Yield Queue Fix
- `process_yield` no longer corrupts the ready queue by calling
  `scheduler_ready_queue_remove(current_process)` on an off-queue
  process
- Defensive check added to `scheduler_ready_queue_remove` for off-queue
  processes
- `testyield` demonstrably alternates between both test processes

### ✔ 3.17 — TSS.RSP0 / `g_syscall_stack_top` Sync
- `process_init` runs after `tss_init` in `kmain`
- `process_init` calls `tss_set_kernel_stack(idle->kernel_stack_top)`
  alongside `tss_set_syscall_stack`
- `tssdump` on a fresh boot shows `rsp0` and `g_syscall_stack_top` as
  the same address

### ✔ 3.18 — Process Cleanup on Exit
- `process_reclaim(pcb_t*)` in `process.c`, called from `process_exit`
- Frees ELF segment pages and user stack pages via
  `process_cleanup_elf_pages`
- Sets PCB `PROC_STATE_UNUSED`, resets `pid = 0`, decrements
  `process_count`
- PCB slot can be reused by a future `process_create`; repeated
  `testyield` and `runproc` no longer exhaust the 32-slot pool
- `process_exit` now disables interrupts for the entire critical
  section (state transition + queue removal + reclaim + context
  switch), closing a timer race where the timer could re-add the
  exiting process to the ready queue or save the current stack frame
  into `next->rsp`
- Page-table teardown deferred (a few pages per process)

### ✔ 3.19 — Scheduler, TSS, and Interrupt ABI Stability Pass ⭐ v0.4.9
- **ELF-load race at boot.** `process_create` queues the PCB before the
  ELF is loaded, so a PIT tick between create and
  `elf_load_into_process` could schedule a process whose entry page was
  not yet mapped. Fixed at the three call sites (`kmain` boot path,
  `elfload`, `usershell`): remove from queue, load ELF, set
  `entry_point`, add back.
- **TSS.RSP0 / `g_syscall_stack_top` lockstep.** The update was gated
  on `entry_point < KERNEL_BASE`; kernel-mode switches left
  `g_syscall_stack_top` stale. Gate removed in `scheduler_switch_to`,
  `process_exit`, `timer_preempt_handler`.
- **`process_exit` fallback TSS restoration.** The no-runnable
  fallback zeroed only `g_syscall_stack_top`, leaving `TSS.RSP0`
  pointing at a dead process's stack. Now restores both to idle's
  kernel stack top.
- **Kernel stack slot aliasing.** Kernel stack slots were computed as
  `pid % MAX_PROCESSES`. `next_pid` is monotonic and never decremented
  on teardown, so after 32+ creations the modulo wrapped and a new
  process aliased a live one's slot. Replaced with `slot_owner[]` and a
  `pcb_t::kernel_stack_slot` field.
- **`context_switch.asm` offsets.** Inserting `kernel_stack_slot` at
  `pcb_t` offset `0x58` shifted later fields by 8 bytes; the asm
  hardcoded the old offsets. Every offset updated by +8, and a
  `_Static_assert` block in `process.c` now pins the offsets the asm
  depends on.
- **`scheduler_switch_to` preemption window.** The function set
  `current_process = next` with interrupts enabled, then called
  `context_switch(prev, next)`. A PIT tick in that window saved the
  wrong stack frame into `next`. Fixed with `cli` at the top of the
  function.
- **`irq1_stub` ABI violation.** `irq1_handler` was called without
  preserving caller-saved registers. A keyboard interrupt between `sti`
  and a subsequent indirect `jmp *%rax` corrupted `%rax` and landed
  mid-instruction inside `kmain_shell_loop`. Fixed on two fronts: all
  stubs that call C handlers now preserve all 15 GPRs, and the fallback
  uses a direct `jmp kmain_shell_loop`.
- **`process_dump_all` cosmetic fix.** Detached PCBs show `DETACHED`
  instead of `READY`.
- **Frame validation in `context_switch.asm`.** `.kernel_task`
  validates the resume frame's `rip` and `cs` before popping, and emits
  a serial marker byte on failure.

### ✔ 3.20 — ATA PIO Block Device Driver
- Primary-channel ATA in PIO (polled) mode, LBA28
- `ata_init()` probes master and slave, IDENTIFY, model strings
- Per-drive read/write entry points (`ata_read_sectors_drive`,
  `ata_write_sectors_drive`, etc.)
- `ata_flush_cache_drive()` for durability
- Write-protect floor on the master: refuses below
  `ATA_WRITE_PROTECT_LBAS` (2048)
- `atatest` kernel shell diagnostic: MBR signature, kernel header
  bytes, model strings
- Three bring-up bugs found and fixed: PIC mask restoration, exception
  frame offsets, LBA mode bit
- Files: `04_kernel_64bit/ata.c`, `04_kernel_64bit/include/ata.h`,
  `04_kernel_64bit/include/io.h`

### ✔ 3.21 — FatFs Integration (Read/Write) ⭐ v0.5.2
- FatFs R0.16 vendored in `04_kernel_64bit/fatfs/`
- `diskio.c` shim maps FatFs `disk_read` / `disk_write` / `disk_ioctl`
  onto the ATA driver
- `FF_FS_READONLY = 0`, `FF_FS_MINIMIZE = 0`, `FF_MULTI_PARTITION = 0`
- `FF_VOLUMES = 1`, `FF_MIN_SS = FF_MAX_SS = 512`
- **v0.5.2: Long filename support.** `FF_USE_LFN = 2` (dynamic working
  buffer on the stack), `FF_LFN_UNICODE = 2` (UTF-8 on the API),
  `FF_CODE_PAGE = 437` (US). `fatfs/ffunicode.o` linked into the
  kernel. `FF_MAX_LFN = 255`, `FF_LFN_BUF = 255`, `FF_SFN_BUF = 12`.
- **v0.5.2: `ff.o` and `ffunicode.o` compiled with the cross-GCC**,
  not Clang. See the cross-GCC note below and
  `LLD_BUG_REPORT.md`.
- Kernel shell commands: `fatmount`, `fatls`, `fatcat <file>`
- Dual-drive storage: kernel on master `hdd.img`, FAT16 volume on slave
  `fat.img`
- `ata.c` drive-parameterized API so FatFs can target the slave
  independently of the boot chain
- Kernel size grew by ~25–30 KB for FatFs, plus ~5 KB for the Unicode
  tables; kernel-size tripwire added to the Makefile
- **`fatls` shows `HELLO-WORLD.TXT`; `fatcat HELLO-WORLD.TXT` reads
  it.**

### ✔ 3.22 — Userland File I/O over FatFs ⭐ v0.5.2
- Per-process file table in the PCB: slots 0, 1, 2 reserved for
  stdin/stdout/stderr; slots 3..7 for `open`/`close`
- Syscalls wired: `SYS_OPEN` (4), `SYS_CLOSE` (6), `SYS_UNLINK` (7),
  `SYS_GETPID` (20), and an fd branch in `SYS_READ` (3)
- `sys_exit` closes any files still open
- Userland `arc2/syscalls.c` exposes `open`, `close`, `read`, `write`,
  `unlink` shims over the syscall instruction
- `sys_open` flag handling uses newlib BSD-style values
  (`O_CREAT = 0x200`, `O_TRUNC = 0x400`, etc.) with access mode in the
  low 2 bits
- Verified end-to-end: create, write, close, reopen, read, byte-exact
  round trip from Ring 3
- A latent bug was fixed along the way: `sys_open` only set `FA_WRITE`
  when `flags == 1`, so `O_WRONLY | O_CREAT | O_TRUNC` (0x601) never
  enabled write mode
- **v0.5.2: `sys_open` path buffer enlarged to 300 bytes**, with a
  `copy_user_string` helper that respects the buffer size. New
  `USER_PATH_MAX` constant (300). Paths up to `FF_MAX_LFN` (255) now
  fit.
- **v0.5.2: `test_multi_file` in the user shell** now deletes the files
  it creates and confirms they are gone.

### ✔ 3.23 — Single-Drive Layout ⭐ v0.5.0
- Boot chain, kernel, and FAT16 partition on one `hdd.img`
- Partition starts at LBA 2048; kernel region is LBA 128..2047 (up to
  960 KB)
- `FAT_CONFIG=dual|single` at build time selects the layout
- `include/fat_config.h` derives `FAT_DRIVE` (`ATA_DRIVE_MASTER` in
  single, `ATA_DRIVE_SLAVE` in dual) and `FAT_VOLUME_SECTORS` from
  `FAT_CONFIG_SINGLE_DRIVE`
- `diskio.c` translates volume-relative sector numbers into device LBAs
  via `FAT_PARTITION_OFFSET` (2048 in single, 0 in dual)
- **Key correction:** with `FF_MULTI_PARTITION = 0`, FatFs is not
  partition-aware. It treats the FAT volume as starting at LBA 0 of the
  physical drive and does not consult the partition table or the BPB
  `hidden_sectors` field. The offset therefore must live in
  `diskio.c`. An earlier iteration assumed FatFs would read
  `hidden_sectors` and skipped the offset; the mount failed with
  `FR_NO_FILESYSTEM` (13)
- `atatest` write leg removed: LBA 1024 is inside the kernel region in
  single-drive mode. Read-only checks remain
- `kmain.c` prints a config-specific boot-time storage line to VGA and
  serial
- `hdd-single.img` built by `mkfs.vfat -F 16 -h 2048 --offset=2048` and
  populated by `mcopy` from `test-files/`
- Image-build rule verifies the FAT boot sector and the BPB
  `hidden_sectors` field after assembly
- Kernel-size tripwire now checks two limits: the stage2 staging ceiling
  (448 KB) and the disk-layout ceiling (983 KB, the region below LBA
  2048)
- User shell gains options 5 (persistence), 6 (multi-file), 7 (4 KB
  round-trip). `[FS TEST]` fixed: `msg_len` was hardcoded to 19 but the
  string is 20 bytes

### ✔ 3.24 — Maintenance Batch: Self-Test, NX on Hardware, Kernel-Owned GDT ⭐ v0.5.1
- **Self-test infrastructure (`selftest` command).** 15 tests covering
  GDT descriptor contents, TSS fields, PMM allocation and freeing, VMM
  control registers, page mapping, recursive paging, heap integrity,
  the NX bit in the final PTE, syscall entry point, ATA reads, FatFs
  mount and directory listing, and the three exception handlers (#DE,
  #PF, #GP). The exception tests use an expected-fault protocol: a
  small kernel-mode child takes the fault, the handler records the
  vector, terminates the child, and resumes the kernel shell. First
  time the exception handlers have been exercised by anything.
  (`interrupts.c`, `interrupts.h`, `kmain.c`.)
- **EFER.NXE enabled.** Previously the kernel wrote NX bits into PTEs
  but the CPU ignored them because `EFER.NXE` was clear. Worked under
  KVM by accident (KVM's shadow MMU enables NX on the host side
  regardless of the guest's EFER); would have faulted on bare metal.
  `enable_nx()` in `kmain.c` now sets the bit, guarded by a CPUID
  check. `test_vmm` asserts on it. (`kmain.c`.)
- **Kernel-owned GDT.** The GDT lived in low memory (base `0x101DC`,
  inside `stage2.asm`'s loaded image), read through the bootloader's
  identity map. `gdt_init` now builds `kernel_gdt[16]` in `.bss` at a
  higher-half address, `lgdt`s it, and reloads the segment registers.
  `gdt_set_tss` writes the TSS descriptor directly.
  `gdt_fix_user_segments` became a no-op. `test_gdt`'s strict
  assertion is restored. (`gdt.c`, `gdt.h`, `kmain.c`.)
- **Print atomicity.** Kernel serial and VGA drivers now share a single
  print lock (cli/sti critical section with a nesting counter and
  RFLAGS save/restore). Multi-part boot messages wrap in
  `serial_lock`/`serial_unlock`; ATA's read-path prints are gated
  behind `ATA_DEBUG`. The DonsDOS banner truncation and interleaved
  boot trace are gone. (`serial.c`, `vga.c`, `ata.c`.)
- **#DF through IST1.** A dedicated 4 KB stack and an IST entry on the
  `#DF` gate turn a double fault into a printed diagnostic instead of a
  silent triple-fault reset. (`idt.c`, `tss.c`.)
- **Kernel shell is process-backed.** It runs as a real process with a
  pool-allocated kernel stack, not a hardcoded address
  (`0xFFFFFFFF8008FF00` is gone). (`kmain.c`, `process.c`.)
- **Staging ceiling raised** from 176 KB to 448 KB by adding PASS 4–7
  in `stage2.asm`.
- **Reboot flushes file handles.** The user shell's reboot path flushes
  open file handles before the hardware reset; option 9 added.
  (`user_shell.c`, `user_syscall.c`.)
- **MAINTENANCE.md.** No open findings remain. Sections 5b (boot-time
  self-test mode) and 5c (make test) deferred with the conditions that
  would make them worth revisiting.

**Key learnings:**
- A self-test suite is the right investment: it found two real latent
  bugs (EFER.NXE off, GDT in low memory) that no one was looking for.
- "Works under KVM" is not the same as "works on hardware." KVM's
  shadow MMU is more forgiving than the architecture requires. When a
  feature depends on a CPU feature flag, enable the flag explicitly
  rather than relying on the hypervisor to enable it for you.
- Low-memory structures are fragile. The GDT was the second such
  structure (after the boot stack in item 3a) to be relocated to the
  higher half. Anything the bootloader left in low memory is a
  candidate for the same treatment.
- Print atomicity is not just cosmetic. Interleaved serial output
  obscures real diagnostics. A single shared lock is enough until
  per-user ttys exist.

### ✔ 3.25 — Heap Rewrite, LFN, SYS_UNLINK, Cross-GCC for FatFs, Makefile Dependency Tracking ⭐ v0.5.2
- **heap.c rewritten.** `heap_extend` now returns the start of the
  newly mapped region and the caller places a single free block
  covering the entire region. The old version placed the new block at
  `heap_brk - requested_size`, which lost up to `PAGE_SIZE - 1` bytes
  per extension and put blocks at the wrong end of the mapped range.
  Blocks are maintained in address order and coalesced on free.
- **`heap_header_t` padded to 48 bytes.** Payloads are 16-aligned when
  the allocation size is a multiple of `HEAP_ALIGNMENT`.
- **`heap_validate()` and `heap_stress()`.** `heap_validate` checks
  every block-list invariant. `heap_stress` runs 4096 ops across 512
  slots with per-block patterns, grows the heap to ~4.15 MB, and
  validates intact with zero leak.
- **`heapcheck` and `heapstress` shell commands.** Both added to
  `selftest`. Test count 17/17.
- **Long filename support.** `FF_USE_LFN = 2`, `FF_LFN_UNICODE = 2`,
  `FF_CODE_PAGE = 437`. `fatfs/ffunicode.o` linked. Kernel grows
  158 KB → 163 KB. `fatls` shows `HELLO-WORLD.TXT`;
  `fatcat HELLO-WORLD.TXT` reads it.
- **`SYS_UNLINK` (syscall #7).** Wraps FatFs `f_unlink`. Kernel handler
  `sys_unlink` in `user_syscall.c`; userland `unlink()` shim in
  `arc2/syscalls.c`. `test_multi_file` in the user shell now deletes
  the files it creates and verifies they are gone.
- **`sys_open` path truncation fix.** New `copy_user_string` helper;
  `USER_PATH_MAX = 300`.
- **Cross-GCC for FatFs.** `fatfs/ff.o` and `fatfs/ffunicode.o`
  compiled with `/opt/cross/bin/x86_64-elf-gcc` at `-O2`. Clang 22.1.8
  miscompiles `ff.c` at every optimization level: `-O0` breaks the
  FILINFO read path (filenames decode as `@80(`, `f_opendir` returns
  `FR_INT_ERR`), `-O1` hangs in `f_unlink`, and `-O2` produces
  LLD-truncated instructions in the linked binary. GCC produces correct
  code and links cleanly with the Clang-built kernel objects. See
  `LLD_BUG_REPORT.md`.
- **Makefile header dependency tracking.** `-MMD -MP` added to
  `CFLAGS`; `-include $(OBJS:.o=.d)` at the bottom. Header changes now
  trigger the right rebuilds automatically.

**Key learnings:**
- A heap allocator without a validator is a heap allocator you cannot
  trust. The `heap_validate` invariant that the last block's end must
  equal `heap_brk` is what would have caught the old extension bug
  immediately.
- The `heap_stress` test must be sized to actually exceed the initial
  heap. The first version allocated only ~256 KB and never forced an
  extension; the size constants were wrong.
- A link-time truncation bug is much harder to diagnose than a compiler
  bug. The two look identical in a disassembly of the linked binary;
  they are distinguished by compiling the same source standalone and
  comparing.
- A Makefile without header dependency tracking turns every header
  change into a two-step rebuild. `-MMD -MP` fixes this permanently.
- Three separate toolchain bugs in Clang 22.1.8 and LLD 22.1.8 affected
  `ff.c` — one at each optimization level. All three are avoided by
  compiling that file with GCC instead. The per-file compiler override
  is the right shape for a toolchain bug that only affects one file.
- GCC and Clang emit ELF64 relocatable objects using the same x86-64
  System V ABI for the `x86_64-unknown-elf` target. There is no
  linker-level distinction between a GCC `.o` and a Clang `.o`, so a
  per-file compiler override is transparent to the rest of the build.

### ✔ 3.26 — `SYS_EXEC` Complete: Disk-Loaded ELF User Programs ⭐ v0.5.3
- **`SYS_EXEC` (syscall #8).** Opens an ELF file on the FAT volume,
  loads it into a new process, and returns the child's pid.
  `spawn("0:/HELLO.ELF")` from the user shell loads `HELLO.ELF` from
  disk at runtime; the child runs as an independent Ring 3 process and
  prints its banner.
- **`SYS_WAITPID` (syscall #9).** Blocks the calling process until a
  matching child exits, then reaps it and returns its exit status.
  `WNOHANG` supported (returns 0 immediately if no matching child has
  exited). `pid > 0` waits for a specific child; `pid == -1` waits for
  any child.
- **`spawn()` and `waitpid()` shims** in `arc2/syscalls.c`, and
  `apps/include/donsdos.h`. Declared for user programs. `WNOHANG`
  defined as 1.
- **Process parent/child tracking.** New `pcb_t` fields `parent_pid`,
  `exit_status`, `wait_pid`, and a `PROC_STATE_ZOMBIE` state. A process
  with a parent becomes a zombie on exit and is reaped by
  `sys_waitpid`; a process with no parent reclaims itself immediately,
  preserving the pre-v0.5.3 behavior for idle and the shells.
  `process_wake_parent_if_waiting` in `process.c` walks the PCB pool
  for a parent in `PROC_STATE_BLOCKED` with
  `block_kind == BLOCK_KIND_WAITPID` and a matching `wait_pid`, sets it
  to `PROC_STATE_READY`, and adds it to the ready queue.
- **Three latent memory-safety bugs found and fixed during bring-up.**
  Each was independently sufficient to make `SYS_EXEC` flaky, and each
  is documented in `MAINTENANCE.md` §3j with the specific symptom it
  caused:
  1. **PMM allocator reentrancy.** `pmm_alloc_page` did a non-atomic
     read-modify-write on the shared bitmap; a timer IRQ between
     `bitmap_test` and `bitmap_set` could hand the same page out twice.
     Fixed with cli/sti critical sections in `pmm_alloc_page`,
     `pmm_free_page`, `pmm_reserve_page`, and `pmm_unreserve_page`,
     using `pushfq`/`pop` to preserve the caller's IF.
  2. **Page-table aliasing in `vmm_clone_page_table`.** The clone was a
     shallow copy of the PML4: parent and child shared every PDPT, PD,
     and PT. Any `vmm_map_page_in_cr3` on the child therefore
     overwrote the *parent's* PTEs, silently replacing the shell's
     code, `.rodata`, and user stack with the child's. Fixed with a
     deep copy of the low-half page-table hierarchy (PML4[0..255]);
     the high half (HHDM and kernel) remains shared by design.
  3. **`context_switch` resumed processes by `entry_point`.** The
     resume side chose user vs kernel by comparing `next->entry_point`
     against `KERNEL_BASE`, which is correct for the first dispatch of
     a fresh process but wrong for a process that had been preempted or
     blocked in kernel mode. The old code took the user branch and
     *rebuilt* the frame from `entry_point` and `user_stack_top`,
     restarting the process from `_start`. This is what produced "shell
     restarts after `waitpid` returns." Fixed by making the resume side
     trust the saved frame verbatim and choose the validation rule from
     the frame's own CS.
- **`HELLO.ELF` built and copied into the FAT partition** at
  image-build time by `05_boot_kernel64/Makefile`. The `hello.c` user
  program is 20 KB.
- **User shell option A.** Loads `HELLO.ELF` from disk, runs it as an
  independent Ring 3 process, blocks in `waitpid`, and verifies the
  exit status is 0.
- All prior features remain functional. `selftest` still passes 17/17;
  user shell options 1–9 still work.

**Key learnings:**
- Three memory-safety bugs, each individually fatal to the feature, can
  coexist and mask one another. The path-string corruption, the
  user-mode `#PF` at RIP=0/1, and the shell restart were all different
  symptoms of the same three bugs interacting.
- The PMM's test-and-set was only a problem because the timer preempts
  kernel-mode processes. In a single-threaded kernel without
  preemption, the same code is correct. Enabling preemption is what
  turns a "safe in practice" allocator into a latent double-allocator.
- A shallow page-table clone is not a copy. It shares every level below
  the root. Any code that assumes "the clone's mappings are private" is
  wrong.
- Resuming a context-switched process from `entry_point` is only
  correct for the very first dispatch. After that, the saved frame is
  the truth.
- The three fixes were all localized (a few lines each) but the
  *symptoms* were not. The bug you see is not the bug you're looking
  for.

**Design notes:** the original `SYS_EXEC_PLAN.md` that guided this
implementation is preserved in git history at `ecc8ade`
(`git show ecc8ade:SYS_EXEC_PLAN.md`). It describes the dons-os-era
implementation — syscall 8/9, newlib userland, the `arc2/syscalls.c`
shims — and is **not** updated for donix's Linux ABI.

**Not yet implemented at v0.5.3:**
- Boot-time self-test mode and `make test` (deferred; see
  `MAINTENANCE.md` §5b–5c)
- Spawn regression test (see `MAINTENANCE.md` §5d)
- ELF loader `PT_NX` follow-up
- Page-table teardown on process exit
- Per-process tty / console focus
- A real shell (spawn by name, `argv`, external commands)

### ✔ 3.27 — The REPL Shell: External Programs by Name, `argv`, `ls`, `cat`, `echo` ⭐ v0.5.4 — *dons-os era*
- **The user shell is now a REPL.** It reads a whole line from stdin
  (with echo and backspace), tokenizes on whitespace, resolves the
  first token to `0:/NAME.ELF`, spawns it via `SYS_EXEC`, waits for it
  via `SYS_WAITPID`, and loops. The old fixed-menu driver is gone; the
  shell no longer contains any test code.
- **Built-ins:** `exit`, `help`, `reboot`. Everything else is an
  external program on the FAT volume.
- **Stage 1 — REPL, no arguments.** `user_shell.c` rewritten (~150
  lines): a `readline()` built on `read(0, &c, 1)` with backspace and
  echo, a whitespace tokenizer that produces a full `argv[]` from day
  one, a path resolver (`memtest` → `0:/MEMTEST.ELF`), and a
  spawn+waitpid helper. First external program: `memtest.elf`, a direct
  port of the old shell's menu option 3.
- **Stage 2 — regression tests as external programs.** `fstest.elf`
  (old option 4, with an argv-gated `--verify` mode for old option 5),
  `multitest.elf` (old option 6), `bigtest.elf` (old option 7). Same
  pattern as Stage 1, one file per test. The shell now contains no test
  code at all.
- **Stage 3 — directory listing.** Three new syscalls:
  - `SYS_OPENDIR` (#12), `SYS_READDIR` (#13), `SYS_CLOSEDIR` (#14),
    wrapping FatFs `f_opendir`/`f_readdir`/`f_closedir`.
  - The per-process `file_table[]` now holds tagged `file_slot_t*`
    entries (`FILE_KIND_FILE` or `FILE_KIND_DIR`) so files and
    directories share the same handle space. `close_all_files`,
    `sys_open`, `sys_close`, `sys_read`, and `sys_write` all dispatch
    on the tag.
  - `struct dons_dirent` (mirrored between `user_syscall.c` and
    `apps/include/donsdos.h`): `char name[256]`, `uint64_t size`,
    `uint8_t attrib`. The kernel copies `FILINFO.fsize` and
    `FILINFO.fattrib` verbatim and does not filter `.` or `..`.
  - `ls.elf`, the first real directory-listing userland program,
    matches the kernel shell's `fatls` output shape.
  - Dispatcher bug fixed: `SYS_ARCH_SET_FS` is defined as 11 in
    `syscall.h`, but the dispatcher routed it under `case 5`. Nothing
    in the tree called it, so the discrepancy was latent; corrected in
    this release.
- **Stage 4 — `argv` passing.** The kernel now passes `argc` and
  `argv` to a spawned child:
  - `SYS_EXEC` extended from `(path)` to `(path, argc, argv)`.
  - `sys_exec` copies each `argv` string and the pointer array onto
    the child's user stack. It uses a new
    `safe_copy_to_user_cr3(uint64_t cr3, ...)` helper, because the
    standard `safe_copy_to_user` resolves against the *caller's*
    `cr3` — the child's `cr3` is different, and switching `cr3` around
    the write was not enough (the helper still read `current->cr3`).
  - The argv region is 4 KB reserved at the top of the child's user
    stack. Strings sit at the **bottom** of the region, growing up; the
    pointer array sits just below them; the top of the region is a
    **free gap** between argv and the child's own downward-growing
    stack frames. (The first attempt placed strings at the top of the
    region, growing down, which the child's own stack clobbered before
    `main` was reached — that is `MAINTENANCE.md` §3l.)
  - `rdi` (frame slot 9) = `argc`, `rsi` (frame slot 10) = array base;
    both written into the child's 20-slot resume frame before the
    child is queued.
  - `crt0.S` stashes `rdi`/`rsi` in a `.data` slot at the very top of
    `_start` (before the BSS-clear loop, which clobbers them via
    `rep stosb`), and reads them back just before `call main`. This is
    a divergence from SysV (recorded in `MAINTENANCE.md` §3m): the
    initial `rsp` is not SysV-compliant, and no prebuilt binary that
    expects the stack-based convention will work.
- **`spawn()` signature changed** from `spawn(path)` to
  `spawn(path, argc, argv)`. The shell passes the full token list.
- **`cat.elf`** — prints one file's contents. Uses `argv[1]`.
  `cat HELLO-WORLD.TXT` prints the file byte-exact.
- **`echo.elf`** — prints `argv[1..argc-1]` separated by spaces.
  `echo hello world` prints `hello world`.
- **Test suite passes end-to-end on a fresh boot:**
  - `help`, `echo hello world`, `echo one two three`, `echo` (blank
    line)
  - `cat HELLO-WORLD.TXT` (four lines, byte-exact)
  - `ls` (9 files + `HELLO-WORLD.TXT`, correct sizes)
  - `memtest`, `fstest`, `multitest`, `bigtest` — all PASS
  - `nope` — command not found
  - `exit` — clean halt
- **Build system.** `apps/cat.elf` and `apps/echo.elf` added to the
  userland Makefile; `CAT.ELF` and `ECHO.ELF` copied onto the FAT image
  by `05_boot_kernel64/Makefile`. The `mcopy` step was refactored into
  an `mcopy_one` shell function inside the recipe, since five inline
  copies was unreadable.

**donix note:** the newlib REPL shell was removed at A5 step 6 and the
`userland/newlib/` tree at A5 step 7. In donix, the shell is `musl_sh`
(musl-linked, `fork` + `execve` + `wait4`); it normalizes `argv[0]` to
`0:/NAME.ELF` and passes `argv[1..n]` verbatim, matching the dons-os
convention.

**Key learnings:**
- **The `_cr3`-suffixed helper is not optional when writing to another
  process.** `safe_copy_to_user` silently assumes "current process is
  the target." `sys_exec` is one of the few call sites where that
  assumption is wrong, and the failure mode was misleading: the write
  *succeeded* — into the wrong address space — because both parent and
  child happen to use the same user-stack virtual addresses.
- **Where data goes on a downward-growing stack matters.** Placing argv
  at the top of the reserved region was wrong; the child's own function
  calls overwrite it before `main` runs. Placing it at the bottom, with
  a free gap above, is correct — and is what Linux does.
- **`crt0.S` has to save the kernel-provided registers before the
  BSS-clear loop.** `rep stosb` clobbers `rdi` and `rsi`. A `.data`
  slot survives the BSS clear; a register does not.
- **Instrumented builds are worth the trouble.** The bug that took the
  longest to find (`safe_copy_to_user_cr3`) was diagnosed by adding
  per-step logging to `sys_open`/`sys_write`/`sys_close` and reading
  one boot log. The same `DEBUG_FIL` infrastructure caught nothing on
  the argv bug, but the discipline of "add logging to the exact
  suspect, rebuild, one boot" is what made both diagnosable in hours
  rather than days.
- **The `file_slot_t` refactor is more invasive than it looks.** Every
  existing `file_table[]` user had to learn about the tag. The result
  is worth it — files and directories share one handle space — but the
  change touched six call sites.

**Not yet implemented at v0.5.4:**
- `fstest --verify` cross-boot persistence test (works now that argv
  reaches the child, but has not been run in a two-boot sequence)
- Boot-time self-test mode and `make test` (see `MAINTENANCE.md`
  §5b–5c)
- Spawn regression test and argv/REPL regression tests (see
  `MAINTENANCE.md` §5d–5e)
- ELF loader `PT_NX` follow-up
- Page-table teardown on process exit
- Per-process tty / console focus
- Kernel log routing to serial only (see `MAINTENANCE.md` §4i)

---

## 4. User-Facing Features

### ✔ 4.1 — Permanent Storage Layer ⭐ v0.5.4
- ~~ATA PIO block device driver (read/write sectors from long mode)~~ ✅
- ~~FatFs integration (FAT12/FAT16/FAT32)~~ ✅
- ~~Mount a filesystem, `f_open` / `f_read` / `f_write` / `f_close`~~ ✅
- ~~Kernel shell commands: `fatmount`, `fatls`, `fatcat`~~ ✅
- ~~Userland file I/O via `SYS_OPEN` (4), `SYS_CLOSE` (6), `SYS_UNLINK`
  (7), fd branch in `SYS_READ` (3)~~ ✅
- ~~Single-drive layout with FAT16 partition at LBA 2048~~ ✅
- ~~Persistence across reboot verified~~ ✅
- ~~Long filename support (`FF_USE_LFN = 2`)~~ ✅
- ~~Cross-GCC for FatFs (`ff.o`, `ffunicode.o`)~~ ✅
- ~~Load user programs from disk rather than embedding them — see §4.9
  (`sys_exec`)~~ ✅
- ~~`sys_exec`-style syscall to load and run an ELF from FAT at
  runtime~~ ✅
- ~~Directory iteration: `SYS_OPENDIR`/`SYS_READDIR`/`SYS_CLOSEDIR` +
  `ls.elf`~~ ✅
- ~~`argv` passing through `SYS_EXEC`, `crt0.S`, and `spawn()`~~ ✅
- ~~External programs by name: `memtest`, `fstest`, `multitest`,
  `bigtest`, `hello`, `ls`, `cat`, `echo`~~ ✅
- ☐ `cd` / relative paths (`chdir` + per-process cwd)
- ☐ Pipes and redirection (`cat file > out.txt`,
  `cat file | grep foo`)
- ☐ Environment variables (`envp`, `getenv`, `setenv`)

### ☐ 4.2 — Framebuffer Graphics
- Switch from VGA text mode
- Draw pixels, shapes, text
- Simple GUI experiments

### ☐ 4.3 — Serial Console Debug Access
- Kernel shell reachable over COM1 (input and output)
- Physically separate from the user's keyboard; cannot be triggered
  from user code
- This is the right shape for runtime kernel-shell access. The
  magic-key-combo approach was tried and abandoned: it is a security
  backdoor, and the kernel shell is not a process the scheduler can
  suspend and resume.

### ☐ 4.4 — Per-Process tty / Console Focus
- Prerequisite for multiple concurrent shells
- Route keyboard input to the focused shell instead of the current
  shared-buffer behavior

### ☐ 4.5 — File System (VFS)
- Virtual File System (VFS) layer above FatFs
- File operations (`open`, `read`, `write`, `close`)
- Path resolution, mount points

### ☐ 4.6 — Device Drivers
- Serial/COM port (working for output; input needed for the serial
  console)
- PCI enumeration
- AHCI disk driver
- PS/2 mouse

### ✔ 4.7 — Kernel Shell on a Proper Stack ⭐ v0.5.1
- ~~Move `kmain_shell_loop` off the hardcoded `0xFFFFFFFF8008FF00`
  boot stack~~ ✅
- ~~Allocate the shell's stack from the kernel stack pool~~ ✅
- ~~Eliminates the `process_exit` fallback's dependency on a specific
  low-memory address being mapped~~ ✅
- ~~Companion: IST stack for `#DF`, so double faults produce printed
  diagnostics instead of triple faults~~ ✅

### ✔ 4.8 — `SYS_UNLINK` ⭐ v0.5.2
- ~~Small syscall addition: `f_unlink` behind `SYS_UNLINK` (syscall
  #7)~~ ✅
- ~~Wires `unlink()` in `arc2/syscalls.c`~~ ✅
- ~~Completes the multi-file test in the user shell (option 6's delete
  phase)~~ ✅
- `sys_open` path buffer enlarged to 300 bytes; new `copy_string`
  helper

### ✔ 4.9 — User Programs from Disk (`SYS_EXEC`) ⭐ v0.5.4
- ~~`SYS_EXEC` (syscall #8): open an ELF on the FAT volume, load it into
  a new process, return its pid~~ ✅
- ~~`SYS_WAITPID` (syscall #9): block until a child exits, reap it,
  return its status~~ ✅
- ~~`spawn()` / `waitpid()` shims in `arc2/syscalls.c`,
  `apps/include/donsdos.h`~~ ✅
- ~~Parent/child tracking in `pcb_t`: `parent_pid`, `exit_status`,
  `wait_pid`; `PROC_STATE_ZOMBIE`~~ ✅
- ~~`process_wake_parent_if_waiting` in `process.c`~~ ✅
- ~~`HELLO.ELF` copied into the FAT partition at image-build time~~ ✅
- ~~User shell option A spawns it, waits, verifies exit status~~ ✅
- ~~A real shell: reads a line, tokenizes, resolves by name, spawns
  with argv, waits~~ ✅
- ~~`argv` passing through the kernel and `crt0.S`~~ ✅
- ~~`memtest`, `fstest`, `multitest`, `bigtest`, `hello` as external
  programs on disk~~ ✅
- ~~`ls`, `cat`, `echo` as external programs on disk~~ ✅
- ☐ Pipes and redirection (shell-level fd inheritance; probably a
  `dup2`)
- ☐ `cd` / relative paths
- ☐ Environment variables

### ☐ 4.10 — ELF Loader `PT_NX` Follow-up
- With `EFER.NXE` now enabled (v0.5.1), `elf_load_into_process` can
  mark non-executable segments (data, BSS, user stack) with `PT_NX`.
- Currently every segment is mapped with the same flags. About an hour
  of work.
- Closes the follow-up noted in `MAINTENANCE.md` §3g.

### ☐ 4.11 — Page-Table Teardown on Process Exit
- Walk the process's page tables in `process_reclaim` and free the
  user-space portion.
- The v0.5.3 deep `vmm_clone_page_table` makes each process own more
  page-table pages than before, so this is now more valuable than it
  was at v0.4.8.
- See `MAINTENANCE.md` §3c.

### ☐ 4.12 — Boot-Time Self-Test Mode and `make test`
- A build flag (`-DSELFTEST`) that runs the same 17 tests at boot and
  halts.
- A `make test` target that boots QEMU headless, runs the self-test,
  and reports pass/fail based on the serial output.
- See `MAINTENANCE.md` §5b–5c.

### ☐ 4.13 — Spawn and argv Regression Tests
- A kernel-mode self-test child that opens `0:/HELLO.ELF`, spawns it,
  waits for it, and asserts exit status 0. Makes the three v0.5.3
  fixes regression-detectable.
- Scripted REPL tests: `echo hello world`, `cat HELLO-WORLD.TXT`,
  `fstest --verify` after a reboot. Makes the v0.5.4 argv path
  regression-detectable.
- See `MAINTENANCE.md` §5d–5e.

### ☐ 4.14 — Kernel Log Routing
- `sys_exec`'s "spawned pid=N" line and `sys_open`'s failure line go
  to the console that userland also uses, and they interleave with
  user program output. Cosmetic, not a correctness bug.
- Fix as part of the ring-buffer console design (`MAINTENANCE.md`
  §4e), or add a `SYS_KLOG(level)` syscall that lets userland silence
  kernel console logs.
- See `MAINTENANCE.md` §4i.

### ☐ 4.15 — `fstest --verify` Cross-Boot Test
- `fstest.elf` already has the code: `argv[1] == "--verify"` triggers
  the persistence check instead of the write test.
- Now that argv reaches the child (v0.5.4), the path is reachable:
  `fstest`, reboot, `fstest --verify` should print
  `[fstest] PASS (file survived reboot, byte-exact)`.
- Not yet run in a two-boot sequence. Also a good candidate for
  `make test` (§4.12).

---

## ⭐ v0.4.2 — STAR MSR Fix (August 2026)

**What was accomplished:**
- Fixed IA32_STAR MSR configuration for SYSCALL/SYSRET
- User CS = 0x30 → STAR[15:0] = 0x20
- Enabled clean SYSRET return path

**Key learnings:**
- STAR MSR requires careful selector calculation (`User CS - 16`)
- SYSRET uses STAR[15:0] + 16 for CS and STAR[15:0] + 8 for SS

---

## ⭐ v0.4.3 — ELF Loader Stabilized + Process Foundation (August 2026)

**What was accomplished:**
- ELF loader now works on **first boot** (no more "run twice" bug)
- Bootloader identity-mapping conflict resolved (detect and replace
  with proper user-mode PTEs)
- `PT_EXEC` (PWT bit) handling added to `vmm_map_page()`
- Safe HHDM-based user-space memory access in syscall handler
  (`safe_copy_from_user`)
- **Process Foundation:** PCB, `process_create()`, `proclist`,
  `proccreate`, `vmmclone`
- **Dynamic HHDM mapping:** `ensure_hhdm_mapped()`
- **BootInfo validation:** Magic number and version checking
- Removed redundant `simple` command
- All existing commands remain fully functional

**Key learnings:**
- Bootloader identity mappings must be replaced, not trusted
- `vmm_get_phys()` is more reliable than `vmm_is_mapped()` for
  detecting valid mappings
- HHDM is essential for safe kernel-to-user memory operations
- STAR MSR requires careful selector calculation (`User CS - 16`)

---

## ⭐ v0.4.4 — Process Stack Setup (August 2026)

**What was accomplished:**
- Added static kernel stack pool for processes (eliminates PMM
  corruption)
- Process creation with dedicated user and kernel stacks
- Process execution via direct function call (kernel mode)
- Process cleanup with `process_destroy()` (frees user stack, marks
  PCB unused)
- **`runproc` command** to create and execute a test process
- Shell returns properly after process execution
- All previous features (`proclist`, `proccreate`, `vmmclone`,
  `elfload`) remain fully functional

**Key learnings:**
- Static kernel stacks avoid dynamic allocation and PMM corruption
- Direct function call is simpler for testing than `iretq` user-mode
  transitions
- Process cleanup is essential to prevent memory leaks

## ⭐ v0.4.5 — Cooperative Scheduler (August 2026)

**What was accomplished:**
- Ready queue with round-robin scheduling
- `process_yield()` for voluntary context switching
- `process_exit()` for clean process termination
- Assembly-level context switching (`context_switch.asm`)
- **`testyield` command** to test cooperative scheduling
- **`schstat` command** to show scheduler statistics
- `runproc` now uses the scheduler
- All previous features (`proclist`, `proccreate`, `vmmclone`,
  `elfload`) remain fully functional

**Key learnings:**
- Context switching requires saving/restoring all registers
- The idle process needs special handling (no entry point)
- Processes must explicitly call `process_exit()` to terminate cleanly
- Round-robin scheduling requires moving processes to the end of the
  ready queue

---

## ⭐ v0.4.6 — Preemptive Scheduler & Unlocked Core (August 2026)

**What was accomplished:**
- Multi-Pass Segment Reader: `stage2.asm` pulls kernel blocks in safe
  128-sector chunks, advancing segment offsets dynamically to defeat
  the 64 KB real-mode wrap limit
- Kernel disk read threshold lifted to 256 sectors (128 KB)
- `SYS_REBOOT` (syscall #25) added; Ring 3 user shell option 4 reboots
  via a Ring 0 triple-fault
- Preemptive core integration: PIT clock (`IRQ0` at 100 Hz) enforces
  quantum-based task slicing across ready queues

**Key learnings:**
- Real-mode segment arithmetic matters for large kernels
- A userland reboot needs a controlled, tested path back to ring 0
- Preemptive slicing is stable when the timer path saves and restores
  per-process kernel frames

---

## ⭐ v0.4.7 — newlib, Blocking I/O, Boot Choice (September 2026) — *dons-os era*

**What was accomplished:**
- **newlib 4.x in userland**: `printf`, `malloc`/`free`, `memcpy`,
  `str*` work in Ring 3, statically linked against `libc.a`/`libm.a`
- **Kernel-stack-on-syscall-entry**: syscall path runs on a per-process
  kernel stack (from A2, tag `20260912I`)
- **Blocking `sys_read`**: reads no longer spin the CPU; blocked
  processes yield via the timer and are woken by `irq1`
- **Boot-time shell choice**: 2-second window, `k` for kernel shell,
  any other key (or timeout) for the user shell
- **User shell is the terminal console**: kernel shell is not reachable
  after boot without a reboot
- **Kernel diagnostics return to the kernel shell** on exit
- **Userland heap test**: user shell menu option 3 exercises
  `malloc`/`free` over `sys_brk`
- **`gdtdump` and `tssdump`** kernel shell commands
- **`testyield` fix**: `process_yield` no longer corrupts the ready
  queue
- **TSS.RSP0 / `g_syscall_stack_top` in lockstep** from boot

**Key learnings:**
- Kernel code must run on a kernel stack, not the user stack; the
  earlier `sti; hlt` spin in `sys_read` blocked the whole scheduler
- The timer path already does everything a cooperative switch needs;
  use it
- A second context-switch primitive (or fabricating iretq frames from
  PCB slots) causes more bugs than it solves
- `process_yield` must not remove the current process from the ready
  queue if it is not on it — remove on an off-queue process corrupts
  the queue
- Newlib reentrancy requires `_impure_ptr = &_impure_data` at startup
- `sys_brk` must `invlpg` after mapping, or the first user write faults

---

## ⭐ v0.4.8 — Process Cleanup on Exit (September 2026)

**What was accomplished:**
- **PCB reclaim.** `process_exit` calls a new `process_reclaim`, which
  frees the exiting process's ELF segment pages and user stack pages
  and marks the PCB slot reusable. Repeated `testyield` and `runproc`
  in one boot no longer exhaust the 32-slot pool.
- **`process_exit` timer race closed.** Interrupts are disabled for the
  entire critical section (state transition + queue removal + reclaim +
  context switch). The timer can no longer re-add the exiting process
  to the ready queue or save the current stack frame into `next->rsp`.
- **Page-table teardown deferred.** The process's page tables still
  leak; the teardown requires walking the page tables and freeing only
  the user-space portion.

**Key learnings:**
- Reclaiming the PCB made slot reuse happen sooner, which exposed a
  latent timer race in `process_exit`. The two fixes are coupled: the
  reclaim is correct, but only with the `cli` window.
- `context_switch` is not safe against timer preemption mid-switch. Any
  code path that mutates `current_process` and then calls
  `context_switch` must run with interrupts disabled, or the timer can
  save the wrong stack frame into the wrong PCB.
- A `noreturn` exit path that can fall through to a kernel shell or a
  halt must explicitly `sti` on that fallback, because the `cli` from
  the critical section persists until the next `iretq`.

---

## ⭐ v0.4.9 — Scheduler, TSS, and Interrupt ABI Stability Pass (September 2026)

**What was accomplished:**
- **ELF-load race at boot.** Fixed at all three call sites (`kmain`
  boot path, `elfload`, `usershell`). Eliminated the intermittent
  user-mode `#PF` at `CR2 == RIP == 0x8000000000` that reproduced when
  a key was pressed during the boot-choice window.
- **`TSS.RSP0` / `g_syscall_stack_top` lockstep.** Gate on
  `entry_point < KERNEL_BASE` removed in `scheduler_switch_to`,
  `process_exit`, and `timer_preempt_handler`. Both fields now update
  on every switch.
- **`process_exit` fallback TSS restoration.** The no-runnable-process
  fallback now restores both `TSS.RSP0` and `g_syscall_stack_top` to
  idle's kernel stack top.
- **Kernel stack slot aliasing.** Replaced `pid % MAX_PROCESSES` with
  an explicit `slot_owner[]` allocator and a
  `pcb_t::kernel_stack_slot` field. Survives 20+ `testyield` runs per
  boot with no fault.
- **`context_switch.asm` offsets.** Every offset at or after
  `user_stack_phys` updated by +8 to account for the new
  `kernel_stack_slot` field. `_Static_assert` block in `process.c`
  pins the offsets.
- **`scheduler_switch_to` preemption window.** Added
  `__asm__ volatile("cli")` at the top of the function to close the
  race between `current_process = next` and `context_switch(prev,
  next)`.
- **`irq1_stub` ABI violation.** All stubs that call C handlers now
  preserve all 15 GPRs. `isr8_stub` also fixed a latent missing
  `add rsp, 8` for the CPU's error code. `process_exit`'s fallback now
  uses a direct `jmp kmain_shell_loop`.
- **Frame validation in `context_switch.asm`.** `.kernel_task` now
  checks the resume frame's `rip` and `cs` before popping, and emits a
  serial marker byte (`R` or `C`) and halts on failure.

**Key learnings:**
- **Two-variable invariants need to be updated everywhere or nowhere.**
  The TSS.RSP0 / `g_syscall_stack_top` pair was updated in one path and
  skipped in another; the divergence was invisible until it wasn't.
- **Monotonic counters and modulo indexing don't mix.**
  `next_pid % MAX_PROCESSES` looks fine until `next_pid` exceeds
  `MAX_PROCESSES`, at which point it aliases live slots.
- **Any struct change in C needs to be reflected in the assembly that
  reads it.** The `_Static_assert` guard is the durable fix; the offset
  update is the one-time fix.
- **Stubs that call C code must save caller-saved registers.** The SysV
  ABI lets the callee clobber `rax`, `rcx`, `rdx`, `rsi`, `rdi`,
  `r8`–`r11`. Interrupt/exception entry points that return via `iretq`
  must restore them.
- **Indirect jumps through registers are fragile.** A direct jump
  (`jmp symbol`) emits a `rel32` with no register involved, and no
  interrupt can corrupt the target.

---

## ⭐ v0.4.10 — ATA PIO + FatFs (September 2026)

**What was accomplished:**
- **ATA PIO block device driver** on the primary channel. `ata_init`
  probes master and slave via IDENTIFY, extracts model strings, and
  logs to serial.
- **Per-drive API.** `ata_read_sector_drive`, `ata_read_sectors_drive`,
  `ata_write_sector_drive`, `ata_write_sectors_drive`,
  `ata_flush_cache_drive`. Default-drive wrappers remain for existing
  callers.
- **Write-protect floor on master.** Refuses writes below
  `ATA_WRITE_PROTECT_LBAS` (initially 64, later raised to 2048 with
  single-drive). Slave is unrestricted.
- **Three bring-up bugs found and fixed:**
  - **PIC mask restoration.** `pic_remap` was saving and restoring the
    BIOS PIC mask values; the BIOS leaves IRQ14 unmasked, so the first
    IDENTIFY asserted IRQ14 → vector 46 → `#GP` (no gate). Fixed by
    writing a known-good mask.
  - **Exception frame offsets.** `isr13_handler` and `isr14_handler`
    read the CPU-pushed error code / RIP / CS at offsets 0..4 from the
    frame base, but the frame base points at the top of the 15-GPR
    save area. Fixed by indexing from `EXC_OFF_ERROR_CODE = 15`
    upward.
  - **LBA mode bit.** `ata_select_drive` wrote 0xA0/0xB0 to the
    drive/head register, leaving bit 6 (the "L" bit) clear. IDENTIFY
    ignores L, but READ/WRITE SECTORS interpret the LBA registers as a
    CHS tuple when L=0. Fixed by using `ATA_DRIVE_MASTER_LBA = 0xE0` /
    `ATA_DRIVE_SLAVE_LBA = 0xF0` in the read/write path.
- **`atatest`** kernel shell diagnostic: read LBA 0 and verify the MBR
  signature, read LBA 64/128 and dump the kernel header, dump master
  and slave model strings.
- **FatFs R0.16 vendored** in `04_kernel_64bit/fatfs/` with a
  `diskio.c` shim over the ATA driver. `FF_FS_READONLY = 0`,
  `FF_FS_MINIMIZE = 0`, `FF_MULTI_PARTITION = 0`, `FF_USE_LFN = 0`
  (initial).
- **Dual-drive storage.** Kernel on master `hdd.img`, FAT16 volume on
  slave `fat.img` (whole disk, no partition table).
- **Kernel shell commands:** `fatmount`, `fatls`, `fatcat <file>`.
- **Userland file I/O.** Per-process file table, `SYS_OPEN` (4),
  `SYS_CLOSE` (6), `SYS_GETPID` (20), and an fd branch in `SYS_READ`
  (3). `arc2/syscalls.c` wires newlib's `open`/`close`/`read`/`write`
  to the syscall instruction.
- **Latent bug fixed:** `sys_open` only set `FA_WRITE` when
  `flags == 1`, so `O_WRONLY | O_CREAT | O_TRUNC` (0x601) never enabled
  write mode.
- **`FF_FS_READONLY = 1 → 0`** exposed a `.userelf` corruption bug:
  `stage2.asm` was staging the third 64 KB read into VGA graphics
  memory (`0xA0000`), which QEMU did not reliably persist. Fixed by
  moving PASS 3 to `0x20000` (plain RAM). Also added `.bss` zeroing in
  `entry.asm` and stripped the embedded user ELF before `xxd -i`.

**Key learnings:**
- BIOS PIC mask values are not safe to restore; write a known-good mask
  instead
- Exception frames from stubs that push 15 GPRs have the CPU-pushed
  fields at offset 15, not 0
- The ATA drive/head "L" bit is required for LBA mode on READ/WRITE
  SECTORS
- `0xA0000–0xAFFFF` is VGA graphics memory in any mode, including text
  mode 03h; it is not free RAM
- `FF_MULTI_PARTITION = 0` in FatFs means FatFs is not partition-aware
  (see v0.4.11)

---

## ⭐ v0.4.11 — Single-Drive Layout (September 2026)

**What was accomplished:**
- **`FAT_CONFIG=dual|single`** at build time selects the storage
  layout. The Makefile passes `-DFAT_CONFIG_SINGLE_DRIVE=0|1`;
  `include/fat_config.h` derives the drive selection and
  volume-sector count.
- **`diskio.c` partition offset.** `FAT_PARTITION_OFFSET` is 0 in dual,
  2048 in single. `disk_read` and `disk_write` add it to every
  FatFs-supplied sector number before calling the ATA layer.
  `disk_ioctl(GET_SECTOR_COUNT)` returns `FAT_VOLUME_SECTORS` (the
  volume size, not the device size).
- **Key correction to an earlier design.** With
  `FF_MULTI_PARTITION = 0`, FatFs reads the FAT boot sector at LBA 0
  of the physical drive and does not consult the BPB `hidden_sectors`
  field. An earlier iteration assumed FatFs would read
  `hidden_sectors` and skipped the offset in `diskio.c`; the mount then
  failed with `FR_NO_FILESYSTEM` (13), because FatFs read the boot
  sector at LBA 0 (boot chain) instead of the FAT boot sector at LBA
  2048.
- **Write-protect floor raised** from LBA 64 to LBA 2048. In
  single-drive mode the kernel lives in LBA 128..2047, so a stray write
  below 2048 would corrupt the kernel. The floor also covers the boot
  chain and the gap.
- **`atatest` write leg removed.** LBA 1024 is inside the kernel region
  in single-drive mode. The raw-sector round-trip was a footgun on a
  disk that now has a filesystem on it; the user shell's `[FS TEST]`
  exercises the write path through FatFs.
- **`kmain.c` config diagnostic.** Boot-time mount prints a
  config-specific line to VGA and serial: `"Storage: single-drive,
  FAT@LBA 2048"` or `"Storage: dual-drive, FAT@LBA 0 on slave"`.
  Mount failures now appear on VGA, not just serial.
- **`hdd-single.img`** built by `mkfs.vfat -F 16 -h 2048 --offset=2048`
  and populated by `mcopy` from `test-files/`. The image-build rule
  verifies the FAT boot sector and the BPB `hidden_sectors` field after
  assembly.
- **Kernel-size tripwire** now checks two limits: the stage2 staging
  ceiling (448 KB) and the disk-layout ceiling (983 KB, the region
  below LBA 2048).
- **User shell gains options 5, 6, 7.** Option 5 verifies persistence
  across reboot; option 6 creates 3 files and verifies contents
  (delete skipped, no `SYS_UNLINK`); option 7 is a 4 KB round-trip
  that catches multi-cluster bugs.
- **`[FS TEST]` fixed.** `msg_len` was hardcoded to 19 but
  `"Hello from ring 3!\r\n"` is 20 bytes; the trailing `\n` was being
  silently truncated.

**Key learnings:**
- `FF_MULTI_PARTITION = 0` means the partition offset belongs in
  `diskio.c`, not the BPB
- A write-protect floor must be maintained when the kernel or partition
  layout changes; a stale floor is a latent bug
- The user shell is the right place for regression tests: it exercises
  the syscall path, the FatFs path, and the persistence path through
  the same code real programs use

---

## ⭐ v0.5.0 — Storage Layer Complete (September 2026)

**What was accomplished:**
- ATA PIO block device driver (see v0.4.10)
- FatFs R0.16 vendored and integrated, read and write, kernel-side and
  userland (see v0.4.10)
- Single-drive layout with FAT16 partition at LBA 2048 (see v0.4.11)
- Persistence across reboot verified end-to-end
- Expanded user-shell regression harness (options 4, 5, 6, 7)
- Config-specific boot-time storage diagnostic on VGA and serial
- Kernel-size tripwire checks both the stage2 staging ceiling (448 KB)
  and the disk-layout ceiling (983 KB)
- A new `run` script at the repo root: one-line-per-option QEMU
  launcher

**Key learnings:**
- The storage layer is a natural milestone: it exercises the boot
  chain, the ATA driver, the FatFs shim, the syscall layer, the
  userland C library, and the file I/O path all at once
- Two configurations (dual-drive, single-drive) with a single kernel
  source is achievable when the difference is expressed as compile-time
  constants and a small amount of `diskio.c` offset arithmetic
- Persistence is the right shape for a storage test; buffered writes
  that pass in-memory round-trips are not proof of durability

**Not yet implemented at v0.5.0:**
- `SYS_UNLINK` (fixed in v0.5.2)
- Loading user programs from disk (`sys_exec`) (fixed in v0.5.3)

---

## ⭐ v0.5.1 — Maintenance Batch: Self-Test, NX, Kernel-Owned GDT (September 2026)

**What was accomplished:**
- Self-test infrastructure (`selftest` command, 15 tests) — see §3.24
- `EFER.NXE` enabled so NX bits in PTEs are enforced on hardware — see
  §3.24
- Kernel-owned GDT at a higher-half address — see §3.24
- Print atomicity (shared serial/VGA lock) — see §3.24
- `#DF` routed through IST1 — see §3.24
- Kernel shell runs on a pool-allocated stack; the hardcoded
  `0xFFFFFFFF8008FF00` is gone — see §3.24
- Staging ceiling raised from 176 KB to 448 KB
- Reboot flushes file handles before the reset

**Key learnings:**
- A self-test suite is the right investment: it found two real latent
  bugs (EFER.NXE off, GDT in low memory) that no one was looking for.
- "Works under KVM" is not the same as "works on hardware."
- Low-memory structures are fragile. Anything the bootloader left in
  low memory is a candidate for the same relocation treatment the boot
  stack (item 3a) and GDT (item 3h) received.
- Print atomicity is not just cosmetic. Interleaved serial output
  obscures real diagnostics.

**Not yet implemented at v0.5.1:**
- `SYS_UNLINK` (fixed in v0.5.2)
- Loading user programs from disk (`sys_exec`) (fixed in v0.5.3)
- Boot-time self-test mode and `make test` (deferred; see
  `MAINTENANCE.md` §5b–5c)

---

## ⭐ v0.5.2 — Heap Rewrite, LFN, SYS_UNLINK, Cross-GCC for FatFs, Makefile Dependency Tracking (September 2026)

**What was accomplished:**
- **heap.c rewritten.** `heap_extend` now returns the start of the
  newly mapped region and the caller places a single free block
  covering the entire region. The old version placed the new block at
  `heap_brk - requested_size`, which lost up to `PAGE_SIZE - 1` bytes
  per extension and put blocks at the wrong end of the mapped range.
  Blocks are maintained in address order and coalesced on free.
- **`heap_header_t` padded to 48 bytes.** Payloads are 16-aligned when
  the allocation size is a multiple of `HEAP_ALIGNMENT`. newlib's
  `malloc` is documented to return 16-aligned pointers on x86-64; the
  40-byte header made payload addresses alternate between 8- and
  16-aligned.
- **`heap_validate()` and `heap_stress()`.** `heap_validate` walks the
  block list and checks every invariant, including that the last
  block's end equals `heap_brk`. `heap_stress` runs 4096 ops across 512
  slots with per-block patterns, grows the heap from 1 MB to ~4.15 MB
  (multiple extensions), and validates intact with zero leak.
- **`heapcheck` and `heapstress` shell commands.** Both added to
  `selftest`. Test count 17/17.
- **Long filename support.** `FF_USE_LFN = 2`, `FF_LFN_UNICODE = 2`,
  `FF_CODE_PAGE = 437`. `fatfs/ffunicode.o` linked into the kernel.
  Kernel grows from 158 KB to 163 KB, well under the 448 KB staging
  limit. `fatls` shows `HELLO-WORLD.TXT`; `fatcat HELLO-WORLD.TXT`
  reads it.
- **`SYS_UNLINK` (syscall #7).** Wraps FatFs `f_unlink`. Kernel handler
  `sys_unlink` in `user_syscall.c`; userland `unlink()` shim in
  `arc2/syscalls.c`. `test_multi_file` in the user shell now deletes
  the files it creates and verifies they are gone.
- **`sys_open` path truncation fix.** Was copying only 127 bytes of the
  path into a 300-byte buffer. Replaced with a `copy_user_string`
  helper that respects the buffer size. Paths up to `FF_MAX_LFN` (255)
  now fit. New `USER_PATH_MAX` constant (300).
- **Cross-GCC for FatFs.** `fatfs/ff.o` and `fatfs/ffunicode.o`
  compiled with `/opt/cross/bin/x86_64-elf-gcc` at `-O2`. Clang 22.1.8
  miscompiles `ff.c` at every optimization level: `-O0` breaks the
  FILINFO read path (filenames decode as `@80(`, `f_opendir` returns
  `FR_INT_ERR`), `-O1` hangs in `f_unlink`, and `-O2` produces
  LLD-truncated instructions in the linked binary. GCC produces correct
  code and links cleanly with the Clang-built kernel objects. See
  `LLD_BUG_REPORT.md`.
- **Makefile header dependency tracking.** `-MMD -MP` added to
  `CFLAGS`; `-include $(OBJS:.o=.d)` at the bottom. Header changes now
  trigger the right rebuilds automatically. This was the root cause of
  several stale-object debugging sessions.

**Key learnings:**
- A heap allocator without a validator is a heap allocator you cannot
  trust. The `heap_validate` invariant that the last block's end must
  equal `heap_brk` is what would have caught the old extension bug
  immediately.
- The `heap_stress` test must be sized to actually exceed the initial
  heap. The first version allocated only ~256 KB and never forced an
  extension; the size constants were wrong.
- A link-time truncation bug is much harder to diagnose than a compiler
  bug. The two look identical in a disassembly of the linked binary;
  they are distinguished by compiling the same source standalone and
  comparing.
- A Makefile without header dependency tracking turns every header
  change into a two-step rebuild. `-MMD -MP` fixes this permanently.
- Three separate toolchain bugs in Clang 22.1.8 and LLD 22.1.8 affected
  `ff.c` — one at each optimization level. All three are avoided by
  compiling that file with GCC instead. The per-file compiler override
  is the right shape for a toolchain bug that only affects one file.
- GCC and Clang emit ELF64 relocatable objects using the same x86-64
  System V ABI for the `x86_64-unknown-elf` target. There is no
  linker-level distinction between a GCC `.o` and a Clang `.o`, so a
  per-file compiler override is transparent to the rest of the build.

**Not yet implemented at v0.5.2:**
- Loading user programs from disk (`sys_exec`) (fixed in v0.5.3)
- ELF loader `PT_NX` follow-up

---

## ⭐ v0.5.3 — `SYS_EXEC` Complete: Disk-Loaded ELF User Programs + Three Memory-Safety Fixes (September 2026) — *dons-os era; superseded by Linux syscalls in donix*

**What was accomplished:**
- **`SYS_EXEC` (syscall #8).** Opens an ELF file on the FAT volume,
  loads it into a new process, and returns the child's pid.
  `spawn("0:/HELLO.ELF")` from the user shell loads `HELLO.ELF` from
  disk at runtime; the child runs as an independent Ring 3 process and
  prints its banner. This is the feature that stops user programs from
  being embedded in the kernel image.
- **`SYS_WAITPID` (syscall #9).** Blocks the calling process until a
  matching child exits, then reaps it and returns its exit status.
  `WNOHANG` supported. `pid > 0` waits for a specific child;
  `pid == -1` waits for any child.
- **`spawn()` and `waitpid()` shims** in `arc2/syscalls.c`, and
  `apps/include/donsdos.h`.
- **Process parent/child tracking.** New `pcb_t` fields `parent_pid`,
  `exit_status`, `wait_pid`, and a `PROC_STATE_ZOMBIE` state. A process
  with a parent becomes a zombie on exit and is reaped by
  `sys_waitpid`; a process with no parent reclaims itself immediately,
  preserving the pre-v0.5.3 behavior for idle and the shells.
  `process_wake_parent_if_waiting` in `process.c` walks the PCB pool
  for a parent in `PROC_STATE_BLOCKED` with
  `block_kind == BLOCK_KIND_WAITPID` and a matching `wait_pid`.
- **Three latent memory-safety bugs found and fixed during bring-up.**
  Each was independently sufficient to make `SYS_EXEC` flaky, and each
  is documented in `MAINTENANCE.md` §3j with the specific symptom it
  caused:
  1. **PMM allocator reentrancy.** `pmm_alloc_page` did a non-atomic
     read-modify-write on the shared bitmap; a timer IRQ between
     `bitmap_test` and `bitmap_set` could hand the same page out twice.
     Fixed with cli/sti critical sections in `pmm_alloc_page`,
     `pmm_free_page`, `pmm_reserve_page`, and `pmm_unreserve_page`,
     using `pushfq`/`pop` to preserve the caller's IF.
  2. **Page-table aliasing in `vmm_clone_page_table`.** The clone was a
     shallow copy of the PML4: parent and child shared every PDPT, PD,
     and PT. Any `vmm_map_page_in_cr3` on the child therefore
     overwrote the *parent's* PTEs, silently replacing the shell's
     code, `.rodata`, and user stack with the child's. Fixed with a
     deep copy of the low-half page-table hierarchy (PML4[0..255]);
     the high half (HHDM and kernel) remains shared by design.
  3. **`context_switch` resumed processes by `entry_point`.** The
     resume side chose user vs kernel by comparing `next->entry_point`
     against `KERNEL_BASE`, which is correct for the first dispatch of
     a fresh process but wrong for a process that had been preempted or
     blocked in kernel mode. The old code took the user branch and
     *rebuilt* the frame from `entry_point` and `user_stack_top`,
     restarting the process from `_start`. This is what produced "shell
     restarts after `waitpid` returns." Fixed by making the resume side
     trust the saved frame verbatim and choose the validation rule from
     the frame's own CS.
- **`HELLO.ELF` built and copied into the FAT partition** at
  image-build time by `05_boot_kernel64/Makefile`. The `hello.c` user
  program is 20 KB.
- **User shell option A.** Loads `HELLO.ELF` from disk, runs it as an
  independent Ring 3 process, blocks in `waitpid`, and verifies the
  exit status is 0. Two consecutive spawns (pid 3, then pid 4) succeed
  with clean menu returns between them.
- All prior features remain functional. `selftest` still passes 17/17;
  user shell options 1–9 still work.

**donix note:** the dons-os `SYS_EXEC` (8) and `SYS_WAITPID` (9) were
removed at A5 step 2 (spawn) and replaced by Linux `execve(59)` and
`wait4(61)` during A2. The design reasoning is preserved in git history
at `ecc8ade` (the deleted `SYS_EXEC_PLAN.md`).

**Key learnings:**
- Three memory-safety bugs, each individually fatal to the feature, can
  coexist and mask one another. The path-string corruption, the
  user-mode `#PF` at RIP=0/1, and the shell restart were all different
  symptoms of the same three bugs interacting.
- The PMM's test-and-set was only a problem because the timer preempts
  kernel-mode processes. In a single-threaded kernel without
  preemption, the same code is correct. Enabling preemption is what
  turns a "safe in practice" allocator into a latent double-allocator.
- A shallow page-table clone is not a copy. It shares every level below
  the root. Any code that assumes "the clone's mappings are private" is
  wrong.
- Resuming a context-switched process from `entry_point` is only
  correct for the very first dispatch. After that, the saved frame is
  the truth.
- The three fixes were all localized (a few lines each) but the
  *symptoms* were not. The bug you see is not the bug you're looking
  for.

**Not yet implemented at v0.5.3:**
- Boot-time self-test mode and `make test` (deferred; see
  `MAINTENANCE.md` §5b–5c)
- Spawn regression test (see `MAINTENANCE.md` §5d)
- ELF loader `PT_NX` follow-up
- Page-table teardown on process exit
- Per-process tty / console focus

---

## ⭐ v0.5.4 — The REPL Shell: External Programs by Name, `argv`, `ls`, `cat`, `echo` (September 2026) — *dons-os era; superseded by musl_sh in donix*

**What was accomplished:**

- **The user shell is now a REPL.** It reads a whole line, tokenizes on
  whitespace, resolves the first token to `0:/NAME.ELF`, spawns it via
  `SYS_EXEC`, waits for it via `SYS_WAITPID`, and loops. The old
  fixed-menu driver is gone; the shell contains no test code.
- **Built-ins:** `exit`, `help`, `reboot`. Everything else is an
  external program on the FAT volume.
- **The four-stage REPL plan completed end-to-end:**
  - **Stage 1:** `user_shell.c` rewritten (~150 lines) as a REPL with
    `readline()`, a whitespace tokenizer producing a full `argv[]`, a
    path resolver (`memtest` → `0:/MEMTEST.ELF`), and a spawn+waitpid
    helper. First external program: `memtest.elf`.
  - **Stage 2:** Regression tests ported to external programs.
    `fstest.elf` (with argv-gated `--verify` for the old persistence
    check), `multitest.elf`, `bigtest.elf`. No test code remains in the
    shell.
  - **Stage 3:** Directory iteration. Three new syscalls —
    `SYS_OPENDIR` (#12), `SYS_READDIR` (#13), `SYS_CLOSEDIR` (#14) —
    wrapping FatFs `f_opendir`/`f_readdir`/`f_closedir`.
    `file_table[]` now holds tagged `file_slot_t*` entries so files and
    directories share the handle space. `struct dons_dirent` in
    `user_syscall.c` and `apps/include/donsdos.h`. New userland program:
    `ls.elf`.
  - **Stage 4:** `argv` passing. `SYS_EXEC` extended from `(path)` to
    `(path, argc, argv)`. `sys_exec` copies the argv strings and
    pointer array onto the child's user stack via
    `safe_copy_to_user_cr3` (a new helper that resolves against the
    child's `cr3`, not the caller's). `crt0.S` stashes `rdi`/`rsi` in a
    `.data` slot before the BSS-clear loop and restores them just before
    `call main`. `spawn()` signature changed to
    `spawn(path, argc, argv)`. New userland programs: `cat.elf`,
    `echo.elf`.
- **Two bugs found and fixed during bring-up (both documented in
  `MAINTENANCE.md`):**
  - **§3k — `safe_copy_to_user` uses the caller's `cr3`.** `sys_exec`
    must write to a child process whose `cr3` differs from the
    caller's. Switching `cr3` around the write was insufficient because
    `safe_copy_to_user` still read `current->cr3`. Because parent and
    child use the same user-stack virtual addresses, the write
    *succeeded* — into the parent's stack. The child then read zeros
    for `argv`. Fixed with `safe_copy_to_user_cr3(uint64_t cr3, ...)`.
  - **§3l — argv region collides with the child's own stack frames.**
    The first layout placed argv strings at the *top* of the reserved
    region, growing down. The child's own `crt0.S` prologue and every
    function call overwrote them before `main` ran. Fixed by placing
    strings at the *bottom* of the region, growing up, with a free gap
    above them and below the child's initial `rsp`.
- **End-to-end test on a fresh boot passes:** `help`,
  `echo hello world`, `echo one two three`, `echo` (blank line),
  `cat HELLO-WORLD.TXT` (byte-exact), `ls` (correct file list and
  sizes), `memtest`, `fstest`, `multitest`, `bigtest` — all PASS;
  `nope` — command not found; `exit` — clean halt.
- **Build system.** `apps/cat.elf` and `apps/echo.elf` targets added;
  `CAT.ELF` and `ECHO.ELF` copied onto the FAT image. The `mcopy` block
  was refactored into an `mcopy_one` shell function inside the recipe
  to keep it readable with eight ELFs.
- **Dispatcher bug fixed.** `SYS_ARCH_SET_FS` is defined as 11 in
  `syscall.h`, but the dispatcher had the handler under `case 5`.
  Nothing called it, so it was latent; corrected in this release.

**donix note:** this section describes the dons-os REPL shell. In
donix the shell is `musl_sh`, a musl-linked ELF loaded from
`0:/MUSL_SH.ELF`; it uses Linux `fork`/`execve`/`wait4` instead of
`SYS_EXEC`/`SYS_WAITPID`, and its command-line normalization matches
the dons-os convention (bare `NAME` → `0:/NAME.ELF`, file arguments
passed through verbatim).

**Key learnings:**
- **The `_cr3`-suffixed helper is not optional when writing to another
  process.** `safe_copy_to_user` silently assumes "current process is
  the target." The failure mode was misleading: the write succeeded
  into the wrong address space because both processes happened to use
  the same user-stack virtual addresses.
- **Where data goes on a downward-growing stack matters.** Placing argv
  at the top of the reserved region was wrong; the child's own function
  calls overwrite it before `main` runs. Placing it at the bottom, with
  a free gap above, is correct — and matches Linux's layout.
- **`crt0.S` has to save the kernel-provided registers before the
  BSS-clear loop.** `rep stosb` clobbers `rdi` and `rsi`. A `.data`
  slot survives the BSS clear; a register does not.
- **Instrumented builds are worth the trouble.** The
  `safe_copy_to_user_cr3` bug was diagnosed by adding per-step logging
  to `sys_open`/`sys_write`/`sys_close` and reading one boot log. The
  argv-layout bug was diagnosed by inspecting a serial capture. Both
  took hours, not days, because the symptom (child reads zeros / child
  reads garbage) pointed at the exact site.
- **The `file_slot_t` refactor is more invasive than it looks.** Every
  `file_table[]` user had to learn about the tag. The change touched
  six call sites. The result is worth it — files and directories share
  one handle space — but the diff is larger than a "add three syscalls"
  description suggests.
- **Two divergences from standard practice, both recorded in
  `MAINTENANCE.md`:** the initial child `rsp` is not SysV-compliant
  (`argc`/`argv` are in registers, not on the stack — §3m), and the
  argv region shares the user stack with the child's own frames (§3l).
  Both are fine today; both will need attention before a prebuilt
  binary is linked or a deeply-recursive user program is added.

**Not yet implemented at v0.5.4:**
- `fstest --verify` cross-boot persistence test (now reachable with
  argv; not yet run in a two-boot sequence)
- Boot-time self-test mode and `make test` (`MAINTENANCE.md` §5b–5c)
- Spawn and argv/REPL regression tests (`MAINTENANCE.md` §5d–5e)
- ELF loader `PT_NX` follow-up
- Page-table teardown on process exit
- Per-process tty / console focus
- Kernel log routing to serial only (`MAINTENANCE.md` §4i)
- Pipes, redirection, `cd`, environment variables

---

## 5. Development Tools

### ✔ QEMU Debug Mode
- `-serial stdio` for real-time serial console output
- `-serial file:qemu.log` for saving serial output
- `-d int,cpu_reset,guest_errors` for interrupt logging
- `-no-reboot -no-shutdown` for debugging crashes
- Multiple run modes: run, run-log, run-debug, run-verbose,
  run-headless, run-kvm, run-single, run-kvm-single, run-telnet

### ✔ GDB Remote Debugging
- `make runkernel64-debug` for GDB server
- Connect with: `gdb -ex "target remote localhost:1234" kernel.elf`

### ✔ Build Automation
- Top-level Makefile with debug targets
- `make logkernel64` for debug runs
- `FAT_CONFIG=dual|single` selects the storage layout
- `./run` convenience script with a menu of preconfigured targets
- Header dependency tracking (`-MMD -MP`)

### ✔ musl Toolchain (added in donix)
- `toolchain/install_musl.sh` — clone and build musl 1.2.5 from source
- `toolchain/musl-gcc.sh` — wrapper around the project-local specs file
- `build_musl_tests.sh` — build every musl ELF the FAT image carries

---

## Status Summary

The **donix v0.5.5** block lists the migration work that produced this
project. The rows below it describe dons-os capabilities that carried
forward; the **Syscall ABI** row reflects donix's current state.

| Stage | Status |
|-------|--------|
| **donix v0.5.5 — musl migration** | ✔ **Complete** |
| A1 — syscall renumbering | ✔ Complete |
| A2 — musl syscalls added (`arch_prctl`, `set_tid_address`, `fork`, `execve`, `wait4`, `getdents64`, `stat`, `fstat`, ...) | ✔ Complete |
| A3 — `musl_sh` boot shell | ✔ Complete |
| A4 — userland apps ported (hello/echo/cat/ls/memtest) | ✔ Complete |
| A5 — newlib retired | ✔ Complete |
| Boot chain | ✔ Complete |
| Long-mode kernel | ✔ Complete |
| Interrupts (IRQ0/IRQ1) | ✔ Complete |
| PMM | ✔ Complete |
| VGA Console | ✔ Complete |
| Command Shell | ✔ Complete |
| Higher-half kernel | ✔ Complete |
| Exception Handlers | ✔ Complete |
| Virtual Memory Manager | ✔ Complete |
| Serial Debug Output | ✔ Complete |
| Heap Allocator | ✔ Complete |
| User Mode (Ring 3) | ✔ Complete |
| NX Bit Support | ✔ Complete |
| **Syscall ABI** | ✔ **Linux x86_64 (donix); dons-os-private numbers retired** |
| ELF Loader | ✔ Complete |
| STAR MSR Fix | ✔ Complete |
| Process Foundation | ✔ Complete |
| Process Stack Setup | ✔ Complete |
| Process Execution | ✔ Complete |
| Process Cleanup | ✔ Complete |
| Cooperative Scheduler | ✔ Complete |
| Context Switching | ✔ Complete |
| Process Yield/Exit | ✔ Complete |
| Preemptive Scheduler | ✔ Complete |
| Segment-Shifting Bootloader | ✔ Complete |
| Userland Syscall Reboot (SYS_REBOOT 503) | ✔ Complete |
| Kernel-Stack-on-Syscall-Entry | ✔ Complete |
| Blocking `sys_read` | ✔ Complete |
| Boot-Time Shell Choice | ✔ Complete |
| `gdtdump` / `tssdump` | ✔ Complete |
| Process Cleanup on Exit | ✔ Complete |
| Scheduler / TSS / Interrupt ABI Stability | ✔ Complete |
| ATA PIO Driver | ✔ Complete |
| FatFs Integration | ✔ Complete |
| Userland File I/O over FatFs | ✔ Complete |
| Single-Drive Layout | ✔ Complete |
| Persistence Across Reboot | ✔ Complete |
| Self-Test Infrastructure (17 tests) | ✔ Complete |
| EFER.NXE enabled (NX on hardware) | ✔ Complete |
| Kernel-Owned GDT (higher-half) | ✔ Complete |
| Print Atomicity (shared serial/VGA lock) | ✔ Complete |
| #DF through IST1 | ✔ Complete |
| Kernel Shell on a Pool-Allocated Stack | ✔ Complete |
| Heap Rewrite and Validator | ✔ Complete |
| Long Filename Support | ✔ Complete |
| `SYS_UNLINK` (FatFs) | ✔ Complete |
| Cross-GCC for FatFs | ✔ Complete |
| Makefile Header Dependency Tracking | ✔ Complete |
| `sys_newfstatat` (262) | ☐ Planned |
| `fcntl` (72) | ☐ Planned |
| `sys_open` `O_DIRECTORY` fix | ☐ Planned |
| `isr14_handler` user-mode fault handling | ☐ Planned |
| ELF Loader `PT_NX` Follow-up | ☐ Planned |
| Page-Table Teardown on Process Exit | ☐ Planned |
| `fstest --verify` Cross-Boot Test | ☐ Planned |
| Kernel Log Routing to Serial Only | ☐ Planned |
| Boot-Time Self-Test Mode and `make test` | ☐ Planned |
| Spawn Regression Test | ☐ Planned |
| argv / REPL Regression Tests | ☐ Planned |
| Pipes and Redirection | ☐ Planned |
| `cd` / Relative Paths | ☐ Planned |
| Environment Variables (`envp`, `getenv`, `setenv`) | ☐ Planned |
| Per-Process tty / Console Focus | ☐ Planned |
| Serial Console Debug Access | ☐ Planned |
| Framebuffer Graphics | ☐ Planned |
| File System (VFS) | ☐ Planned |
| **Phase B — busybox against musl** | ☐ **Next** |

---

## 6. Phase B — busybox / coreutils against musl (next milestone)

**Status:** planned, not started.

The donix kernel speaks the Linux x86_64 syscall ABI and runs static
musl-linked binaries. The next milestone is running a **real Unix
userland**: a static busybox binary linked against the project-local
musl 1.2.5.

### Approach

1. Build busybox against the project-local musl:
   ```
   cd third_party
   git clone https://git.busybox.net/busybox
   cd busybox
   git checkout 1_36_stable        # or whatever the current stable is
   make defconfig
   # Set CONFIG_STATIC=y, CONFIG_PREFIX=/tmp/busybox-install
   # Use toolchain/musl-gcc.sh as CC
   make -j$(nproc)
   make install
   ```
   The result is `_install/bin/busybox`, a static musl-linked ELF.

2. Copy it to the FAT as `BUSYBOX.ELF` via `build_musl_tests.sh` or
   the image Makefile.

3. From `musl_sh`: `busybox.elf echo hello` (or the correct argv
   layout — busybox expects `argv[0]` to be the applet name).

4. Watch the `Unknown syscall: N` output. Every unimplemented syscall
   busybox hits is a candidate for the next kernel commit.

### What to expect

Busybox at startup does much more than the current canary tests: it
installs signal handlers, reads `/proc` or sysfs, checks terminal
settings, and uses `fcntl`, `getuid`, `getgid`, `geteuid`, `getegid`,
`umask`, `rt_sigreturn`, `prctl`, `setrlimit`/`getrlimit`, `uname`, and
possibly `access`/`faccessat`. Each of those is a small implementation.

Plan Phase B as a sequence of small commits:

1. Get busybox to link (build-script change only, probably).
2. Get it to reach `main` (may need new syscalls).
3. Get one applet (`echo` is easiest) to work.
4. Get a real applet (`ls`, `cat`) working.

Each is its own commit with the existing canary re-run.

### After Phase B

The medium-term items in section 4 are the natural follow-ups once
busybox runs: pipes and redirection, `cd`, environment variables, and
the `fcntl`/`newfstatat` implementations that busybox will have
surfaced.

---

## Notes

The roadmap is intentionally incremental. Each milestone builds toward a
fully functional x86_64 kernel while keeping the project educational and
approachable.

For known debt, latent bugs, and cleanup work, see
[`MAINTENANCE.md`](MAINTENANCE.md). That file is the counterpart to this
roadmap: this file lists features, that file lists everything else that
needs attention.

The session-by-session story of the musl migration is in
[`handoff.md`](handoff.md). The working tags used during the migration
are recorded in [`migration-tags.txt`](migration-tags.txt).

MIT licensed — contributions welcome.
