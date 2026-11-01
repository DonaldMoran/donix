# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-01 (late session). **`v0.6.18` is released.**
Work is pre-`v0.6.19`. Baseline is `d1ae213`
(`tag: 20261031-ctrlz-owner-fix`), working tree clean, build green
(`MAKE_EXIT=0`). **`dev` local is 68 commits ahead of `origin/dev`
(`799e98f`, `v0.6.18`) and has NOT been pushed.** `origin/dev` must
not be force-pushed without first reading those 68 commits; they are
intentional work, not accidental divergence.

**Baseline builds. Baseline is NOT "green" as a test run.**
`canary --full` on `d1ae213` crashes with a `#PF` on a live user page
(see below). The old handoff's claim that `d1ae213` is green is
**false**; it was never re-run on a clean checkout in the previous
session, only assumed.

---

## THE PRIMARY OPEN BUG: `#PF` on a live user page after `^Z`/`fg`

`canary --full` **passes 28/28 without `^Z`** on a clean `d1ae213` tree.
The crash requires at least one `^Z`/`fg` cycle. When it fires, it
kills canary (or a `busybox` test child) with a `#PF` on a page of the
process's own `.text`, and the PTE at the faulting VA is 0.

### Symptom (verbatim from `d1ae213` + handoff commit, one `^Z` then `fg`)

    === PAGE FAULT (#PF) ===
      CR2 (Bad Address) : 0x0000000000400C07
      Faulting RIP      : 0x0000000000400C07
      Raw Error Code    : 0x0000000000000014
      CS                : 0x0000000000000033
      RSP               : 0x00000080000FBD08
      CR3               : 0x0000000000347000     <- child CR3 or canary's
      Walk indices: pml4=0 pdpt=0 pd=2 pt=12
      pde               : 0x000000000034B027   (present)
      pte               : 0x0000000000000000   (NOT PRESENT)

- **CR2 = RIP** — instruction fetch.
- **CS = 0x33** — user mode, so the process was running normally.
- **PTE = 0 with PDE present** — an explicit unmap of a live code page.
- **CR3 varies** (`0x33B000` = canary, `0x347000` = a child). The
  faulting VA varies (`0x413962`, `0x40C0B8`, `0x400C07`, …) but is
  always inside the process's own `.text` (`0x40xxxx`–`0x41xxxx`).
- **PDE at fault time gives the PT phys** (`0x33F000` for the
  `pt[19]` walk, `0x347B000` for `pt[12]`).

### Second, separate fault seen in the same run

Later in the same `canary --full` sequence, `sigsuspend_probe` faults
with:

    CR2 = 0x000000000000003D
    RIP = 0x00000000004051F8
    pte = 0x0000000000000003   (PRESENT, phys 0x00000000)
    CR3 = 0x0000000000347000

`CR2 = 0x3D` is a small integer — a **NULL pointer plus offset**, not
a user VA. The PTE is present but points at phys page 0. This is a
**distinct bug** in `sigsuspend_probe` / `sys_rt_sigsuspend`'s ABI,
unrelated to the `.text` PTE zeroing. Do not conflate the two when
triaging.

### What we PROVED on 2026-11-01 (do not re-derive)

1. **`canary --full` passes 28/28 without `^Z`** on a clean
   `d1ae213` + handoff commit tree, no instrumentation. Verified
   once, cleanly, by hand.

2. **`process_reclaim` is NOT the caller.** A guard was added to
   `process_reclaim` refusing to reclaim any PCB that is
   `current_process` or whose `state` is not `ZOMBIE`/`TERMINATED`.
   The guard did **not** fire, and the crash still happened. So
   `process_reclaim(canary)` is not the mechanism.

3. **`elf_page_list` does not contain page-table pages.** Every one
   of the six `elf_add_page_to_pcb` callers (`elf.c:190`,
   `user_syscall.c:5419, 7581, 9733, 11299`, `process.c:401`) passes
   a `phys` returned by `pmm_alloc_page_for_elf()`, i.e. a **leaf**
   page. `sys_brk` and `sys_mmap` follow the correct pattern:
   `phys = pmm_alloc_page_for_elf(); vmm_map_page_in_cr3(..., phys,
   ...); elf_add_page_to_pcb(self, phys);`. The `vmm_map_page_in_cr3`
   page-table pages are never returned to the caller. So
   `exec_free_and_unmap_user_pages` and `process_cleanup_elf_pages`
   **cannot** free the faulting PT page via `elf_page_list`.

4. **`vmm_unmap_page_in_cr3` is not called on canary's `.text`.** A
   gdb breakpoint on `vmm_unmap_page_in_cr3` during a `canary --full`
   `^Z`/`fg` run logged only VAs in the child's stack
   (`0x80000f6000`–`0x80000ff000`) and mmap region
   (`0x8010001000`, `0x8010002000`), with `cr3 = 0x347000`. **No
   `0x413xxx` or `0x40cxxx` VA was ever passed.** The crash is not
   caused by an explicit unmap call.

5. **`pmm_free_page(0x33F000)` fired exactly once** in an
   instrumented run: from `process_reclaim(canary)` via
   `process_free_clone`. In the plain (uninstrumented) run, the crash
   happens with **no `pmm_free_page(0x33F000)` at all** (the guard
   didn't fire and no `[FREE-33Fxxx]` line was logged because the
   plain run has no such instrumentation).

6. **`exec_churn` passes 24 rounds of fork/execve/wait4** — plain
   fork/exec is not the trigger.

7. **`gdb` masks the crash.** With gdb attached, the guest is slowed
   enough that `^Z`/`fg` never triggers the fault, even with the
   exact repro. **Do not rely on gdb for this bug** — it changes the
   timing that makes the bug visible.

8. **`^Z` loses pipe wakeups on canary.** From a race_probe dump:
   `pid=4 wake_tries=28 wake_ok=0 refuse=r3=32`. Canary is blocked
   in `BLOCK_KIND_PIPE_READ` while stopped, and 28 pipe-write wakes
   are refused because `waiter->state != PROC_STATE_BLOCKED` (it is
   `STOPPED`). Canary re-checks the pipe on `fg` and reads the
   pending data, so this is a **latency bug, not a data-loss bug** —
   but it is real and separate from the `.text` PTE fault.

### The remaining hypothesis: `pmm_bitmap` corruption

The fault-page PT (`0x33F000` for the `pt[19]` walk) is **canary's
own PT** for the `0x40F000`–`0x40FFFF` range. Its PTEs are
individual pages of canary's `.text`. For `pt[19]` (or `pt[12]`) to
be zero, one of two things happened:

- **The whole PT page was `memset` to zero** by
  `vmm_clone_page_table` (`vmm.c:329`) or `vmm_map_page_in_cr3`
  (`vmm.c:164`, `vmm.c:463`), then refilled with a *different*
  process's PTEs. Canary's PD entry still points at `0x33F000`, but
  `pt[19]` is now whatever the new owner wrote there — probably 0.

- **A single PTE at `pt[19]` was zeroed directly** by
  `vmm_unmap_page_in_cr3` or `vmm_unmap_page` — **ruled out** by
  fact #4 above.

The remaining path is the first: **`pmm_alloc_page_for_tables`
returned `0x33F000` as a new PT page**, even though canary's PD
still referenced it. That can only happen if `bitmap_test(0x33F)`
returned 0 inside `pmm_scan_zone`.

The bitmap for page `0x33F` is byte `0x67F`, bit 7 of
`pmm_bitmap`. **For that bit to be 0, `pmm_bitmap[0x67F]` must have
been written with bit 7 clear.** The only legitimate writers are
`bitmap_set`, `bitmap_clear`, and `pmm_init`, all in `pmm.c`. None
of them should touch `0x67F` for a page that is not explicitly being
allocated or freed.

**The hypothesis:** a stray write (buffer overflow, wild pointer,
stack overflow from a user-triggered syscall) writes a 0 bit into
`pmm_bitmap[0x67F]`. Then `pmm_scan_zone` sees `0x33F000` as free,
`pmm_alloc_page_for_tables` returns it for a new PT, and
`vmm_map_page_in_cr3`/`vmm_clone_page_table` `memset`s it to zero.
Canary's `.text` PTEs are wiped. Canary executes `.text` and faults.

### Next instrumentation (fresh session, do this FIRST)

Add to `pmm.c`:

1. In `bitmap_clear(uint64_t page)`: if `page == 0x33F`, print
   `[BITMAP_CLEAR_33F] caller=0x...` with `__builtin_return_address(0)`.
2. In `bitmap_set(uint64_t page)`: if `page == 0x33F`, print
   `[BITMAP_SET_33F] caller=0x...`.
3. In `pmm_alloc_page`, when a page is returned for
   `PAGE_PAGE_TABLE`, print `phys` if `phys == 0x33F000`. Also print
   if `phys == 0x340000` (the adjacent `pt`-range page) as a
   cross-check.

Then run `canary --full` with `^Z`/`fg` (no gdb — gdb masks it) and
look at the serial log.

- If `[BITMAP_CLEAR_33F]` fires → the caller of `bitmap_clear(0x33F)`
  is the bug. The address names the function.
- If only `[BITMAP_SET_33F]` fires → page `0x33F` was set (allocated)
  while already canary's PT; the caller that re-set it is the bug.
- If `[PAGE_TABLE_33F000]` fires but neither bitmap call does → the
  bitmap was corrupted silently (stray write) and we need to find
  the writer.
- If none fire → the mechanism is even more subtle. Look at
  `pmm_scan_zone` and whether the page-index arithmetic could alias.

**No patch until one of these prints.** The bitmap corruption is the
only remaining candidate, and the instrumentation is what confirms or
refutes it.

### What NOT to retry

- **Do not** re-add a `process_reclaim` guard (tried, didn't fire).
- **Do not** re-add top-of-tick `signal_deliver_frame_iretq` (tried,
  neutral).
- **Do not** re-add the `rsp == 0` guards in `scheduler.c` /
  `process.c` / `user_syscall.c` (tried, turned the fault into a `Z`
  halt and made it harder to diagnose).
- **Do not** re-add the `sys_munmap` owned-check (tried, did not fix).
- **Do not** use gdb for this bug (masks the timing).
- **Do not** assume `^Z` is optional. The 2026-10-31 handoff's
  "Run 3 crashed without `^Z`" was one data point; today's clean
  run passed 28/28 without `^Z` on the exact same tree.

### Files to check in the fresh session

- `pmm.c`: `bitmap_clear`, `bitmap_set`, `pmm_alloc_page_for_tables`,
  `pmm_scan_zone`, `pmm_page_info` bookkeeping.
- `vmm.c`: `vmm_clone_page_table` (`memset` at `vmm.c:329`),
  `vmm_map_page_in_cr3` (`memset` at `vmm.c:164`, `vmm.c:463`).
- `process.c`: `process_create`, `process_free_clone`,
  `process_cleanup_elf_pages`, `process_reclaim`.
- `elf.c`: `elf_add_page_to_pcb` (defensive check: reject if `phys`
  is inside the page-table range or a known PT).

The `.bak-before-*` files from 2026-11-01 are all reverted. The tree
at `23826a8` (== `d1ae213` + this handoff) is the clean starting
point.

## THE OTHER OPEN BUG: `^C` -> the console dies after `/ #`

**Unchanged since 2026-10-31.** The `^C` work was not part of today's
investigation. The `^C` bug and its WIP patch are unchanged from the
previous handoff. **This section is verbatim from the old handoff,
with the timestamp updated.**

`^C` is **not fixed**. Everything tried is in
`/tmp/donix-ctrlc-wip-20261031-1330.patch` (94 lines) and
`/tmp/donix-ctrlc-wip-status.txt`. The tree is reverted to `d1ae213`
so the OS boots; `^C` does nothing on the committed tree.

### What we PROVED on 2026-10-31 (do not re-derive)

- **`^C` -> SIGINT delivery WORKS.** The foreground child dies in every
  variant tried.
- **The console dies AFTER the shell prints `/ #`** — keyboard dead —
  in every delivery variant (sys_kill in IRQ1, sys_kill in timer,
  pure sig_pending bit-set).
- **`^Z` on a foreground child WORKS**, and `^Z` also calls
  `sys_kill(-fg,20)` from `irq1_handler`. So "IRQ handler + switch"
  is **not** the trigger.
- The one consistent difference: **`^C` TERMINATES the child**
  (`signal_terminate_current` -> `process_exit`); **`^Z` STOPS it**.
  The console death **correlates with a child `process_exit` /
  termination via a signal**.

### Next action for `^C` (unchanged from 2026-10-31)

**gdb at the `^C` lockup.** At the hang, run:

    gdb -q -ex 'target remote :1234' \
        -ex 'info registers rip rsp cs cr3' \
        -ex 'x/40gx $rsp' -ex 'detach' -ex 'quit'

Read the kernel stack. The answer ("CPU is in `process_exit`" vs "in
a fault" vs "idle / `sys_poll` `hlt`") dictates the fix. Do not
re-attack `irq1_handler`'s delivery path; the suspect is child
`process_exit` interacting with the console wake.

**Possible relationship to `#PF`:** if the `#PF` bug (VMM teardown
during fork/exec) also affects the child-terminate path (which uses
VMM teardown), the `^C` console death may be a downstream symptom of
the same root cause. **Do not assume this.** Chase `#PF` first, and
re-test `^C` on the fixed tree; if `^C` is fixed as a side effect,
great; if not, its gdb investigation stands as its own bug.

---

## Working style -- how this project gets changed (READ THIS)

**One change at a time.** A "change" is a commit, verified by
**RUNNING** before the next starts. A build passing is not a test
passing.

**This session's hard lesson (2026-11-01):** we stacked five unproven
theories on one tree (context_switch, scheduler guards, top-of-tick
signals, `sys_munmap` owned-check, plus assorted diagnostic edits) and
none of them fixed the `#PF`. **Every one had to be reverted.** The
correct sequence was: (1) save state, (2) gdb at the crash, (3) read
the stack, (4) one fix. We did not do (2) and spent the session
guessing.

**A handoff claim is not evidence.** The old handoff said `d1ae213`
was green; it is not. **Re-run every repro on a fresh boot and read
the bytes before acting.** Today's clean-checkout tag test is the
model: fresh `cp -a`, `git clean -xdf`, fresh `musl-install`,
`MAKE_EXIT=0`, run. If it crashes, the "baseline is green" claim is
false and the handoff is wrong.

**Ask for source you do not have** before patching a file not seen
this session.

**A command block is one paste -- write it to a file first.**
`cat > /tmp/x.sh <<'SCRIPT_EOF' ... SCRIPT_EOF` then run it as a
**separate** paste. Never paste a multi-line block with trailing
backslashes into an interactive shell.

**Python edits with `count != 1` abort checks.** `set -e` at top.
Count in Python, not `grep`. Anchor from the file's current bytes
(`cat -A`), open `rb`/`wb` to preserve line endings. If an anchor
aborts, **ask for the current bytes** -- do not guess a second anchor.

**The build is TWO steps:**
`make -C 04_kernel_64bit FAT_CONFIG=single`;
`make -C 05_boot_kernel64 hdd-single.img`.
Verify the mtime chain `source` < `.o` < `kernel.elf` < `kernel.bin` <
`hdd-single.img`.

**Test harness.** `./run` (no arg) = clean build + fresh
`hdd-single.img` + `-enable-kvm -cpu host -display sdl`, no freeze,
gdb stub on `:1234`, `capture.txt` written (overwritten each run --
**copy it aside after any failure**). The Makefile run targets use
`-cpu max` + TCG (a different CPU) -- state which launcher was used.
Default cwd `/root` (prompt `~`).

**In-guest gate:** `cd /`, then `canary --full` (28/28) and
`/root/scripts/test.sh` (57/57).

**Do not** `git reset --hard` without stashing; **do not** edit
`third_party/`; **do not** commit `kmain.c`'s `pit_init` knob; **do not**
call a race closed on one clean run.

**Do not** assume a commit fixed something because its message says
it did. `f91d31a` claims to fix canary/pipe7e/ctrl-Z; it does not.
Test every commit before trusting its message.

**Do not push `dev` without a full read of the 68 unpushed commits.**
`origin/dev` is at `799e98f` (`v0.6.18`). Local `dev` is 68 ahead.
That is intentional work, not accidental divergence. Force-pushing
would destroy it.

---

## Releases

**`v0.6.15` through `v0.6.18` are released.**
`origin/dev` = `799e98f` = `v0.6.18`. Local `dev` = `d1ae213` = 68
commits ahead (unpushed).

**Scratch tags present, not on `dev`:**
- `fixA-stable` = `2c0df3b` (sys_poll revert; STOP/CONT self-pick)
- `20261101-stable-scheduler-fixes` = `f91d31a` (scheduler guards;
  turns `#PF` into `Z` halt; not a fix)

Both are preserved as tags; they can be cherry-picked or merged later
if the scheduler work is independently valuable, but they are **not**
on `dev` because they don't fix the `#PF`.

**Release procedure (on `dev`, pre-release):** (1) patch docs + this
file, commit; (2) bump the version string in `kmain.c`, commit;
(3) `git tag -a vX.Y.Z -m "<summary>"`; (4) drop scratch tags;
(5) `git push origin dev` then `git push origin vX.Y.Z` (by name --
never `--tags`); (6) `git checkout main && git merge --no-ff dev &&
git push origin main && git checkout dev`.

**Current release state:** `v0.6.18` released; work pre-`v0.6.19`;
`dev` is 68 commits ahead of `origin/dev` (unpushed). No new release
until the `#PF` is fixed.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 ABI, natively. Newlib is gone. Userland
is `userland/musl/`. Phase B (busybox) well underway: interactive
shell, pipelines, redirection, job control on the framebuffer/PS-2
console, shebang / `/bin/sh` in; applets include `bc`, `dc`, `diff`,
`du`, `awk`. Full strategy `docs/strategy.md`; direction `ROADMAP.md`;
concurrency rules `docs/contract.md`.

---

## Where we are

### Baseline (committed, `d1ae213`, tree clean, builds green)

- `make clean && make` → `MAKE_EXIT=0`
- **`canary --full` is NOT green.** It crashes with `#PF` on a live
  user page, independent of `^Z`. See "THE PRIMARY OPEN BUG" above.
- `^Z` on a foreground child → `[1]+ Stopped`; `jobs` lists it; `fg`
  resumes. Works.
- `^C` → does nothing (original behavior). Unchanged from 2026-10-31.
- `exec_churn` → 24 rounds pass. This is the one clean baseline probe.

### State on disk / tree

- Kernel sources `04_kernel_64bit/`. `04_kernel_64bit/fatfs/ff.c` is
  **tracked**, built by the cross-GCC, not `third_party/`.
- `third_party/` is **gitignored** -- do not commit edits to it.
  `third_party/busybox/` has `busybox` (stripped) and
  `busybox_unstripped` + `.map` if a userspace symbol is ever needed.
  **`third_party/musl-install/` is gitignored and is needed for the
  build.** If it's missing after a `git clean -xdf`, copy it back from
  a working tree, or rebuild it with `toolchain/install_musl.sh`.
- After a fresh image: `/bin/busybox` and `/bin/sh` are staged; run
  `/bin/busybox --install -s /bin` (absolute path) to get applet
  symlinks. Manual step, not part of the build.
- `capture.txt` is gitignored, overwritten each `./run` -- copy aside
  after any failure.
- Saved artifacts from this session:
  - `~/donix-20261101-sys_munmap-owned-check.patch` — 41-line
    `sys_munmap` fix. Correct-to-have, does not fix `#PF`. Apply as
    its own commit with its own gate pass.
  - `~/donix-20261101-sys_munmap-owned-check.patch` is the **only**
    file left from today's edits. Everything else was reverted.
  - No `context_switch.asm` revert, no scheduler guards, no
    top-of-tick signal delivery — those were all reverted. Do not
    reapply them without a gdb-driven reason.

### Committed this session

**Nothing.** All edits were either saved as a patch (the `sys_munmap`
one), tried and reverted, or tried and abandoned. `dev` is at
`d1ae213`, unchanged from the start of the session. That is
deliberate: nothing was fixed, so nothing was committed.

### Open issues (top)

1. **`#PF` on a live user page in fork/exec context** (above). Next:
   gdb at the `#PF`, read the stack, then one fix. Do NOT re-derive
   the fork/exec shapes in "REFUTED SHAPES".
2. **`^C` -> console dies after `/ #`** (above). Next: gdb at the
   `^C` lockup; suspect child `process_exit`, not the keymap.
3. `sys_munmap` unconditional `pmm_free_page` bug — saved as a patch,
   apply as an independent commit.
4. `open-issues.md` item 5: SSE state not saved.
5. `open-issues.md` item 6: no privilege model.
6. `process_dump_all` `DETACHED` state may mask a stranded process.
7. `kill(pid <= 0)` group semantics: `kill(-pgid, sig)` works;
   `kill(0, sig)` returns `-EINVAL`.
8. Ctrl-C on the serial console is not mapped to SIGINT (host
   `intr = ^C` eats it; and the framebuffer `^C` is bug 2 above).

Plus: busybox batch 2; PIPE7E soak at 100 Hz; `run`'s `stty susp undef`
restore (`stty -g` save / `stty "$old"` restore); interpreter-argv
proof.

### REFUTED SHAPES (do not retry)

1. `cli`-depth lock around the ATA transfer -- crashed on boot.
2. `f_sync` on inherited `FIL`s.
3. `f_opendir("/tmp")` + `f_opendir("/")` warm-up.
4. `user_syscall_clear_file_table(self)` alone.
5. Removing `strip_dot_prefix(exec_path)` -- real latent bug, not the
   `&` bug; one-line cleanup for later.
6. `f_mount(NULL)` + `f_mount(fs)` in `sys_execve`.
7. `f_read`/`f_write` `winsect = -1`.
8. `move_window` in `dir_sdi`/`dir_next` tails -- broke boot.
9. `sys_fork` giving a `FILE_KIND_DIR` child its own `DIR`
   (`bc7bef1`, reverted `950d4f0`).
10. `follow_path` reading the window for `dp->sect` before the
    sub-directory descent (`4594038`, reverted `89c7f3f`).
11. Invalidating `winsect` alone at the mount fast-path return, without
    `wflag = 0` (`b5c5075`, corrected `68185de`).

**`^C`, refuted on 2026-10-31 (all in the WIP patch, none closed it):**

12. `sys_kill(-fg,2)` in `irq1_handler` -> console dies after `/ #`.
13. `sys_kill(-fg,2)` in `timer_preempt_handler` -> console dies.
14. Pure `sig_pending` bit-set from the timer (no `sys_kill`) ->
    console dies.
15. Deferring the raise to `syscall_dispatch` -- never ran (the shell
    parks in `sys_poll` `hlt`, no syscall).
16. Orphan reparent to the shell / to pid 1 / to pid 1 + terminate,
    and skip-`current_process` in the reparent loop -- each changed
    *where* it hung, none fixed it.
17. Double-EOI removal, 8042 drain at the top of the handler, and
    `sys_poll` park via `process_yield` -- none fixed `^C`; the
    `process_yield` park also lost the wake.

**`#PF`, refuted on 2026-11-01 (all reverted; none fixed `#PF`):**

18. Removing the `.resume_rsp_zero` frame-consume in
    `context_switch.asm` (`mov qword [r12+0x110], 0` after `mov rsp,
    [r12+0x110]`). Did not fix `#PF`. **Not restored — the `Z` halt
    was observed on `f91d31a` with the frame-consume in place; the
    frame-consume is not what zeroes canary's code PTE.**
19. Removing the `rsp == 0` guards in `process.c` (`process_wake_all_blocked`,
    `process_wake_parent_if_waiting`), `scheduler.c`
    (`scheduler_ready_queue_add`), and `user_syscall.c`
    (`pipe_wake_waiter`). Did not fix `#PF`; turned the `.resume_rsp_zero`
    sentinel into a live halt. **Do not remove these guards without
    a gdb-driven reason** — they protect against dispatching a
    frame-less process.
20. Adding top-of-tick `signal_deliver_frame_iretq(current, stack_pointer)`
    to `timer_preempt_handler` for SIGINT/SIGTERM/SIGKILL/SIGHUP.
    Did not fix `#PF`. **Not related to the crash.** (The change was
    architecturally defensible but empirically neutral, and it is not
    the bug.)
21. `sys_munmap` owned-check (only `pmm_free_page` if the frame is in
    `self->elf_page_list`). **Applied, built, ran, still crashed with
    `#PF`.** Saved as `~/donix-20261101-sys_munmap-owned-check.patch`.
    Correct-to-have; apply separately. **Did not fix `#PF`.**

**`^Z`-specific refuted on 2026-11-01:**

22. Treating `^Z` as the trigger. **Refuted by Run 3**: a plain
    `canary` (no `--full`, no `^Z`) crashed with `#PF` on the same
    class of VA. `^Z` is a race-window widener, not the trigger.

---

## NEXT (in order)

1. **gdb at the `#PF`** on `d1ae213` (baseline, no `^Z`). Run
   `canary --full` (no `^Z`) with gdb stub on `:1234`; when the `#PF`
   frame dump appears on serial, attach:

       gdb -q -ex 'target remote :1234' \
           -ex 'info registers rip rsp cr3 cr2' \
           -ex 'x/40gx $rsp' \
           -ex 'x/20i $rip' \
           -ex 'detach' -ex 'quit'

   And separately, at the fault handler breakpoint:

       break *<fault_handler_symbol>
       (or) break *<isr14_common> — whichever symbol the linker map names

   Inspect `current_process->pid`, `current_process->cr3`,
   `current_process->state`, `current_process->elf_page_list`,
   `current_process->elf_num_pages`. **Read the bytes first. No patch
   until the stack says what function is on it.**

2. If the fault is in `sys_munmap` or `exec_free_and_unmap_user_pages`,
   add a `serial_print` at the top of each that prints `self->pid` and
   the `va` being unmapped, run `canary --full`, and check whether
   `va` in `0x40xxxx` is being unmapped for a process that still owns
   the page.

3. If the fault is elsewhere (idle, scheduler, signal), the stack
   will say which. **One fix, one commit, one verification.**

4. **After `#PF` is fixed**, re-test `^C` on the fixed tree before
   re-opening the `^C` WIP. If `^C` is fixed as a side effect (VMM
   teardown was the mechanism), note it; if not, `^C`'s own gdb
   investigation stands.

5. **After both are fixed**, apply the `sys_munmap` owned-check patch
   as its own commit with its own gate pass.

6. Then: SSE (item 5), privilege model (item 6), busybox batch 2,
   PIPE7E soak, `run`'s `stty` restore, interpreter-argv proof.

---

## One-line summary

**`v0.6.18` is the last release. `dev` is at `d1ae213` (`20261031-ctrlz-owner-fix`), tree clean, builds green — but `canary --full` crashes with a `#PF` on a live user page, independent of `^Z`, in a fork/exec context. This is the primary open bug. The `^C` bug is unchanged from 2026-10-31. The 2026-10-31 handoff's "baseline is green" claim is false; `d1ae213` crashes and does not pass `canary --full`. The next session must start with gdb at the `#PF`, not with more code reading — five wrong hypotheses were tried on 2026-11-01, all reverted, all refuted. The `sys_munmap` owned-check patch is saved and correct-to-have but does not fix the `#PF`. The scheduler work in `2c0df3b` and `f91d31a` is preserved as tags, not on `dev`, because it does not fix the `#PF` and turns it into a `Z` halt when applied.**
