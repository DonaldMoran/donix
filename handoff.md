# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-04 (fifth session), at `c8ef340`.
**`v0.6.19` is the last pushed release.** `dev` is at `c8ef340`; its
parents are `63cb81b`, `4b24e71`, `52e1569`, `0f97048`, `e128c92`.
`origin/dev` = `ba56b3d`. Ahead **37**, clean tree, **stash present**
(`stash@{0}`: vfork 3b diagnostics; preserved, do not drop). No push,
no tag this session; banner still `v0.6.19`. `main` = `7e4748b` (the
`--no-ff` merge of `v0.6.19`). Scratch tag `20261104-gettimeofday-time`
is on `5a3b65e` (still preserved).

**`~/code/testme` is the `v0.6.19` reference tree, but its busybox
omits `date`/`time`/`uptime` from its applet set** even though
`configs/busybox.config` enables them. `cmp`-identical to `testme`
confirms ELF *shape*, not *applet set*; the applet-set oracle is
`busybox --list`, and the build of record is `configs/busybox.config`
plus `toolchain/musl-gcc.sh`.

**`~/code/testme` is a `v0.6.19` reference tree (`ba56b3d`, tagged).**
It is the known-good comparison. **Do not modify it.** When a shell
behavior is in doubt, boot `testme` and compare.

---

## START HERE

**Before doing anything else with `time`, read
`docs/open-issues.md`'s section "The `time true` fix attempts of the
2026-11-04 fifth session".** It records the three fix attempts made
that session -- all measured, all reverted -- and the four options
that remain with their reproducibility. The root cause is unchanged:
the static busybox has no `PT_PHDR` and no `_DYNAMIC`, so musl's
`static_init_tls` computes `base = 0` and the TLS image copy faults.

**Three fix routes are refuted; do not re-walk them.**
`-Wl,--dynamic-linker` has no effect on a static non-PIE link;
`-fuse-ld=lld` emits `PHDR` but moves the base `0x400000`->`0x200000`,
adds `PT_INTERP`, and loses the applets; a from-scratch `SECTIONS`
block discards the real sections (a 714-byte-code skeleton). The
measurements are in that open-issues section.

**Reproducibility is a first-class constraint.** A fix must be
tracked and built by the clone script, or a cloner does not get it.
`busybox_ldscript` is gitignored and binutils-version-dependent, so
the linker-script route is a local experiment, not a shippable fix.

**You do not need the conversation history.** You need this file's
`WHERE WE ARE` and `NEXT` sections, and that one section of
`docs/open-issues.md`.

**Do not re-apply `939e3ef` or `153dda9`.** Both were reverted
(`0f97048`, `e128c92`). They moved the `time true` fault forward but
broke the shell: `939e3ef` breaks `^C` on a foreground child,
`153dda9` breaks `busybox sh` re-entry. **Any `sys_execve` stack
change must be tested with both before it lands.** See the gate gap
below.

**`time true` is not a race.** It fails deterministically on the first
invocation in a boot where everything else passes.

---

## WHERE WE ARE

**`v0.6.19` is the last pushed release.** `dev` is at `c8ef340`.
Ahead **37**, clean, stash present. Nothing pushed this session.

**WHAT LANDED THIS SESSION (fourth session, five commits):**

1. **`6ca3690`** (docs) -- third-session handoff rewrite, from the
   session start.
2. **`54e3b2d`** (code, **kept**) -- `sys_execve` clears
   `pcb->clear_child_tid` on exec. **This is the session's real
   gain.** It fixes the `pipe7e` kernel-mode `#PF` (`CR2 0x4087D0`,
   `error 0x2`, `CS 0x18`, PTE not present) that `4312aab`
   introduced. `pipe7e` reaches `iter 4000` without a `#PF` (vs
   `iter 0` before).
3. **`e128c92`** (revert) -- reverts `153dda9` (16-byte-align
   `rsp_init`). With `153dda9`, `busybox sh` from the `donix>`
   fallback hangs after its banner.
4. **`0f97048`** (revert) -- reverts `939e3ef` (`pcb->user_rsp =
   rsp_init`). With `939e3ef`, `^C` on a foreground child fails.
5. **`52e1569`** (docs) -- `docs/open-issues.md` fourth-session
   section: the reverts, the kept fix, the gate gap, the
   `time true` state.

**Baseline, verified in the fourth-session boot (all in one boot):**

