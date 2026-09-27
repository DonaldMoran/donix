# Full OSDev Milestone Checklist
## donix (x86_64) — Project Progress

For planned work, see [`ROADMAP.md`](ROADMAP.md).
For known debt and cleanup work, see [`MAINTENANCE.md`](MAINTENANCE.md).
For the session-by-session story of the musl migration, see
[`handoff.md`](handoff.md).

### Legend
- ✅ **Complete** — Feature implemented and stable
- 🚧 **In Progress** — Partially implemented or in testing
- ☐ **Not Started** — Planned for future development
- ⚠️ **Stable** — Working with known limitations
- ❌ **Not Needed** — Feature not required
- 🔵 **dons-os era** — Implemented in dons-os before the fork; may
  have been retired or superseded in donix (see the notes column)

---

## 0. donix — the musl migration — v0.5.5 (50/50 Complete)

The items below are the A1–A5 migration that produced donix. The
kernel carried forward from dons-os (sections 1–5); only the syscall
ABI and the userland library changed. See [`ROADMAP.md`](ROADMAP.md)
section 0 for the milestone narrative.

### 0.1 — Phase A1: syscall renumbering (4/4)

| # | Milestone | Status | Notes |
|---|-----------|--------|-------|
| A1.1 | **`include/syscall.h` renumbered to Linux x86_64** | ✅ Complete | All `SYS_*` defines. |
| A1.2 | **`user_syscall_entry.asm` renumbered** | ✅ Complete | The single numeric reference (`SYS_EXIT` = 60). |
| A1.3 | **`user_syscall.c` case labels renumbered** | ✅ Complete | `case N:` labels only; no body changes. |
| A1.4 | **`arc2/syscalls.c` `#define SYS_*` renumbered** | ✅ Complete | dons-os-era; the file was removed at A5 step 7. |

Complete at `20260922H`.

### 0.2 — Phase A2: musl syscalls (18/18)

| # | Milestone | Status | Notes |
|---|-----------|--------|-------|
| A2.1 | **`arch_prctl(ARCH_SET_FS)` = 158** | ✅ Complete | musl's `__init_tls` calls it before `__libc_start_main`. |
| A2.2 | **`set_tid_address` = 218** | ✅ Complete | Returns pid; `clear_child_tid` tracking not implemented. |
| A2.3 | **`rt_sigaction` = 13** | ✅ Complete | Stub returning 0. |
| A2.4 | **`rt_sigprocmask` = 14** | ✅ Complete | Stub returning 0. |
| A2.5 | **`set_robust_list` = 273** | ✅ Complete | Stub returning 0. |
| A2.6 | **`ioctl` = 16** | ✅ Complete | Returns `-ENOTTY`; musl's stdio checks `TCGETS`. |
| A2.7 | **`brk` = 12 (Linux absolute-address ABI)** | ✅ Complete | Fixed at `20260924B`. newlib's increment `sbrk` moved to a private syscall, later deleted at A5 step 5. |
| A2.8 | **`mmap` = 9, `munmap` = 11, `mprotect` = 10** | ✅ Complete | Minimal anonymous-private implementation; `mprotect` is a stub returning 0. |
| A2.9 | **`getrandom` = 318, `rseq` = 334** | ✅ Complete | Stubs returning `-ENOSYS`. |
| A2.10 | **`fork` = 57** | ✅ Complete | Child resumes at parent's user RIP with `%rax = 0`; eager user-stack copy; `fs_base` inherited. |
| A2.11 | **`execve` = 59 (in-place)** | ✅ Complete | Replaces the calling process's address space. dons-os spawn moved to 507 and later deleted at A5 step 2. |
| A2.12 | **`wait4` = 61** | ✅ Complete | Blocking wait, reap, parent-pid preserved across `execve`. |
| A2.13 | **`getdents64` = 217** | ✅ Complete | One record per call, deliberate; FatFs cursor is irreversible. |
| A2.14 | **`stat` = 4** | ✅ Complete | Added at the A4 `ls` port. |
| A2.15 | **`fstat` = 5** | ✅ Complete | Added at the A4 `ls` port. |
| A2.16 | **`writev` = 20** | ✅ Complete | ioiv array copied via `safe_copy_from_user`. |
| A2.17 | **`getpid` = 39** | ✅ Complete | — |
| A2.18 | **`unlink` = 87** | ✅ Complete | Renumbered from dons-os's 7 at A1. |

Complete at `20260924K`, extended with `stat`/`fstat` at `20260926T`.

### 0.3 — Phase A3: musl shell (3/3)

| # | Milestone | Status | Notes |
|---|-----------|--------|-------|
| A3.1 | **`musl_sh` bootstrapped** | ✅ Complete | Minimal `fork` + `execve` + `wait4` shell. |
| A3.2 | **Tokenizes command line into `argv`** | ✅ Complete | Whitespace split. |
| A3.3 | **Normalizes `argv[0]` to `0:/NAME.ELF`** | ✅ Complete | File arguments passed verbatim. |

Complete at `20260926J`.

### 0.4 — Phase A4: userland apps ported to musl (5/5)

