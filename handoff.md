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

**The `#PF` is UNSOLVED.** See "THE `#PF`: VA-stable, PT-phys-unstable"
below. The fault is stable by VA (`RIP=CR2=0x413966`, `pt=19`, `pte=0`)
but the `.text` PT's *physical* address varies by boot, so every
physical-address probe this session (gated on `0x33F000`) was boot-
dependent and has been reverted. The next probe is VA-based: gate on
`virt in [0x413000, 0x414000)`, not on `phys == 0x33F000`.

**Baseline builds. Baseline is NOT "green" as a test run.**
`canary --full` on `80d272b` crashes with the `#PF` described below,
after at least one `^Z`/`fg` cycle. The old handoff's "baseline is
green" claim was false; it was re-tested this session and confirmed
false.

---

## THE `#PF`: VA-stable, PT-phys-unstable, **UNSOLVED**

**Symptom (stable across every boot this session):**

    === PAGE FAULT (#PF) ===
      CR2 / Faulting RIP : 0x413966
      Raw Error Code     : 0x14    (present, user, instruction fetch)
      CR3                : canary's cr3
      Walk               : pd=2 pt=19
      pde                : 0x...0027 (4 KB PT, present/RW/user)
      pte                : 0

Canary's `.text` PTE for `0x413000`'s page is **zero**, canary fetches
it, faults. **Stable by VA in every run this session.**

**The critical finding: the PT's *physical* address is not stable across
boots.** In most runs canary's `.text` PT landed at `0x33F000`; in one run
it landed at `0x340000` (whole hierarchy shifted one page: `cr3=0x33C000`,
`pml4e=0x33D027`, `pdpte=0x33E027`, `pde=0x340027`). **Every diagnostic
probe this session gated on the literal `0x33F000`, so every "probe is
silent" conclusion is valid only for the boots where the PT happened to
land there.** Those probes have been **reverted**; the conclusions below
are marked accordingly.

**Ruled out (for the boots where the PT was `0x33F000`):**
- Bitmap corruption (`bitmap_set`/`clear` of `0x33F` fired only at
  clone-alloc and reclaim, both consistent).
- `memset(phys_to_virt(0x33F000), ...)` in `vmm.c` -- silent.
- `elf.c` copy/BSS writes to `0x33F000` -- silent.
- Zombie dispatch -- fixed (`scheduler_ready_queue_next`), fault unchanged.
- `sys_munmap` from canary -- the `UNMAP_33F` on `0x33F000` was
  `exec_free_and_unmap_user_pages`, legitimate exec teardown.

**Not ruled out -- and the next probe must be VA-based, not phys-based:**

The zeroing (or flag-change) write to `pt[19]` of canary's `.text` PT, by
**virtual address**. Gate in **`vmm_unmap_page_in_cr3`** and
**`vmm_map_page_in_cr3`'s else-branch write** on
**`virt in [0x413000, 0x414000)`** (and the clone's `new_pt[m] = src_pt[m]`
loop on `virt == 0x413000`'s slot), printing `virt`, `cr3`, `pt_idx`,
`old`, `new`. **Do NOT gate on `phys == 0x33F000`.** This catches the
write regardless of where the PT lives this boot.

**Secondary tool:** gdb hardware watchpoint on the **virtual** PTE -- but
the PT phys must be read from the fault dump's `pde` first (it is
`0x33F027` or `0x340027` depending on the boot), so the watchpoint
address is `HHDM_START | (pde & ~0xFFF) + 19*8`. Arm it after the fault
dump prints, then re-run.

**What was tried and reverted (do not reapply):** every physical-address
gate (`[BITMAP_*_33F]`, `[FREECLONE]`, `[FREE_TARGET]`, `[UNMAP_33F]`,
`[MAP33F]`, `[MEMSET_33F]`, `[ELF_WRITE_33F]`, `[EXIT]`). They are
boot-dependent. The `sys_munmap` ELF-range guard is **kept** -- it is a
range check, not a phys check.

**The three fixes landed this session are independent of the `#PF` and
stay.**

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

**Possible relationship to `#PF`:** the `#PF` (VA-stable write to
canary's `.text` PT) could also affect the child-terminate path (VMM
teardown). **Do not assume this.** Chase the `#PF` first, and
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
  found; the writer of the `pt[19]` zeroing is not yet named (see
  the `#PF` section: VA-based probe is next).
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