- `canary --full` **28/28**
- `test.sh` **57/57**
- **`^C` on a foreground `sleep 100` returns the prompt** (worked)
- **`^Z` on `sleep 100` -> `[1]+ Stopped`; `jobs` lists it; `fg`
  resumes**
- **`exit` from `ash` -> `donix>`; `busybox sh` at `donix>` -> the
  ash prompt** (re-entry works)
- **`pipe7e` reaches `iter 4000` without a `#PF`**
- **`time true` faults**, busybox-side signature: `num 218`,
  `last sysret rcx 0x40FBC0`, `RSP = g_user_rsp_save + 0x180`, the
  `__init_tls` frame intact below the fault, the `argv`/`envp`/auxv
  layout at `rsp` correct, and a `ret` in `__init_libc`'s chain
  popping a slot that is already zero.
- **`time` is not usable.** `true` alone works; `time` with no args
  prints usage; `time true` and `time ls /` fault. **`/usr/bin/TIME`
  and `/bin/true` do not exist.**

---

## NEXT -- the highest-value work, in order

The full ranking with the reproducibility analysis is in
`docs/open-issues.md`'s fifth-session section. This is the short
form. **No decision has been made between the `time` options.**

1. **The `time` question: which route.** Four options, with the
   constraint that a fix must be tracked and built by the clone
   script:
   - **Own `time` binary** (`userland/musl/apps/time.c` + image
     staging line). Tracked, built by the clone script, low risk.
     Gives a working `time`; does **not** fix the busybox `time
     true` fault (busybox's own startup still faults).
   - **musl `static_init_tls` patch** via a tracked
     `toolchain/*.patch` applied by `toolchain/install_musl.sh`.
     Reproducible; fixes the fault itself. **Blast radius: every
     static musl binary's startup.** The fourth-session gate gap
     applies; the narrowest guard is for static `ET_EXEC` with
     `base == 0`.
   - **Dynamic linking.** Fixes the fault by construction, but is
     **not a small option**: the kernel has no `PT_INTERP` handling
     (`elf.c:91` skips non-`PT_LOAD`; `elf.c:232` returns the
     binary's `e_entry`), no `/lib` staging in the image,
     `userland/musl/Makefile` builds `-static -no-pie` only, and
     `third_party/musl-install/lib/` has no `libc.so`. A
     multi-session kernel + build-system project.
   - **Linker script** (`busybox_ldscript` = default `ld` script +
     `PHDRS`). Mechanism proven; content refuted; **not
     reproducible** for a cloner. Local experiment only, with a
     `readelf -SW` section-size gate (`.text` ~`0x541e5`, not
     `0x2ca`) before any boot.
2. **`sys_mkdir` `-ENOTDIR` vs `-EEXIST`** for the file case.
   POSIX wants `-ENOTDIR` when the target exists as a file. Small,
   `sys_mkdir`-local; no current caller depends on it.
3. **`docs/open-issues.md` item 10 -- SSE save/restore.** Unchanged.
   The test is in the item.
4. **`docs/open-issues.md` item 9 -- the privilege model.**
   **Before starting, ask what it unlocks.** If "nothing," skip.
5. **The `time` applet's accounting bug** (`real 0m 0.34s` on some
   runs, `1191h 48m` on others; `user`/`sys` about `152710235h`).
   Separate from the `time true` fault. **Do not fix in the same
   session as the `time true` fault.**
6. **The `^C` intermittence** seen earlier in the fourth session
   (works once, fails later). It is race-shaped and **separate** from
   the `time true` fault. Not diagnosed. Do not conflate the two.

---

## THE GATE GAP (new this session -- read before any `sys_execve` change)

**`canary --full` (28/28) and `test.sh` (57/57) do not cover `^C` on
a foreground child or `busybox sh` re-entry.** Both `939e3ef` and
`153dda9` passed the green gate and broke one of those two shell
behaviors. **Any change to `sys_execve`'s user stack (`pcb->user_rsp`,
`rsp_init`, the argv/envp/auxv layout) must be tested with:**

- **`sleep 100` then `^C`** -> the prompt returns
- **`exit` from `ash` -> `donix>` -> `busybox sh`** -> the ash
  prompt returns

before it is committed. **A green `canary`/`test.sh` is not
sufficient for a stack-layout change.**

---

## WHAT WAS REFUTED -- do NOT re-walk.

*Fifth-session additions first.*

