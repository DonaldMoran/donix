# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-02. **`v0.6.18` is released.** Work is
pre-`v0.6.19`. Baseline is `80d272b`; `dev` is now well past it (run
`git rev-list --count origin/dev..dev` for the real ahead-count).
Working tree clean, build green (`MAKE_EXIT=0`). **`origin/dev` is at
`799e98f` (`v0.6.18`) and has NOT been pushed to.** `origin/dev` must
not be force-pushed without first reading every unpushed `dev` commit;
they are intentional work, not accidental divergence.

**`^C`, `^Z`/`fg`, and the `busybox sh` nesting are FIXED** (2026-11-02,
commit `7cb26fb`). The fix is the 2026-10-31 `^C` WIP, which the old
handoff reported as "lost from `/tmp`" -- **it was not lost; it was
uncommitted in `~/code/saved`.** Recovered, applied cleanly on top of the
three correctness fixes, committed, and verified:
- `^C` SIGINT reaches the foreground pgrp without killing IRQ1; the
  console survives and the prompt returns.
- `^Z`/`fg` works across many cycles; `jobs` lists the stopped child.
- `exit` from busybox to the Donix shell, then `busybox sh` again,
  returns the ash prompt (`sys_execve: f_open(usr/bin/BUSYBOX) -> 4`
  on the first PATH attempt, then the fallback to `/bin/busybox`
  succeeds).

**The `#PF` is REDUCED but NOT CLOSED.** Before `7cb26fb`, one `^Z`/`fg`
cycle reliably faulted `canary --full` at `0x413966` (`pt=19`, `pte=0`).
After `7cb26fb`, a full `canary --full` with **5** `^Z`/`fg` cycles
reaches **28/28**, and `ps` shows a correct process table. But an
**unlucky run still faults**, at a **different `.text` slot**:
`RIP=CR2=0x450630`, `pt=80`, `error 0x14`, `pte=0`, canary's cr3. So
the job-control state machine was **a contributing cause**, not the only
one. The fault class is "a present `.text` PTE is zeroed under some
timing." **The VA-based probe is still the next step, generalized to
any `.text` PTE that gets zeroed** -- see "THE `#PF`" below.

---

## THE `#PF`: intermittent `.text` PTE zeroing, **still open**