| # | Milestone | Status | Notes |
|---|-----------|--------|-------|
| A4.1 | **`hello`** | ✅ Complete | `printf` only. |
| A4.2 | **`echo`** | ✅ Complete | `write` + argv. |
| A4.3 | **`cat`** | ✅ Complete | `open`/`read`/`close`/`write` + argv. |
| A4.4 | **`ls`** | ✅ Complete | `opendir`/`readdir` + `stat` per entry. Required `sys_fstat` and `sys_stat`. |
| A4.5 | **`memtest`** | ✅ Complete | `malloc`/`free`/`printf`. |

Complete at `20260926W` (parallel builds), cut over to `NAME.ELF` at
`20260926Y`.

### 0.5 — Phase A5: retire newlib (8/8)

| # | Milestone | Status | Notes |
|---|-----------|--------|-------|
| A5.1 | **Remove newlib `*-elf` targets and `user-elfs` aggregate** | ✅ Complete | Also dropped `FSTEST`/`MULTITEST`/`BIGTEST` from the FAT (no musl port). FAT 24 → 21 entries. |
| A5.2 | **Remove `SYS_DONIX_SPAWN` (507) and `sys_spawn`** | ✅ Complete | Nothing in the musl canary called 507. |
| A5.3 | **Remove `SYS_OPENDIR`/`SYS_READDIR`/`SYS_CLOSEDIR` (500–502)** | ✅ Complete | musl uses `open(O_DIRECTORY)` + `getdents64`. Also removed the kernel-side `dons_dirent_t`. |
| A5.4 | **Remove `SYS_ARCH_SET_FS` (504)** | ✅ Complete | musl uses `arch_prctl` = 158. |
| A5.5 | **Remove `SYS_DONIX_SBRK` (505)** | ✅ Complete | musl uses `brk` = 12. |
| A5.6 | **Remove embedded newlib shell and fallback** | ✅ Complete | Deleted `user_shell_data.c` (~89 KB), its Makefile rule, and all references in `kmain.c`. Boot path now panics on FAT read failure. |
| A5.7 | **Delete `userland/newlib/`** | ✅ Complete | Whole tree, plus the `userland` phony target, `USERLAND_DIR`, `USER_CFLAGS`. |
| A5.8 | **Remove leftover dead code** | ✅ Complete | `syscall.c`, `DEBUG_WRITE_BOUNCE`, dead CR3 switch in `kmain.c`'s `elfload`, unused `vmm_clone_kernel_half`/`vmm_free_user_page_tables`. |

Complete at `20260926-08`.

### 0.6 — Bugs found and fixed during the migration (6/6)

| # | Milestone | Status | Notes |
|---|-----------|--------|-------|
| A6.1 | **Syscall return path preserved `%r10`** | ✅ Complete | Linux ABI says only `%rax`, `%rcx`, `%r11` are clobbered. musl's stdio keeps a live pointer in `%r10` across `writev`. Fixed at `20260924L`. |
| A6.2 | **`sys_read` on fd 0 returns on first byte, not `count`** | ✅ Complete | POSIX terminal semantics. Fixed at `20260926A`. |
| A6.3 | **`brk` uses Linux absolute-address ABI** | ✅ Complete | Fixed at `20260924B`. |
| A6.4 | **`MSR_FS_BASE` saved/restored/inherited per process** | ✅ Complete | musl reads `%fs:0` on the child's first instruction after `fork`. Fixed at `20260926E`. |
| A6.5 | **`execve` passes `argc`/`argv` in `%rdi`/`%rsi`** | ✅ Complete | newlib's `crt0.S` read them from registers; kept for the migration canary. Fixed at `20260926G`. |
| A6.6 | **`execve`'s argv layout zeroed `argv[1]`** | ✅ Complete | For short `argv[0]` values. Fixed at `20260926H`. |

The 56 working tags used during the migration are recorded in
[`migration-tags.txt`](migration-tags.txt).

---

## 1. Boot & System Initialization (5/5 Complete)

| # | Milestone | Status | Notes |
|---|-----------|--------|-------|
| 1 | **Boot Sector** | ✅ Complete | 16-bit real-mode bootloader, BIOS interrupts, disk loading |
| 2 | **Protected Mode Entry** | ✅ Complete | A20 line, GDT, CR0.PE, 32-bit flat mode |
| 3 | **Basic VGA Console** | ✅ Complete | 80×25 text mode, print routines, cursor control |
| 4 | **Long Mode Entry** | ✅ Complete | PAE paging, PML4/PDPT/PD/PT, IA32_EFER.LME, 64-bit jump |
| 5 | **64-bit Kernel Start** | ✅ Complete | `_start`, stack setup, `kmain` entry point |

---

## 2. Core Kernel Features (65/65 Complete)