- **`-Wl,--dynamic-linker=/lib/ld-musl-x86_64.so.1` as a way to
  force `PT_PHDR`.** No effect on a static non-PIE link; the binary
  is byte-identical in shape (no `PHDR`/`INTERP`/`DYNAMIC`).
- **`-fuse-ld=lld` as a fix.** Emits `PHDR` but moves the base
  `0x400000`->`0x200000` (entry `0x214140`), adds `PT_INTERP`
  requesting `/lib/ld-musl-x86_64.so.1`, loses `date`/`time`/
  `uptime`, and the boot faults at `CR2 0x400040` (auxv `AT_PHDR`).
- **A from-scratch `SECTIONS` block in `busybox_ldscript`.** The
  mechanism works (`trylink` uses the file, `PHDR` appears, no
  `INTERP`), but the hand-built `SECTIONS` discarded the real
  sections (`.text` `0x2ca` instead of `0x541e5`, no applets). A
  correct script must be the default `ld` script plus `PHDRS` and
  `:seg`, gated by `readelf -SW` before any boot.
- **`testme` as an applet-set reference.** Its busybox omits
  `date`/`time`/`uptime`. Use `busybox --list`.
- **Dynamic linking as a small option.** The kernel has no
  `PT_INTERP` handling, no `/lib` staging, a static-only userland
  build, and no `libc.so`.

*Fourth-session additions.*

- **`939e3ef` (`pcb->user_rsp = rsp_init`) as a safe fix.** It
  regressed `^C` on a foreground child. **Reverted by `0f97048`.**
  Do not re-apply without the `^C` test.
- **`153dda9` (16-byte-align `rsp_init`) as a safe fix.** It
  regressed `busybox sh` re-entry. **Reverted by `e128c92`.** Do not
  re-apply without the `busybox sh` test.
- **The `0x44cc0d` / `0x44ac73` probe addresses.** Those were
  downstream of the two reverted changes. **Stale.** The next probe is
  the pre-session `0x40FBC0` one.
- **`time true` as a race.** It fails deterministically on the first
  invocation in a boot where `^C`, `^Z`, `fg`, `canary`, `test.sh`,
  and `pipe7e` all pass. The `race_probe` dump is instrumentation.
- **The `race_probe` `RACE PROBE DUMP` as a mechanism.** It reports
  the standing `wake_no_waiter == writers_nonzero` question. It does
  not mean the fault is a race.
- **`time /bin/true` as a control.** `/bin/true` does not exist.
  Valid real-file controls are `/usr/bin/HELLO`, `/usr/bin/ECHO`,
  `/usr/bin/CAT`.
- **`$fs_base` as a per-process marker.** Same VA across busybox
  processes. **Only `pid` or `CR3` distinguishes.** The working-style
  rule below has been corrected.
- **`argv_scratch` / `envp_scratch` as the cause.** `argv_scratch`
  is 4 KB, `envp_scratch` is kmalloc'd. Already refuted.
- **A `call *%rax` with `rax = 0` as the fault.** The dumps show a
  `ret` popping a zeroed slot.
- **The boot-time `pid 3` `num 7` fault as a `time true` signature.**
  Recorded, not chased. Not `time true`.
- **The `clear_child_tid` store in `process_exit` as a `^C` cause.**
  `54e3b2d` (the `sys_execve` reset) is kept and the `^C` failure
  was `939e3ef`, not `process_exit`.

*Earlier, unchanged.*  Do not re-walk: the `cli` window; signal
delivery as the zeroer; `memset` as the zeroer; the sysret slot
arithmetic; "the slot flips `0x40B163`->0 in one process"; the three
`exec_path` forms; `.bss`-not-zeroed; `0x448C6A`/`CR2=0x34` as the
root cause (real, separate, fixed by `1bf2ca2`); `lbb_prepare` /
`run_applet_and_exit` / `applet_main[rbx]`; the auxv;
`set_tid_address`'s return; mmap; V3/V3b register inheritance; the
`efc8e08` rsp split and `frame_kind`; the four uncommitted scheduler
guards; the `EXIT-REFUSE frame-CS` guard; the canary `.text` `#PF`
probe stack; the physical-address gates on `.text` PTE zeroing; the
`f91d31a` claim; `exec_churn`'s KVM slowness; `ata_poll_drq` spinning;
`exec_churn` can measure `sys_fork`'s eager-copy cost; the
`perf kvm stat record` `double free` is a donix issue; the
2026-11-04 PTAUDIT "22 anomalies"; the empty-low-half
`vmm_create_address_space`; V3/V3b register zeroing; the missing auxv;
`static_init_tls` at `0x40FBC0` as a `call *%rax`; `kmain.c`'s
self-tests map over the ELF text page.