**Two observed instances, same class, different slot:**

    # old (before 7cb26fb, first ^Z/fg reliably):
    === PAGE FAULT (#PF) ===
      CR2 / RIP : 0x413966   error 0x14 (present, user, fetch)  pt=19  pte=0

    # new (after 7cb26fb, unlucky run):
    === PAGE FAULT (#PF) ===
      CR2 / RIP : 0x450630   error 0x14 (present, user, fetch)  pt=80  pte=0

Both are canary's own cr3, both are a `present` 4 KB PT (`pde=0x…027`),
both have the faulting slot `pte=0`. `0x413966` is in `.text`; `0x450630`
is also in `.text` (the ELF image is `0x400000`–`0x600000`). **The
faulting VA is stable per instance but the specific PT slot varies**,
and the `.text` PT's *physical* address also varies by boot (see
"physical-address probes" below).

**What 7cb26fb changed:** the fault went from "fires on the first
`^Z`/`fg`" to "fires occasionally, at a moving `.text` slot." That is a
real improvement in behavior, and it names the job-control state machine
(pgrp inheritance, `wait4` pgroup semantics, console-fg handoff) as **a**
cause. It is not the whole cause.

**Ruled out this session (for the boots where the probes fired):**
- Bitmap corruption of the specific page under test.
- `memset(phys_to_virt(<pt>), …)` in `vmm.c` (gated on the literal
  physical page -- silent).
- `elf.c` copy/BSS writes to that page -- silent.
- Zombie dispatch -- fixed separately (`b931603`), fault unchanged by
  that fix alone.
- `sys_munmap` from canary -- the gated `vmm_unmap_page_in_cr3` events
  on the ELF range were `exec_free_and_unmap_user_pages`, legitimate
  exec teardown.

**The next probe -- VA-based, generalized to any `.text` slot:**

Gate on the **virtual address**, not the physical page. In
`vmm_unmap_page_in_cr3` and `vmm_map_page_in_cr3`'s `else`-branch write
(and the clone's `new_pt[m] = src_pt[m]` loop for the matching slot),
print whenever the write **clears the present bit of a PTE whose `va`
is in `[0x400000, 0x600000)`** (the ELF image range). Print `va`, `cr3`,
`pt_idx`, `old`, `new`. **This catches the zeroing regardless of which
`.text` slot and which physical PT page the boot happens to use.** The
two known faulting VAs (`0x413966`, `0x450630`) are both in that range,
so gating on "any `.text` PTE becoming non-present" is the right
generalization.

Also record **`interrupts_enabled` (RFLAGS.IF)** and **`g_ticks`** at
each such write. If a write to a live `.text` PTE lands with IF=1 in a
function whose caller assumed `cli`, that is the `cli`/`sti`-class
explanation, and the fix is a `cli` at the right boundary -- a
different probe shape from the VA gate, and the one never run this
session.

**Secondary tool:** gdb hardware watchpoint on the **virtual** PTE. The
PT phys must be read from the fault dump's `pde` first (it changes by
boot), then the watchpoint is `HHDM_START | (pde & ~0xFFF) + pt_idx*8`.
Arm after the fault dump prints, re-run.

**What was tried and reverted (do not reapply):** every
physical-address gate (`[BITMAP_*_33F]`, `[FREECLONE]`, `[FREE_TARGET]`,
`[UNMAP_33F]`, `[MAP33F]`, `[MEMSET_33F]`, `[ELF_WRITE_33F]`,
`[EXIT]`). They are boot-dependent. The `sys_munmap` ELF-range guard is
**kept** -- it is a range check, not a phys check.

**The correctness fixes from `b931603` (`pmm_alloc_page` type check,
`scheduler_ready_queue_next` zombie/terminated/unused skip,
`sys_munmap` ELF-range guard) and the job-control WIP from `7cb26fb`
are all in and stay.** They are independent of the remaining `.text`
zeroing.

**Note on `pmm_alloc_page`'s type check:** the `PMM: bitmap desync!`
print fires only when the bitmap and `pmm_page_info[page].type`
disagree. That is a general "metadata desync" signal, not a
`0x33F000`-specific gate -- keep it. If it fires, that is a distinct
bug from the `.text` PTE zeroing.

---

## Fixed this session (do not re-investigate)

**`^C` -> the console no longer dies after `/ #`.** Fixed by `7cb26fb`.
The WIP was written 2026-10-31, never committed, and recovered from
`~/code/saved` (see "The `~/code/saved` lesson" below). What it does:
deferred `^C` signal via `console_raise_pending` (never `sys_kill` inside
IRQ1), pgrp inheritance in `sys_fork`, `wait4` pid/pgrp/wait-any forms,
`console_read` foreground check, `sys_poll` EINTR, deferred `setpgid`,
`console_fg_pgrp_set`/`_reclaim_if`, `tcgetpgrp`/`tcsetpgrp` latching,
and an IRQ1 unmask safety net in the timer. The old "`^C` is not fixed"
section is **obsolete** -- replaced by this paragraph.

**`^Z`/`fg` on a foreground child works across many cycles.** `jobs`
lists the stopped child; `fg` resumes. Fixed/robust by the same WIP.

**`busybox sh` nesting works.** `exit` from busybox returns to the Donix
shell; `busybox sh` from the Donix prompt returns the ash banner and
prompt. On the first PATH attempt, `sys_execve: f_open(usr/bin/BUSYBOX)
-> 4` is printed (FatFs `FR_NO_FILE`); the fallback to `/bin/busybox`
(which is staged) then succeeds. **The image does NOT have
`/usr/bin/busybox`** -- only `/bin/busybox` and `/bin/sh`. If the
fallback ever stops working, that is the `^C`/job-control state again,
not a missing file.

---

## Working style -- how this project gets changed (READ THIS)

**One change at a time.** A "change" is a commit, verified by
**RUNNING** before the next starts. A build passing is not a test
passing.

**This session's hard lessons (2026-11-01 .. 02):**

- **Stacked probes break the boot.** Six probes on one tree made
  `musl_sh` fault at `0x400047`. Sequence should be: (1) save state,
  (2) **gdb hardware watchpoint**, (3) read the backtrace, (4) one fix.
- **Physical-address gates are boot-dependent.** A dozen probes gated
  on `0x33F000` were silent on the boot where the `.text` PT landed at
  `0x340000`. **Gate on the invariant (VA, pid, state, pgrp), not on
  the address that happens to hold today.**
- **`canary --full` can pass.** 28/28 has now been observed across
  multiple runs, including one with 5 `^Z`/`fg` cycles and a correct
  `ps` table. The `.text` `#PF` is intermittent.
- **THE `~/code/saved` LESSON (the big one).** A comparison tree is
  not necessarily the commit it claims to be. `~/code/saved` was
  "`d1ae213`" by `git log`, but had **five uncommitted files** -- the
  entire 2026-10-31 `^C` WIP. Every "it works at `d1ae213` but not at
  `HEAD`" comparison was `d1ae213+WIP` vs `HEAD`, i.e. **WIP vs no-WIP**,
  not baseline vs regression. The fix took two sessions to find because
  nobody ran `git status` on the comparison tree. **Rule: before
  calling anything a regression, `git status` BOTH trees. If the
  "known-good" tree is dirty, `git diff` it first.**

**A handoff claim is not evidence.** The old handoff said `d1ae213` was
green; it was not. The old handoff said the `^C` WIP was "lost from
`/tmp`"; it was not -- it was uncommitted in `~/code/saved`. **Re-run
every repro on a fresh boot and read the bytes before acting.**

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
`hdd-single.img`. **After a session of probe edits and reverts, run a
full `make clean` before the next behavioral test** -- per-file `rm` is
not enough when multiple files were patched.

**Test harness.** `./run` (no arg) = clean build + fresh
`hdd-single.img` + `-enable-kvm -cpu host -display sdl`, no freeze,
gdb stub on `:1234`, `capture.txt` written (overwritten each run --
**copy it aside after any failure or pass you want to keep**).
`./run debug` adds `-S` (frozen at reset). **`./run tcg` /
`./run tcg-debug`** = the same flow in TCG (`-cpu max`, no
`-enable-kvm`): use TCG for the `mmap`-window fault (`CR2=0x8010000000`,
`cr3=0x347000`, `pt[0]=0`, `error=0x6`), KVM for the canary `.text`
`#PF`. The Makefile run targets also use `-cpu max` + TCG -- **state
which launcher was used.** Default cwd `/root` (prompt `~`).

**In-guest gate:** `cd /`, then `canary --full` (28/28) and
`/root/scripts/test.sh` (57/57). **Use multiple `^Z`/`fg` cycles.**
One is not always enough to reach the `#PF`; several reliably do.

**Do not** `git reset --hard` without stashing; **do not** edit
`third_party/`; **do not** commit `kmain.c`'s `pit_init` knob; **do not**
call a race closed on one clean run.

**Do not** assume a commit fixed something because its message says
it did. `f91d31a` claims to fix canary/pipe7e/ctrl-Z; it does not.
Test every commit before trusting its message.

**Do not push `dev` without a full read of every unpushed commit.**
Run `git log --oneline origin/dev..dev | wc -l` for the current count.
`origin/dev` is at `799e98f` (`v0.6.18`). Force-pushing would destroy
the unpushed work.

**gdb hardware watchpoint is safe for this bug.** The handoff's
"do not use gdb" applies to **timing-sensitive repro** (gdb-at-fault
masks the race). A **write watchpoint** on the physical page does not
mask it; it caught writers in single runs.

---

## Releases

**`v0.6.15` through `v0.6.18` are released.**
`origin/dev` = `799e98f` = `v0.6.18`. Run
`git rev-list --count origin/dev..dev` for the current ahead-count on
`dev`; **do not hardcode the number here.**

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

**Current release state:** `v0.6.18` released; work pre-`v0.6.19`.
No new release until the `.text` `#PF` is closed. The `^C`/`^Z`/nesting
fixes (`7cb26fb`) are a strong candidate for `v0.6.19` **once the
intermittent `.text` `#PF` is also addressed or explicitly accepted as
a known open bug in the release notes**.

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

### Baseline (committed, `80d272b` plus this session's commits, tree clean, builds green)

- `make clean && make` -> `MAKE_EXIT=0`
- **`canary --full` is green on lucky runs and faults on unlucky
  ones.** 28/28 has been observed across multiple runs, including one
  with 5 `^Z`/`fg` cycles and a correct `ps` table. The intermittent
  fault is the `.text` `#PF` at a moving slot (`0x413966`/`pt=19`
  before `7cb26fb`; `0x450630`/`pt=80` after).
- `^Z` on a foreground child -> `[1]+ Stopped`; `jobs` lists it; `fg`
  resumes. Works across many cycles.
- `^C` -> SIGINT reaches the foreground pgrp; the console survives and
  the prompt returns. **Fixed 2026-11-02** (`7cb26fb`).
- `busybox sh` nesting -> works. `exit` returns to Donix shell;
  `busybox sh` returns the ash prompt. **Fixed 2026-11-02**
  (`7cb26fb`).
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
  after any failure or pass you want to keep.
- **`~/code/saved` is the old `d1ae213` copy WITH THE 2026-10-31 `^C`
  WIP uncommitted.** The WIP has been recovered and committed as
  `7cb26fb`; `~/code/saved` can now be treated as stale. **Do not
  compare against it again without `git status` first.**
- `~/donix-20261101-canary-probes.patch` -- the old physical-address
  probe stack. **Do not apply it as a stack**; it broke the boot.
