# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-01 (late session). **`v0.6.18` is released.**
Work is pre-`v0.6.19`. Baseline is `80d272b`, working tree clean, build
green (`MAKE_EXIT=0`). **`dev` local is 69 commits ahead of `origin/dev`
(`799e98f`, `v0.6.18`) and has NOT been pushed.** `origin/dev` must
not be force-pushed without first reading those 69 commits; they are
intentional work, not accidental divergence.

**The writer of the `#PF` is FOUND.** See "THE `#PF` BUG: WRITER FOUND"
below. The bug is a PMM bitmap corruption: `pmm_alloc_page_for_tables`
returned `0x33F000` (canary's live `.text` PT) to `vmm_clone_page_table`
during a `sys_fork`, and the `memset` zeroed canary's `pt[19]`.

**Baseline builds. Baseline is NOT "green" as a test run.**
`canary --full` on `80d272b` crashes with the `#PF` described below,
after at least one `^Z`/`fg` cycle. The old handoff's "baseline is
green" claim was false; it was re-tested this session and confirmed
false.

---

## THE `#PF` BUG: WRITER FOUND

### The watchpoint result (verbatim)

    Hardware watchpoint 1: *(unsigned long long *)(0xFFFF80000033F000 + 19*8)
    Old value = <unreadable>
    New value = 132862015
    0xffffffff80105876 in vmm_clone_page_table+534
    RIP: 0xffffffff80105876  <vmm_clone_page_table+534>
    CR3: 0x32f000
    Backtrace:
    #0  vmm_clone_page_table+534
    #1  process_create
    #2  sys_fork
    #3  syscall_dispatch
    #4  user_syscall_entry

### What it means

`sys_fork` -> `process_create` -> `vmm_clone_page_table` wrote `pt[19]`
of **physical page `0x33F000`** -- canary's live `.text` PT -- while
running in the **parent's** cr3 (`0x32F000`). The write is a
`memset(new_pt, 0, PAGE_SIZE)` (or the copy that follows it) inside
`vmm_clone_page_table`'s deep-copy of the low half.

`vmm_clone_page_table` allocates a **new** PT with
`pmm_alloc_page_for_tables()`, memsets it to zero, and copies the
parent's leaf PTEs into it. If `pmm_alloc_page_for_tables()` returns
**`0x33F000`** -- a page that is still a live PT in canary's address
space -- the `memset` zeroes canary's `pt[19]`, and canary's next
instruction fetch at `0x413966` (or whichever VA is on that page) faults
with `pte = 0`.

**This is the original "pmm_bitmap corruption" hypothesis, proven.**
`pmm_bitmap[0x67F]` bit 7 (page `0x33F`) was **clear** when
`pmm_alloc_page_for_tables` ran, even though canary's PD still pointed
at `0x33F000`. The PMM handed the page out as free; the clone zeroed it.

### Why earlier probes missed it

- `[PT_ALLOC_33F]` (in `pmm_alloc_page`, gated on `result == 0x33F000`)
  was **stripped** before the `sys_fork`/`sys_clone` path ran on the
  base tree. It should have caught it; it was not in the image.
- The tick canary (`[PT-CANARY]`) checks on every tick. The write
  happens inside `vmm_clone_page_table`, which runs with **interrupts
  off** (from `sys_fork`'s `cli`). No tick can fire between the
  `memset` and the copy, so the tick canary could not see it.
- The write-protect PDE probe did not fire because it watched for
  **kernel writes to the PT page through the HHDM alias**, and the
  clone's write is through `HHDM_START + new_pt_phys` -- but the
  write-protect block was removed before the clone path ran, and the
  probe was never live during a fork that hit `0x33F000`.
- The gdb hardware watchpoint was the tool that caught it, because it
  watches the **physical page** and does not need a tick or a probe
  in the kernel.

### The root cause is still open

**Who cleared `pmm_bitmap[0x67F]` bit 7?** The clone is the **victim**,
not the cause. The PMM handed out a live PT because the bitmap said the
page was free. The bitmap was corrupted by some **earlier** stray write
or a missing `bitmap_set` on an allocation that did not record the page
as used. The fix is to find **who clears the bit**, not to patch
`vmm_clone_page_table`.

**The next probe is the handoff's original one, never run:**
`bitmap_clear(page)` / `bitmap_set(page)` in `pmm.c`, gated on
`page == 0x33F`, printing the caller's return address. Run it with
**several `^Z`/`fg` cycles** on the base tree. The first
`[BITMAP_CLEAR_33F]` before a fork names the clearing code.

Do **not** patch `vmm_clone_page_table` or `process_create` before the
bitmap-clear probe names the real culprit. The clone is a victim.

### What NOT to retry

- Do **not** re-add the `process_reclaim` guard (tried, did not fire).
- Do **not** re-add top-of-tick `signal_deliver_frame_iretq` (tried, neutral).
- Do **not** re-add the `rsp == 0` guards in `scheduler.c` /
  `process.c` / `user_syscall.c` (tried, turned the fault into a `Z`
  halt).
- Do **not** re-add the `sys_munmap` owned-check (tried, did not fix).
- Do **not** use gdb for **timing-sensitive repro** (masks the race).
  The **hardware watchpoint** is a different intervention and **did
  catch it** -- the masking risk did not materialize.
- Do **not** assume `^Z` is optional. Every reproduction this session
  required at least one `^Z`/`fg` cycle.
- Do **not** re-stack the probe stack from
  `~/donix-20261101-canary-probes.patch` all at once. It **broke the
  boot** (`musl_sh` faulted at `0x400047`, `CR2=0`, `pte=0x3`). Apply
  one probe at a time, boot-check each, per the "one change at a time"
  rule.

### Files to check in the fresh session

- `pmm.c`: `bitmap_clear`, `bitmap_set`, `pmm_alloc_page_for_tables`,
  `pmm_scan_zone`, `pmm_page_info` bookkeeping. **Start here.**
- `vmm.c`: `vmm_clone_page_table` (`memset` at `vmm.c:329`),
  `vmm_map_page_in_cr3` (`memset` at `vmm.c:164`, `vmm.c:463`).
- `process.c`: `process_create`, `process_free_clone`,
  `process_cleanup_elf_pages`, `process_reclaim`.
- `elf.c`: `elf_add_page_to_pcb` (defensive check: reject if `phys` is
  inside a known PT range).

### The probe to add first

In `pmm.c`, in `bitmap_clear(uint64_t page)`:

    if (page == 0x33F) {
        serial_lock();
        serial_print("[BITMAP_CLEAR_33F] caller=0x");
        serial_print_hex((uint64_t)__builtin_return_address(0));
        serial_print("\n");
        serial_unlock();
    }

and the same in `bitmap_set`. Run `canary --full` with several `^Z`/`fg`
cycles, **no gdb**, and read the serial log. The first
`[BITMAP_CLEAR_33F]` before a fork names the clearing function.

---

## THE OTHER OPEN BUG: `^C` -> the console dies after `/ #`

**Unchanged since 2026-10-31.** The `^C` work was not part of today's
investigation. The `^C` bug and its WIP patch are unchanged from the
previous handoff. **This section is verbatim from the old handoff,
with the timestamp updated.**

`^C` is **not fixed**. Everything tried is in
`/tmp/donix-ctrlc-wip-20261031-1330.patch` (94 lines) and
`/tmp/donix-ctrlc-wip-status.txt`. The tree is reverted to `80d272b` so
the OS boots; `^C` does nothing on the committed tree.

### What we PROVED on 2026-10-31 (do not re-derive)

- **`^C` -> SIGINT delivery WORKS.** The foreground child dies in every
  variant tried.
- **The console dies AFTER the shell prints `/ #`** -- keyboard dead --
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

**Possible relationship to `#PF`:** the bitmap corruption that causes
the `#PF` could also affect the child-terminate path (VMM teardown).
**Do not assume this.** Chase the bitmap corruption first, and
re-test `^C` on the fixed tree; if `^C` is fixed as a side effect,
great; if not, its gdb investigation stands as its own bug.

---

## Working style -- how this project gets changed (READ THIS)

**One change at a time.** A "change" is a commit, verified by
**RUNNING** before the next starts. A build passing is not a test
passing.

**This session's hard lesson (2026-11-01):** we stacked six probes on
one tree and **broke the boot** (`musl_sh` faulted at `0x400047`,
`CR2=0`, `pte=0x3`). Every probe had to be stashed. The correct
sequence was: (1) save state, (2) **gdb hardware watchpoint**, (3) read
the backtrace, (4) one fix. We did not do (2) until the end, and the
watchpoint gave the answer in one run.

**A handoff claim is not evidence.** The old handoff said `d1ae213` was
green; it is not. **Re-run every repro on a fresh boot and read the
bytes before acting.**

**Ask for source you do not have** before patching a file not seen this
session.

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
**copy it aside after any failure**). `./run debug` adds `-S` (frozen
at reset). The Makefile run targets use `-cpu max` + TCG (a different
CPU) -- state which launcher was used. Default cwd `/root` (prompt
`~`).

**In-guest gate:** `cd /`, then `canary --full` (28/28) and
`/root/scripts/test.sh` (57/57). **Use multiple `^Z`/`fg` cycles.**
One is not always enough to reach the `#PF`; several reliably do.

**Do not** `git reset --hard` without stashing; **do not** edit
`third_party/`; **do not** commit `kmain.c`'s `pit_init` knob; **do not**
call a race closed on one clean run.

**Do not** assume a commit fixed something because its message says
it did. `f91d31a` claims to fix canary/pipe7e/ctrl-Z; it does not.
Test every commit before trusting its message.

**Do not push `dev` without a full read of the 69 unpushed commits.**
`origin/dev` is at `799e98f` (`v0.6.18`). Local `dev` is 69 ahead.
That is intentional work, not accidental divergence. Force-pushing
would destroy it.

**gdb hardware watchpoint is safe for this bug.** The handoff's
"do not use gdb" applies to **timing-sensitive repro** (gdb-at-fault
masks the race). A **write watchpoint** on the physical page does not
mask it; it caught the writer in one run. Use it again for the
`^C` bug if the bitmap fix does not fix `^C` as a side effect.

---

## Releases

**`v0.6.15` through `v0.6.18` are released.**
`origin/dev` = `799e98f` = `v0.6.18`. Local `dev` = `80d272b` = 69
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
`dev` is 69 commits ahead of `origin/dev` (unpushed). No new release
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

### Baseline (committed, `80d272b`, tree clean, builds green)

- `make clean && make` -> `MAKE_EXIT=0`
- **`canary --full` is NOT green.** It crashes with the `#PF`
  described above after at least one `^Z`/`fg` cycle. The writer is
  found; the root cause (PMM bitmap corruption) is not yet fixed.
- `^Z` on a foreground child -> `[1]+ Stopped`; `jobs` lists it; `fg`
  resumes. Works.
- `^C` -> does nothing (original behavior). Unchanged from 2026-10-31.
- `exec_churn` -> 24 rounds pass.

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
  - `~/donix-20261101-canary-probes.patch` --