---

## Working style -- how this project gets changed

Read this before proposing any command block.

**One change at a time.** A "change" is a commit, verified by
**RUNNING** before the next starts -- build, run, see the behavior,
THEN commit. A build passing is not a test passing.

**The state check comes before the command block.** Every command
block starts with `git status -sb` (or `git log`).

**A command block is one paste -- write it to a file first.**
`git add`, `git status`, `git commit`, and `git tag` belong in **one**
block, with the message inline via heredoc. **Do not paste a block
with `cd ... || exit 1` into an interactive shell** -- the `exit`
closes the terminal. Write to `/tmp/<name>.sh` with
`cat > ... <<'SCRIPT_EOF'`, then run it as a **separate** paste.
**`cd "$HOME/..."` or a literal path in the script, never `cd
"$(dirname "$0")"` when the script is pasted.**

**A block that writes a file must actually be run as its own paste.**
Two steps, both explicit. **And a `/tmp/<name>.sh` file may be stale
from an earlier session: re-write it in the same paste that runs it,
or `cat` it and read before running.** This session committed a
handoff under an obsolete message because the script in `/tmp` was
old.

**Never put a destructive git command inside a block that also runs
diagnostics.** A `git checkout -- <file>` or `git restore` in a block
whose other lines are `git status`/`git log`/`grep` is a trap. If a
revert is wanted, name it on its own line, say what it discards, and
run it as its own paste.

**Python edits with `count != 1` abort checks are the preferred edit
style.** **Idempotent edit scripts.** **Do the count check IN PYTHON
(`str.count`), not with `grep`.** `grep -Fc` does not do multi-line
matches; `grep -Fxc` requires a whole-line match; `str.count` is the
only honest multi-line count.

**NO `set -e` in a runner that can abort or that runs a patch script
that may abort.** It closes the terminal on the non-zero exit and
hides the abort message, which is the signal. Use
`python3 /tmp/patch.py ; git diff --stat` (semicolon) and read the
`ABORT` line. `set -e` **inside** a written `/tmp/*.sh` script is
fine and is the preferred style for the script itself; it is the
*runner* pasted at the prompt that must not have it.

**A one-run diagnostic on the FAILURE path beats three guesses.**
When a branch or an address is in doubt, add the print, build, run
once, read it, and revert -- do not guess twice.

**BEFORE ASSERTING A COUNT, count the occurrences in the file and
write the expected number from that count, not from memory.** **When
a `_Static_assert` fails, read the number in `evaluates to 'N == M'`
-- `N` is the actual value.**

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION -- AND ON A
STATEMENT BOUNDARY, NOT A FRAGMENT INSIDE A STRING LITERAL.** Grep the
file, confirm the count is 1 **and** the match is in the intended
function. Anchor from the file's current bytes (`cat -A`), open
`rb`/`wb` to preserve line endings. If an anchor aborts, **ask for the
current bytes** -- do not guess a second anchor.

**A post-write check string must be copied from the `new` literal,
not retyped.**

**A post-write check is only valid if `new` is unique in the file.**
For a removal edit (`new` is a short, non-unique replacement), skip
the post-check; the `count(old) == 1` pre-check plus a single
`s.replace(old, new, 1)` is sufficient.

**A conflict resolution is a patch script, not a hand edit.** The
`<<<<<<<` / `=======` / `>>>>>>>` block is resolved by a Python
script that extracts the `HEAD` side, deletes exactly the line(s) the
revert removes, preserves the comment bytes byte-for-byte, then
`git add` + `git revert --continue`. **The `grep -n` for the removed
line after the commit is the proof the tree is what the build sees.**

**The build is TWO steps.** `make -C 04_kernel_64bit
FAT_CONFIG=single` builds the kernel; `make -C 05_boot_kernel64
hdd-single.img` builds the image. Running only the second copies a
STALE `kernel.bin`. Always run both, and verify the mtime chain
`source` < `.o` < `kernel.elf` < `kernel.bin` < `hdd-single.img`.
**`make: Nothing to be done` after editing a header means the object
was not rebuilt -- run `make -C 04_kernel_64bit clean` first.**
**After a session of probe edits and reverts, run a full `make clean`
before the next behavioral test.**