Items 6–65 carried forward from dons-os. The **System Calls** row (#19)
has been updated to Linux x86_64 numbers; the **newlib Userland C
Library** row (#29) is marked `dons-os era`.

| # | Milestone | Status | Notes |
|---|-----------|--------|-------|
| 6 | **IDT + ISR Stubs** | ✅ Complete | Exception handlers, interrupt gates, error-code support. All stubs that call C handlers preserve caller-saved registers. |
| 7 | **PIC Remap** | ✅ Complete | IRQ0–IRQ15 mapped to 0x20–0x2F |
| 8 | **PIT Timer** | ✅ Complete | IRQ0 tick counter, scheduling foundation |
| 9 | **Keyboard Driver** | ✅ Complete | IRQ1, scancode set 1, shift/caps, input buffer |
| 10 | **VGA Console Upgrade** | ✅ Complete | Scrolling, cursor control, shell-ready console |
| 11 | **Kernel Shell** | ✅ Complete | Diagnostic console reached via `k` at boot. Commands: help, clear, info, mem, version, reboot, pmmtest, test, vmmtest, serialtest, heapstat, maptest, testrec, heaptest, heapcheck, heapstress, nxtest, syscall, elfload, proclist, proccreate, vmmclone, runproc, schstat, testyield, gdtdump, tssdump, selftest, atatest, fatmount, fatls, fatcat.  The dons-os-era `usershell` command was removed at A5 step 6 (the shell now launches automatically at boot). |
| 12 | **E820 Memory Map** | ✅ Complete | Memory detection, BootInfo struct passed to kernel |
| 13 | **Physical Memory Manager** | ✅ Complete ⭐ v0.5.3 | Bitmap allocator, page alloc/free, reserved region marking. **Reentrancy fix (v0.5.3): `pmm_alloc_page`'s test-and-set is now atomic with respect to a timer IRQ; `pmm_free_page`, `pmm_reserve_page`, `pmm_unreserve_page` also wrapped in cli/sti critical sections.** |
| 14 | **Virtual Memory Manager** | ✅ Complete ⭐ v0.5.4 | Recursive paging at PML4[510]. HHDM mapping at PML4[256]. Dynamic page table allocation. User-space page mapping with PT_USER. **NX bit support via PT_NX, with EFER.NXE enabled so the CPU enforces it (v0.5.1).** **Dynamic HHDM mapping via `ensure_hhdm_mapped()`.** **Deep page table cloning via `vmm_clone_page_table()` (v0.5.3).** **Cross-process write helper `safe_copy_to_user_cr3()` for writing into a child's address space (v0.5.4).** |
| 15 | **Serial Debug Output** | ✅ Complete | COM1 serial output for kernel debugging alongside VGA |
| 16 | **Kernel Heap Allocator** | ✅ Complete ⭐ v0.5.2 | `kmalloc()`/`kfree()` with free list, `heapstat`/`heaptest`. 1 MB initial heap with automatic expansion up to 24 MB. Rewritten in v0.5.2: `heap_extend` places new blocks at the start of the newly mapped region (the old version lost up to 4 KB per extension). Header padded to 48 bytes so payloads are 16-aligned. `heap_validate()` checks every block-list invariant; `heap_stress()` exercises the extension path (grows the heap to ~4.15 MB, zero leak). `heapcheck` and `heapstress` shell commands. |
| 17 | **User Mode (Ring 3)** | ✅ Complete | GDT with user segments (0x33 code, 0x2B data). TSS configured for stack switching. `iretq` transition. CPL=3 with page protection. |
| 18 | **NX (No Execute) Bit Support** | ✅ Complete | PT_NX flag in `vmm.h` (bit 63). NX handling in `vmm_map_page()`. `nxtest` and `vmmtest` verify. |
| 19 | **System Calls (Linux x86_64 ABI in donix)** | ✅ Complete ⭐ v0.5.5 | SYSCALL/SYSRET via MSRs. **Linux numbers, all in `syscall_dispatch`:** `read`(0), `write`(1), `open`(2), `close`(3), `stat`(4), `fstat`(5), `mmap`(9), `mprotect`(10), `munmap`(11), `brk`(12), `rt_sigaction`(13), `rt_sigprocmask`(14), `ioctl`(16), `writev`(20), `getpid`(39), `fork`(57), `execve`(59), `exit`/`exit_group`(60/231), `wait4`(61), `unlink`(87), `arch_prctl`(158), `getdents64`(217), `set_tid_address`(218), `set_robust_list`(273), `getrandom`(318), `rseq`(334). Plus **`SYS_REBOOT` (503, donix-private)**. `syscall` test command. **Safe user-space access via `safe_copy_from_user()`/`safe_copy_to_user()`; cross-process variant `safe_copy_to_user_cr3()`.** The dons-os-private numbering (1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 20, 25) is retired. |
| 20 | **ELF Loader** | ✅ Complete ⭐ FINALIZED | Parses ELF64, maps LOAD segments with permissions, allocates user stack, transitions to Ring 3 via IRETQ, `elfload` command. Works on first boot. Loads ELFs from the FAT volume at runtime via Linux `execve` (59). |
| 21 | **Process Foundation** | ✅ Complete | PCB structure, `process_create`, `proclist`, `vmmclone`. Ready-queue infrastructure. |
| 22 | **BootInfo Fix** | ✅ Complete | Fixed BootInfo alignment between bootloader and kernel. Magic number and version validation. |
| 23 | **Process Stack Setup** | ✅ Complete | Static kernel stack pool. Dedicated user/kernel stacks. `process_destroy()`. `runproc` command. |
| 24 | **Cooperative Scheduler** | ✅ Complete | Ready queue, round-robin. `process_yield()`, `process_exit()`. `context_switch.asm`. `testyield`, `schstat`. |
| 25 | **Preemptive Scheduler** | ✅ Complete | PIT timer (100 Hz) preempts user and kernel mode. Quantum-based slicing. Timer saves/restores per-process kernel frames. |
| 26 | **Segment-Shifting Bootloader** | ✅ Complete | `stage2.asm` reads in 128-sector chunks, advancing segment offsets. Kernel size ceiling lifted to 448 KB (v0.5.1). |
| 27 | **Userland Syscall Reboot** | ✅ Complete | `SYS_REBOOT` (503) maps Ring 3 to a Ring 0 triple-fault reboot. In donix this is the only 500+ syscall number left. |
| 28 | **Kernel-Stack-on-Syscall-Entry** | ✅ Complete ⭐ v0.4.7 | Syscall path runs on a per-process kernel stack. `g_syscall_stack_top` maintained by scheduler in lockstep with `TSS.RSP0`. |
| 29 | **newlib 4.x Userland C Library** | 🔵 dons-os era | Removed at A5 step 7. In donix the userland C library is musl 1.2.5 (see item A0.6 or section 0). |
| 30 | **Blocking `sys_read`** | ✅ Complete ⭐ v0.4.7 | Reads no longer spin the CPU. Process marks BLOCKED, yields via timer, woken by `irq1`. Timer skips blocked processes. |
| 31 | **Boot-Time Shell Choice** | ✅ Complete ⭐ v0.4.7 | 2-second window at boot. `k` → kernel shell, else → user shell. Kernel shell not reachable after boot. |
| 32 | **Userland Heap via `brk`** | ✅ Complete ⭐ v0.4.7 | musl `malloc`/`free` over `brk` → page mapping. Exercised by `memtest`. |
| 33 | **`gdtdump` / `tssdump`** | ✅ Complete ⭐ v0.4.7 | On-demand kernel shell commands to inspect the GDT and TSS. |
| 34 | **Scheduler / TSS / Interrupt ABI Stability Pass** | ✅ Complete ⭐ v0.4.9 | Seven bugs fixed: ELF-load race, TSS lockstep, fallback TSS restoration, kernel stack slot aliasing, `context_switch.asm` offsets, `scheduler_switch_to` preemption window, `irq1_stub` ABI violation. Plus frame validation in `context_switch.asm` and `_Static_assert` layout guards in `process.c`. |
| 35 | **ATA PIO Block Device Driver** | ✅ Complete ⭐ v0.4.10 | Primary-channel ATA in PIO (polled) mode, LBA28. Per-drive read/write entry points. FLUSH CACHE for durability. Write-protect floor on master. Three bring-up bugs found and fixed: PIC mask restoration, exception frame offsets, LBA mode bit. `atatest` diagnostic. |
| 36 | **FatFs Integration** | ✅ Complete ⭐ v0.5.2 | FatFs R0.16 vendored with `diskio.c` shim over the ATA driver. `FF_FS_READONLY = 0`. Dual-drive storage: kernel on master, FAT16 volume on slave. Kernel shell commands `fatmount`, `fatls`, `fatcat`. Long filename support added in v0.5.2. |
| 37 | **Userland File I/O over FatFs** | ✅ Complete ⭐ v0.4.10 | Per-process file table in the PCB. `open`(2), `close`(3), `getpid`(39), and an fd branch in `read`(0). musl `open`/`close`/`read`/`write` work from Ring 3. Verified create/write/close/reopen/read round-trip byte-exact. |
| 38 | **Single-Drive Layout** | ✅ Complete ⭐ v0.5.0 | `FAT_CONFIG=dual|single` selects the layout. FAT16 partition at LBA 2048. `diskio.c` applies `FAT_PARTITION_OFFSET`. `FF_MULTI_PARTITION = 0` means FatFs is not partition-aware, so the offset lives in `diskio.c`, not the BPB. `hdd-single.img` built by `mkfs.vfat --offset=2048 -h 2048` and populated by `mcopy`. |
| 39 | **Persistence Across Reboot** | ✅ Complete ⭐ v0.5.0 | Verified end-to-end: `fstest` writes in one boot, verifies byte-exact after reboot on the same image. Proves writes are durable, not just buffered. |
| 40 | **Config Diagnostic at Boot** | ✅ Complete ⭐ v0.5.0 | `kmain.c` prints a config-specific storage line to VGA and serial (`"Storage: single-drive, FAT@LBA 2048"` vs `"Storage: dual-drive, FAT@LBA 0 on slave"`). Mount failures now appear on VGA, not just serial. |
| 41 | **Self-Test Infrastructure** | ✅ Complete ⭐ v0.5.1 | `selftest` kernel shell command runs 17 tests and prints a pass/fail summary. Coverage: GDT descriptor contents, TSS fields, PMM allocation/free, VMM control registers, page mapping, recursive paging, heap integrity, NX bit in final PTE, syscall entry point, ATA reads, FatFs mount/directory listing, and the three exception handlers (#DE, #PF, #GP). Expected-fault protocol lets the exception tests recover cleanly. |
| 42 | **`EFER.NXE` Enabled (NX on Hardware)** | ✅ Complete ⭐ v0.5.1 | Previously the kernel wrote NX bits into PTEs via `PT_NX` but the CPU ignored them because `EFER.NXE` was clear. `enable_nx()` in `kmain.c` now sets the bit, guarded by a CPUID check. `test_vmm` asserts on it. |
| 43 | **Kernel-Owned GDT (Higher-Half)** | ✅ Complete ⭐ v0.5.1 | Previously the GDT lived in low memory (base `0x101DC`, inside `stage2.asm`'s loaded image). `gdt_init` now builds `kernel_gdt[16]` in `.bss` at a higher-half address, `lgdt`s it, and reloads the segment registers. `gdt_set_tss` writes the TSS descriptor directly. `gdt_fix_user_segments` became a no-op. |
| 44 | **Print Atomicity (Shared Serial/VGA Lock)** | ✅ Complete ⭐ v0.5.1 | Serial and VGA drivers share a single print lock (cli/sti critical section with nesting counter and RFLAGS save/restore). Multi-part boot messages wrap in `serial_lock`/`serial_unlock`. The DonsDOS banner truncation and interleaved boot trace are gone. |
| 45 | **`#DF` through IST1** | ✅ Complete ⭐ v0.5.1 | A dedicated 4 KB stack and an IST entry on the `#DF` gate turn a double fault into a printed diagnostic instead of a silent triple-fault reset. |
| 46 | **Kernel Shell on a Pool-Allocated Stack** | ✅ Complete ⭐ v0.5.1 | The kernel shell is now a real process (`kshell`) with a stack from the kernel stack pool, not a hardcoded address. `0xFFFFFFFF8008FF00` is gone. |
| 47 | **Heap Rewrite and Validator** | ✅ Complete ⭐ v0.5.2 | `heap_extend` places new blocks at the start of the new region. Header padded to 48 bytes. `heap_validate()` checks every invariant. `heap_stress()` grows the heap from 1 MB to ~4.15 MB with zero leak. `heapcheck` and `heapstress` commands; both in selftest. |
| 48 | **Long Filename Support** | ✅ Complete ⭐ v0.5.2 | `FF_USE_LFN = 2`, `FF_LFN_UNICODE = 2`, `FF_CODE_PAGE = 437`. `fatfs/ffunicode.o` linked. `fatls` shows `HELLO-WORLD.TXT`; `fatcat HELLO-WORLD.TXT` reads it. `sys_open`'s path buffer enlarged to 300 bytes; `copy_user_string` helper respects the buffer size. Kernel 158 KB → 163 KB. |
| 49 | **`unlink` (Linux 87)** | ✅ Complete ⭐ v0.5.2 | Wraps FatFs `f_unlink`. Kernel handler `sys_unlink` in `user_syscall.c`; musl `unlink()` calls the syscall directly. `multitest` deletes the files it creates and confirms they are gone. |
| 50 | **Makefile Header Dependency Tracking** | ✅ Complete ⭐ v0.5.2 | `-MMD -MP` in CFLAGS; `-include $(OBJS:.o=.d)`. Header changes now trigger the right rebuilds automatically. This was the root cause of several stale-object debugging sessions. |
| 51 | **Cross-GCC for FatFs (`ff.o`, `ffunicode.o`)** | ✅ Complete ⭐ v0.5.2 | `fatfs/ff.o` and `fatfs/ffunicode.o` compiled with `/opt/cross/bin/x86_64-elf-gcc` at `-O2`. Clang 22.1.8 miscompiles `ff.c` at every level: `-O0` breaks the FILINFO read path, `-O1` hangs in `f_unlink`, `-O2` produces LLD-truncated instructions. GCC produces correct code and links cleanly with the Clang-built kernel. See `LLD_BUG_REPORT.md`. |
| 52 | **`execve(59)` — in-place disk-loaded ELF execution** | ✅ Complete ⭐ v0.5.5 | Opens an ELF file on the FAT volume, replaces the calling process's address space in place. In donix, musl's `execve` wrapper calls Linux 59 directly. |
| 53 | **`wait4(61)` — parent/child synchronization** | ✅ Complete ⭐ v0.5.5 | Blocks the calling process until a matching child exits, then reaps it and returns its exit status. `WNOHANG` supported. |
| 54 | **Process Parent/Child Tracking** | ✅ Complete ⭐ v0.5.3 | `pcb_t` fields `parent_pid`, `exit_status`, `wait_pid`; `PROC_STATE_ZOMBIE`. A process with a parent becomes a zombie on exit and is reaped by `wait4`; a process with no parent reclaims itself immediately. `process_wake_parent_if_waiting` in `process.c`. |
| 55 | **PMM Allocator Reentrancy Fix** | ✅ Complete ⭐ v0.5.3 | `pmm_alloc_page`'s test-and-set on the shared bitmap was non-atomic; a timer IRQ between the read and write could hand the same page out twice. Fixed with cli/sti critical sections using `pushfq`/`pop` to preserve the caller's IF. Applied to `pmm_alloc_page`, `pmm_free_page`, `pmm_reserve_page`, `pmm_unreserve_page`. |
| 56 | **Page-Table Aliasing Fix (Deep Clone)** | ✅ Complete ⭐ v0.5.3 | `vmm_clone_page_table` was a shallow copy: parent and child shared every PDPT, PD, and PT. Any `vmm_map_page_in_cr3` on the child overwrote the parent's PTEs. Fixed with a deep copy of the low-half hierarchy (PML4[0..255]); the high half (HHDM and kernel) remains shared by design. |
| 57 | **`context_switch` Resume-by-Frame Fix** | ✅ Complete ⭐ v0.5.3 | The resume side chose user vs kernel by comparing `next->entry_point` against `KERNEL_BASE`, which is wrong for a process preempted or blocked in kernel mode. Fixed by making the resume side trust the saved frame verbatim and choose the validation rule from the frame's own CS. |
| 58 | **REPL shell (dons-os era)** | 🔵 dons-os era | The newlib REPL shell was removed at A5 step 6. In donix the shell is `musl_sh`, loaded from `0:/MUSL_SH.ELF`. |
| 59 | **Directory syscalls `SYS_OPENDIR`/`READDIR`/`CLOSEDIR`** | 🔵 dons-os era | Removed at A5 step 3. In donix, musl's `opendir` goes through Linux `open(O_DIRECTORY)` + `getdents64`. |
| 60 | **`ls.elf` (dons-os era, ported to musl)** | 🔵 / ✅ | The `ls` program exists in donix as a musl build: it uses `opendir`/`readdir` + `stat` per entry and produces byte-for-byte identical output to the old newlib version. |
| 61 | **argv passing (`execve` + musl `_start`)** | ✅ Complete ⭐ v0.5.5 | Linux `execve`(59) delivers `argc`/`argv` on the child's user stack in the SysV layout; musl's `_start` reads them. The dons-os-era `%rdi`/`%rsi` writing in `sys_execve` is kept but no longer load-bearing for musl. |
| 62 | **`cat` and `echo` (musl)** | ✅ Complete ⭐ v0.5.5 | `cat HELLO-WORLD.TXT` prints the file byte-exact. `echo hello world` prints `hello world`. Both are standalone musl ELFs on the FAT volume. |
| 63 | **`safe_copy_to_user_cr3` (Cross-Process Write Helper)** | ✅ Complete ⭐ v0.5.4 | `safe_copy_to_user` assumes the current process is the target. `sys_execve` writes into a *child* process, so it uses the new `safe_copy_to_user_cr3(uint64_t cr3, ...)` variant that resolves against the target's page tables directly via the HHDM. |
| 64 | **`file_slot_t` Tagged File Table (Files + Dirs Share Handles)** | ✅ Complete ⭐ v0.5.4 | `file_table[]` entries are `file_slot_t*` with a `kind` tag (`FILE_KIND_FILE` or `FILE_KIND_DIR`). `close_all_files`, `sys_open`, `sys_close`, `sys_read`, and `sys_write` all dispatch on the tag. Touched six call sites. |
| 65 | **Dispatcher alignment** | ✅ Complete ⭐ v0.5.5 | In donix every `case N:` label matches its Linux syscall number. The dons-os-era mismatch (`SYS_ARCH_SET_FS` defined as 11 but dispatched as `case 5`) was corrected at v0.5.4 and the case was removed entirely at A5 step 4. |

---

## 3. Memory Management (8/8 Complete)

| # | Milestone | Status | Notes |
|---|-----------|--------|-------|
| 66 | **Higher-Half Kernel** | ✅ Complete | Kernel mapped to `0xFFFFFFFF80100000`, identity map preserved |
| 67 | **Virtual Memory Manager** | ✅ Complete ⭐ v0.5.4 | Recursive paging, HHDM, dynamic page tables, NX, dynamic HHDM mapping, deep page table cloning, cross-process write helper |
| 68 | **Serial Debug Output** | ✅ Complete | COM1 serial output, integrated with QEMU |
| 69 | **Kernel Heap** | ✅ Complete ⭐ v0.5.2 | `kmalloc()`/`kfree()` with free list. Memory reuse verified. Rewritten in v0.5.2 with `heap_validate()` and `heap_stress()`. |
| 70 | **User Memory Mapping** | ✅ Complete | Pages mapped with PT_USER for user/kernel isolation |
| 71 | **NX (No Execute) Bit** | ✅ Complete | PT_NX flag, `nxtest`, NX status in `vmmtest`. Since v0.5.1, `EFER.NXE` is enabled so the CPU actually enforces it. |
| 72 | **HHDM Dynamic Mapping** | ✅ Complete | `ensure_hhdm_mapped()` for on-demand physical memory access. Used by ELF loader and page table cloning. |
| 73 | **`brk` Heap Growth** | ✅ Complete ⭐ v0.4.7 | Per-process heap state in `current->brk_virt`. Pages mapped with `invlpg` after map. In donix the syscall number is Linux 12 with absolute-address ABI. |

---

## 4. Storage & File Systems (9/9 Complete)

| # | Milestone | Status | Notes |
|---|-----------|--------|-------|
| 74 | **ATA PIO Driver** | ✅ Complete ⭐ v0.4.10 | Primary channel, LBA28, polled. Per-drive entry points, FLUSH CACHE. Write-protect floor on master. `atatest` diagnostic. |
| 75 | **FatFs Integration** | ✅ Complete ⭐ v0.5.2 | FatFs R0.16, read and write, long filename support. Kernel shell and userland access. `ff.o` and `ffunicode.o` compiled with the cross-GCC. |
| 76 | **Single-Drive Layout** | ✅ Complete ⭐ v0.5.0 | Boot chain + kernel + FAT16 partition on one `hdd.img`. FAT at LBA 2048. |
| 77 | **Persistence Across Reboot** | ✅ Complete ⭐ v0.5.0 | `fstest` proves writes survive reboot. `fstest --verify` (reachable with musl argv) exercises the cross-boot path. |
| 78 | **Dual-Drive Layout (Retained)** | ✅ Complete | Kernel on master, FAT16 on slave. Remains the default; useful for debugging. |
| 79 | **`unlink` (Linux 87)** | ✅ Complete ⭐ v0.5.2 | Wraps FatFs `f_unlink`. Kernel handler `sys_unlink` in `user_syscall.c`. `multitest` deletes the files it creates and confirms they are gone. |
| 80 | **User programs from disk (`execve`)** | ✅ Complete ⭐ v0.5.5 | Loads ELF files from the FAT volume and runs them. `hello`, `echo`, `cat`, `ls`, `memtest`, `musl_stat`, and the musl test binaries are standalone ELFs on the FAT partition. Only the shell (`MUSL_SH.ELF`) is boot-loaded; everything else runs on demand. |
| 81 | **Directory iteration from Ring 3** | ✅ Complete ⭐ v0.5.5 | musl's `opendir` → Linux `open(O_DIRECTORY)` (2) + `getdents64` (217). `struct linux_dirent64` returned by `sys_getdents64`, one record per call. |
| 82 | **argv passing through `execve`** | ✅ Complete ⭐ v0.5.5 | `argc`/`argv` placed on the child's user stack by `sys_execve`; musl's `_start` reads the SysV layout. `cat` and `echo` consume it. |

---

## 5. User Space & Advanced Features (18/19 Complete)

| # | Milestone | Status | Notes |
|---|-----------|--------|-------|
| 83 | **System Calls** | ✅ Complete ⭐ v0.5.5 | See item 19 for the full Linux x86_64 list. Safe user-space access; cross-process variant. |
| 84 | **ELF Loader** | ✅ Complete ⭐ FINALIZED | ELF64 parsing, user-mode transition, `elfload`, and Linux `execve(59)`-based loading from disk. Works on first boot. |
| 85 | **Process Foundation** | ✅ Complete | PCB, `process_create`, `proclist`, `vmmclone` |
| 86 | **Process Stack Setup** | ✅ Complete | Static kernel stack pool, user/kernel stacks, `runproc`, `process_destroy` |
| 87 | **Cooperative Scheduler** | ✅ Complete | Ready queue, `process_yield()`, `process_exit()`, `testyield`, `schstat` |
| 88 | **Preemptive Scheduler** | ✅ Complete | PIT timer preemption, quantum slicing, timer-driven kernel-mode preemption |
| 89 | **musl 1.2.5 Userland C Library** | ✅ Complete ⭐ v0.5.5 | Static musl linked into user programs. Standard C available in Ring 3. Built from source into `third_party/musl-install/`. Toolchain wrapper at `toolchain/musl-gcc.sh`. |
| 90 | **Blocking I/O** | ✅ Complete ⭐ v0.4.7 | `read` on fd 0 blocks via BLOCKED + `hlt`, woken by `irq1`. |
| 91 | **File I/O from Ring 3** | ✅ Complete ⭐ v0.5.5 | `open`/`close`/`read`/`write`/`unlink` on FAT files from userland, with long filename support. Linux syscall numbers 2, 3, 0, 1, 87. |
| 92 | **User-mode process spawning** | ✅ Complete ⭐ v0.5.5 | `fork`(57) + `execve`(59) + `wait4`(61) from `musl_sh`. |
| 93 | **Process Cleanup on Exit** | ✅ Complete ⭐ v0.4.8 | `process_reclaim` frees ELF pages and user stack pages, marks PCB UNUSED, resets pid, decrements count. `process_exit` runs with interrupts disabled. Page-table teardown deferred. |
| 94 | **Regression harness (musl apps)** | ✅ Complete ⭐ v0.5.5 | The musl programs `hello`, `echo`, `cat`, `ls`, `memtest`, `musl_stat`, `musl_readdir`, `musl_fork`, `musl_exec`, `musl_wait`, `musl_r10probe`, `brk_verify`, `brkraw`, `brkgrow`, `printnum`, plus the tiny `musl_min`/`musl_malloc`/`musl_printf`, cover the syscall surface. All pass in dual-drive and single-drive. |
| 95 | **`musl_sh` (the boot shell)** | ✅ Complete ⭐ v0.5.5 | Reads a line byte-at-a-time, tokenizes, normalizes `argv[0]` to `0:/NAME.ELF`, `fork`s, `execve`s, `wait4`s, loops. There are no built-ins. |
| 96 | **argv (argument passing to user programs)** | ✅ Complete ⭐ v0.5.5 | Kernel writes `argc`/`argv` onto the child's user stack in SysV layout; musl's `_start` reads them. |
| 97 | **Directory listing (`ls`)** | ✅ Complete ⭐ v0.5.5 | Real `ls` via musl `opendir`/`readdir` + `stat` per entry. |
| 98 | **`cat` and `echo`** | ✅ Complete ⭐ v0.5.5 | First argv-consuming musl userland programs. `cat FILE`, `echo words...`. |
| 99 | **Slab Allocator** | ❌ Not Needed | Free list already provides memory reuse for kmalloc/kfree |
| 100 | **Per-Process tty / Console Focus** | ☐ Not Started | Prerequisite for multiple concurrent shells. Not urgent until there's more than one shell. |

---

## Summary

| Phase | Completed | Total | Progress |
|-------|-----------|-------|----------|
| donix musl migration (v0.5.5) | 50 | 50 | **100%** ✅ |
| Boot & System Init | 5 | 5 | **100%** ✅ |
| Core Kernel | 65 | 65 | **100%** ✅ |
| Memory Management | 8 | 8 | **100%** ✅ |
| Storage & File Systems | 9 | 9 | **100%** ✅ |
| User Space | 18 | 19 | **95%** 🚧 |
| **Overall** | **155** | **156** | **99%** |

Everything that was tracked as a "capability" is now at 100% except
User Space, where per-process tty / console focus remains the last
item. Everything else is either a follow-up refinement (ELF loader
`PT_NX`), an alternative console path (serial console debug access), a
new subsystem (framebuffer, VFS), a testing-infrastructure improvement
(boot-time self-test mode, `make test`, spawn regression, argv/REPL
regression tests), or a shell feature (pipes, redirection, `cd`,
environment variables).

For the milestone-by-milestone narrative (what each version fixed, and
why), see [`ROADMAP.md`](ROADMAP.md).

---

## Next Steps (Recommended Order)

1. **Phase B — busybox / coreutils against musl.** The immediate next
   milestone. Build a static busybox against the project-local musl
   and run it. Watch the `Unknown syscall: N` output for the next
   batch of unimplemented syscalls. See [`ROADMAP.md`](ROADMAP.md)
   section 6.
2. **`fcntl` (Linux 72).** musl's `opendir` calls `fcntl(fd, F_SETFD,
   FD_CLOEXEC)` and ignores the failure. busybox will likely call it
   more. A minimal stub (return 0 for `F_SETFD`/`F_GETFD`, `-1`
   otherwise) removes the noise and unblocks a common code path.
3. **`sys_newfstatat` (Linux 262).** Not on any current test's path,
   but busybox will reach it via `fstatat(fd, path, st, flags)`.
4. **`sys_open` `O_DIRECTORY` fix.** `ls 0:/hello-world.txt` prints
   an empty listing and exits 0 instead of failing. Fix in `sys_open`'s
   fallback path: after `f_opendir` succeeds, check `fattrib & AM_DIR`;
   if not set, close and return `-ENOTDIR`.
5. **`isr14_handler` user-mode fault handling.** Currently a user-mode
   `#PF` halts the console instead of terminating the faulting process.
   This will bite the first time busybox segfaults.
6. **`fstest --verify` cross-boot persistence test.** Reachable now
   that argv reaches the child; run `fstest`, reboot, `fstest --verify`.
7. **Spawn and argv regression tests.** A kernel-mode self-test child
   that opens `0:/HELLO.ELF`, spawns it, waits for it, and asserts exit
   status 0; plus scripted REPL tests for `echo hello world`,
   `cat HELLO-WORLD.TXT`, and `fstest --verify` after a reboot.
8. **Boot-time self-test mode and `make test`.** Runs the existing 17
   tests at boot and halts; wrapper boots QEMU headless and greps the
   serial log.
9. **Pipes and redirection.** `cat file > out.txt`,
   `cat file | grep foo`. Needs `pipe(2)` and `dup2(2)`.
10. **`cd` and relative paths.** `chdir` + per-process cwd. FatFs
    already supports `f_chdir`.
11. **Environment variables.** Extend the argv mechanism with an
    `envp` array; `getenv`/`setenv` on the userland side.
12. **Kernel log routing.** Route `sys_execve`'s and `sys_open`'s log
    lines to serial only, or add a `SYS_KLOG(level)` syscall.
13. **ELF Loader `PT_NX` follow-up.** With `EFER.NXE` now enabled,
    mark non-executable segments (data, BSS, user stack) with `PT_NX`.
14. **Page-table teardown on process exit.** Walk the process's page
    tables and free the user-space portion.
15. **Serial Console Debug Access.** Kernel shell over COM1.
16. **Per-Process tty / Console Focus.** Prerequisite for multiple
    concurrent shells. Also the natural point to build the ring-buffer
    console.
17. **Framebuffer Graphics.** Move from VGA text mode to graphics.
18. **File System (VFS).** VFS layer above FatFs, with mount points
    and path resolution.

---

*Last Updated: September 2026 (donix v0.5.5)*