**The patch-script write changes the mode of the file it writes in
some environments.** A Python `open(..., "wb")` on the **same path**
is supposed to preserve inode and mode bits, but tracked `.c` and
`.md` files have come out `755` after a patch script several times.
**Verify `stat -c '%a'` before and after every patch script; if the
mode is not `644` on a tracked file, `chmod 644` as its own paste
before staging.**

**`nm` output moves between builds.** Re-run `nm` before using any
address. **And the symbol at an address may be the *next* symbol, not
the function the address is in** -- the first symbol `<=` the address
is the enclosing function; the first `>=` is the *next* function.
Verify both.

**A diagnostic on the SUCCESS path is noise. A diagnostic on the
FAILURE path is signal.**

**A probe that fires nothing is a result.** A silent probe and a loud
sibling together are the evidence.

**gdb printf with no per-process marker is not evidence about a
process.** `%rsp` is the same VA in every process; `$fs_base` is the
same VA in every busybox process. **Print `pid` or `CR3` at every
hit, or the lines are cross-process and the inference is invalid.**
`$fs_base` and `%rsp` alone are not sufficient. This is the single
most expensive mistake this project has made.

**Test harness.** `./run` (no arg) = clean rebuild + fresh
`hdd-single.img` + `-enable-kvm -cpu host -display sdl`, no freeze,
gdb stub on `:1234`, `capture.txt` written (overwritten each run --
**copy it aside after any failure or pass you want to keep**).
`./run debug` adds `-S` (frozen at reset; **the guest does nothing
until gdb attaches and `continue`s -- if it looks frozen at a black
screen, that is the `-S`, not a fault**). **`./run tcg` /
`./run tcg-debug`** = the same flow in TCG (`-cpu max`, no
`-enable-kvm`). The Makefile run targets also use `-cpu max` + TCG --
**state which launcher was used.** Default cwd `/root`.

**In-guest gate.** `cd /`, then `canary --full` (28/28) and
`/root/scripts/test.sh` (57/57). **Plus the two shell tests this
session added:** `sleep 100` then `^C` returns the prompt; `exit`
from `ash` -> `donix>` -> `busybox sh` -> the ash prompt. **Use
multiple `^Z`/`fg` cycles.** One is not always enough to reach an
intermittent fault.

---

## Releases

**Before a release:** the scratch `2026MMDD-*` tags are annotated and
are the source for the version summary. **The scratch tags are
PRESERVED until the user decides to push to GitHub, and are dropped
ONLY after the summary is written and reviewed by the user.** A
session does NOT drop them on its own initiative.

**The release procedure** (on `dev`, pre-release):

1. **Patch the docs first.** Update this file's release paragraph and
   (optionally) `docs/version-history.md`. Commit.
2. **Bump the version string in `kmain.c` manually** (the banner).
   Commit.
3. **Tag** `vX.Y.Z` annotated, on `dev`, summary from the scratch
   tags' annotations and `vPREV..HEAD`:

       git tag -a vX.Y.Z -m "<version summary>"

4. **Drop the scratch tags locally** (the tag and summary now exist):

       for t in $(git tag --list '2026*'); do git tag -d "$t"; done

5. **Push `dev`, then the tag:**

       git push origin dev
       git push origin vX.Y.Z

6. **Merge to `main` with `--no-ff`** (keeps the merge point in
   `main`'s history), push, return to `dev`:

       git checkout main
       git merge --no-ff dev -m "<version summary>"
       git push origin main
       git checkout dev

Do not use `git push --tags` -- it would push the scratch tags. Push
`vX.Y.Z` by name.

**The GitHub push policy.** **Nothing is pushed until `vX.Y.Z` is
ready and verified.** `origin/dev` is not force-pushed; every unpushed
`dev` commit is intentional work. Run `git log --oneline
origin/dev..dev | wc -l` for the current count.

**Current release state.** **`v0.6.19` is the last pushed release.**
`dev` is at `52e1569`; its parents are `0f97048`, `e128c92`,
`54e3b2d`, `6ca3690`, `8dc6f99`. Ahead **34**, clean, **stash
present**. `origin/dev` = `ba56b3d`. Scratch tag
`20261104-gettimeofday-time` on `5a3b65e`. Work is now
pre-`v0.6.20`. **There is no release gate recorded for `v0.6.20`; if
a gate is wanted it is set in this file before that release session,
not during it. The user has said not to move to `v0.6.20` until they
say so.**

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 ABI, natively. Newlib is gone. Userland
is a tracked source tree at `userland/musl/`. Phase B (busybox) is
well underway: the shell is interactive, pipelines and redirection
work, job control works (`^C`, `^Z`, `jobs`, `fg`), and the applet set
includes `bc`, `dc`, `diff`, `du`, `awk`, `date`, `uptime`, `time` in
addition to the file/text applets. **`tar` creates, lists and extracts
correct archive members, including directory members.**
**`mkdir -p` works.**
`date`/`uptime` report seconds since boot as a 1970-epoch date (no RTC
-- the honest base). **`time` builds, its vfork parent-reap path works,
but `time`'s own startup still faults on a forked child; `time` is not
usable as a command today.** See `docs/open-issues.md`'s
"The `time true` fault, after the fourth-session reverts".

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`. Open issues:
`docs/open-issues.md`.

---

## Where we are

### Current baseline

- `test.sh` **57/57**, `canary --full` **28/28** at every gate point
  this session, on a fresh boot. **Re-run both on a fresh boot before
  trusting them.**
- `date` prints `Thu Jan 1 00:00:44 UTC 1970`, `uptime` prints
  `up 0 min`, load `0.00` -- correct for donix.
- `mktemp` works (both `/tmp/tmp.XXXXXX` forms).
- **`^Z` on a foreground child -> `[1]+ Stopped`; `jobs` lists it; `fg`
  resumes. `^C` -> SIGINT reaches the foreground pgrp; the console
  survives.** Verified in the fourth-session boot.
- **`exit` from `ash` -> `donix>` (the musl_sh boot shell);
  `busybox sh` at `donix>` -> the ash prompt.** Verified.
- **`mkdir -p /b/c/d` succeeds silently**, `ls /b/c/d` empty,
  `mkdir /b` prints `File exists`, `mkdir ""` prints `No such file
  or directory`. `umask` returns `0022` and round-trips through
  `077` and `022`.
- **`pipe7e` reaches `iter 4000` without a `#PF`** (`54e3b2d`).
- **`time` is not usable**; `true` alone works; `time` (no args)
  prints usage. **`/usr/bin/TIME` and `/bin/true` do not exist.**
- **`time true` faults** at the busybox-side signature (see
  `docs/open-issues.md`). The shell prints `Command terminated by
  signal 11` and returns to the prompt. **No `CR2=0x34` fault since
  `1bf2ca2`.**
- The C is not observed since the 2026-11-02 disk-path fixes; still
  open -- evidence, not a close.

### Next -- the highest-value work

1. The pre-session `0x40FBC0` probe. Scoped in `docs/open-issues.md`.
   **`hbreak *0x40fbc0` with `$rsp` and `$fs_base`, then a watchpoint
   on the popped slot with `$rip`.**
2. `sys_mkdir` `-ENOTDIR` vs `-EEXIST` for the file case.
3. Item 10 SSE.
4. Item 9 privilege model -- **with the "is this worth a session"
   note.**
5. The `time` accounting bug (separate; do not fix with the `time
   true` fault).
6. The `^C` intermittence -- recorded, not diagnosed, race-shaped,
   separate from `time true`.

### Refuted -- do NOT re-walk

See the **WHAT WAS REFUTED** section above. It is the single list;
this section is a pointer, not a duplicate.

### Open issues

The full list is `docs/open-issues.md`. The numbered items are 9 (no
privilege model) and 10 (no SSE save/restore). **Read the section
"The `time true` fault, after the fourth-session reverts" before doing
anything with `time`/`time true`/`^C`/`busybox sh`.**

---

## State on disk

Kernel sources: `04_kernel_64bit/`. `capture.txt` is gitignored,
overwritten each `./run` -- **copy it aside after any failure or pass
you want to keep.**

**`~/code/testme` is the `v0.6.19` reference tree (`ba56b3d`,
tagged).** It is the known-good comparison. **Do not modify it.**

**Interactive applet use does NOT need `--install`.**  BusyBox is
built with `CONFIG_FEATURE_SH_STANDALONE=y`, so `ash` recognizes the
compiled-in applet names and runs them in-process -- `date`, `time`,
`ls`, `cat`, `true` all work on a fresh image with only
`/bin/busybox` and `/bin/sh` staged.  Verified 2026-11-04: `date`
printed the epoch date before any `--install`.

**`--install` is needed only for path-based exec** -- a shebang such
as `#!/bin/date`, or a program that `execve("/bin/date")`.  Without
it, `/bin/date` is `f_open -> 4` (`FR_NO_FILE`) and the shell prints
`sh: /bin/date: not found`.  After `/bin/busybox --install -s /bin`
the symlink exists and `/bin/date` runs.  `/bin/sh` is staged by the
build as a copy of busybox, so shebang lines that name `/bin/sh`
work without `--install`.

The absolute path is required for `--install` itself: busybox reads
`/proc/self/exe` to find itself, donix has no `/proc/self/exe`, and
it falls back to `argv[0]` -- which must start with `/`.  **Note:**
`busybox --install -s /bin` prints `Operation not permitted` for
`/bin/busybox` and `/bin/sh` (the `FR_EXIST` -> `-EPERM_` mapping for
those two, which are already there) and creates the rest.  That is
expected; `ls /bin` is the check.

**Correction to an earlier claim:** a `sh: date: not found` seen in a
prior session was a *missing applet in the binary* (the `testme`
busybox omitted `date`/`time`/`uptime`), not a missing symlink.  The
rebuild from `configs/busybox.config` fixed it.  Do not diagnose
`not found` for an applet as "run `--install`" without checking
`busybox --list` first.

**Before `--install`, `/bin` contains only `busybox` and `sh`.**
There is no `/bin/true`, `/bin/echo`, `/bin/cat`; after
`--install -s /bin` the applet names are symlinks (see above).
**`time true` does NOT work** -- it faults at the `static_init_tls`
signature (see the `time` section). The `true` applet itself works
when run directly; it is `time`'s re-exec of busybox that faults.
`time /bin/true` fails at `f_open` with `FR_NO_FILE` (there is no
`/bin/true` file). Use `/usr/bin/HELLO`, `/usr/bin/ECHO`,
`/usr/bin/CAT` for real-file control tests.

**`third_party/`** is **gitignored** -- do not commit edits to it.
`third_party/busybox/` has `busybox` (stripped) and
`busybox_unstripped`; the `.map` file is **not** present, so `nm -n` +
`objdump -d` are the symbol tools, and `readelf -lW` gives the program
headers. `third_party/musl-install/` is gitignored and is needed for
the build. If it's missing after a `git clean -xdf`, copy it back from
a working tree, or rebuild it with `toolchain/install_musl.sh`.
**`configs/busybox.config`** is the busybox build config; `CONFIG_TIME=y`,
`CONFIG_TRUE=y`, `CONFIG_LS=y`, `CONFIG_FEATURE_PREFER_APPLETS=y`,
`CONFIG_FEATURE_SH_STANDALONE=y`, `CONFIG_SH_IS_ASH=y`. Turning more
applets on will **not** touch the `time true` fault; do not edit this
config to chase it.

**`04_kernel_64bit/fatfs/ff.c`** is tracked and has two `sync_window`
fixes; any further edit there must re-run the full gate.

**`~/code/saved`** -- not a baseline. Run
`git -C ~/code/saved status --short` before any comparison.

**The reset baseline.** `dev` was reset to `873be27`, tag
`reset-baseline-873be27`. `efc8e08` is not on `dev`; it survives as a
commit object. Treat its guards as **refuted**.

**The 2026-11-02 performance and correctness commits** (six, on top of
`f902af6`): `78216bc`, `ea4dc2e`, `e52bd2f`, `905bc70`, `fdbcbc7`,
`5db42f2`, `6df4fef`.

**The 2026-11-03 release commits:** `c0126d4`, `28272f9`, `b0e9cce`,
`9a36e76`, `ef090f4` (docs), and `ba56b3d` (version banner `v0.6.19`,
tagged).

**The 2026-11-04 commits (31, on top of `9b62951`):**

- `5a3b65e` -- syscalls 96/201, tagged
  `20261104-gettimeofday-time`.
- `ea0986e` -- `borrows_cr3` guard.
- `dca2eff` -- busybox config: `DATE`, `UPTIME`, `TIME`.
- `1aaca0d` -- `sys_vfork` (58), block only.
- `6ca941b` -- `vmm_create_address_space` (first version, superseded).
- `c652c8f` -- `vmm_create_address_space` clone-then-unmap.
- `e5081e1` -- docs: record the open vfork-child fault.
- `8514fca` -- docs: the previous handoff rewrite.
- `a885888` -- `sys_vfork`: clear `block_kind`/`wait_pid` on resume.
- `3f81fb1` -- `sys_vfork`: save the nine caller-saved GPRs before the
  parent blocks.
- `001a756` -- `sys_execve`: fresh cr3 for the vfork child + exec
  wake.
- `c1bd883` -- `sys_vfork`: zero the child's callee-saved GPRs.
  **Reverted by `325e85b`.**
- `0a9ca16` -- `process.c`: accept `BLOCK_KIND_VFORK`.
- `f57299b` -- `sys_vfork`: zero the child's caller-saved GPRs.
  **Reverted by `325e85b`.**
- `a10e585` -- `sys_execve`: write the auxv.
- `325e85b` -- revert of `c1bd883` + `f57299b`.
- `32b3417` -- docs: the morning handoff rewrite.
- `fb188f0` -- **`interrupts`: `exception_frame_t` matches the
  20-slot stub frame.**
- `1bf2ca2` -- **`context_switch`: restore `fs_base` on resume from
  `next->fs_base` (`0x378`, asserted).**
- `4704b8a` -- docs: the evening handoff rewrite.
- `9cf7380` -- docs: replace the refuted
  `lbb_prepare`/`run_applet_and_exit` NEXT with the `0x40FBC0`
  probe.
- `ddd73ba` -- docs: two post-close corrections.
- `4312aab` -- **`syscalls`: `set_tid_address` `clear_child_tid`
  (218).**
- `ae97ae9` -- **`syscalls`: `umask(95)`; `mkdir("/")` and existing
  names return `EEXIST`; `mkdir -p` unblocked.**
- `86c8864` -- docs: second-session handoff rewrite + open-issues
  patch.
- `939e3ef` -- **`syscalls`: `sys_execve` -- set
  `pcb->user_rsp = rsp_init`.**  **Reverted by `0f97048`.**
- `153dda9` -- **`syscalls`: `sys_execve` -- 16-byte-align
  `rsp_init` per SysV AMD64 ABI.**  **Reverted by `e128c92`.**
- `8dc6f99` -- docs: open-issues -- third-session `time true` status
  and next probe.
- `6ca3690` -- handoff: rewrite for the third session at `8dc6f99`.
- `54e3b2d` -- **`syscalls`: `sys_execve` -- clear
  `clear_child_tid` on execve.**  Fixes the `pipe7e` `#PF`.
- `e128c92` -- **Revert of `153dda9`.**
- `0f97048` -- **Revert of `939e3ef`.**
- `52e1569` -- docs: open-issues -- fourth-session reverts, `pipe7e`
  fix, gate gap.

**Stash.** `stash@{0}`: "vfork 3b (wake arm + sys_vfork) +
diagnostics; time true intermittent, not a complete fix." This is a
preserved diagnostic trail. **Do not drop, do not pop.**

---

## One-line summary

**`v0.6.19` is released; `dev` is at `52e1569` (parents `0f97048`,
`e128c92`, `54e3b2d`, `6ca3690`, `8dc6f99`), ahead 34, clean, stash
present, nothing pushed.** The 2026-11-04 fourth session landed
**`54e3b2d`** (kept -- `sys_execve` clears `clear_child_tid` on
exec, fixing the `pipe7e` kernel-mode `#PF`) and **reverted
`939e3ef`** (`0f97048`) and **`153dda9`** (`e128c92`), because both
moved the `time true` fault forward but broke the interactive shell:
`939e3ef` broke `^C` on a foreground child, `153dda9` broke
`busybox sh` re-entry. **The gate (`canary --full` 28/28 +
`test.sh` 57/57) was green with both applied and covers neither shell
behavior -- that is this session's lasting lesson.** Baseline
verified in one boot: 28/28, 57/57, `^C` and `^Z`/`fg` work,
`busybox sh` re-entry works, `pipe7e` reaches `iter 4000`, and
`time true` faults at its pre-session busybox-side signature.
**`time` is not usable.** **NEXT: the pre-session `0x40FBC0` probe
with `$rsp` and `$fs_base`, then a watchpoint on the popped slot --
read `docs/open-issues.md`'s "The `time true` fault, after the
fourth-session reverts" first.** **Do not re-apply `939e3ef` or
`153dda9` without the `^C` and `busybox sh` tests.** Every earlier
hypothesis is in the Refuted list. Read `docs/contract.md` and this
file's NEXT before any code. **Do not move to `v0.6.20` until the
user says so.**
