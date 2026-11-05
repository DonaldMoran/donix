# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-04 (fifth session), at `62b5433`.
**`v0.6.19` is the last pushed release.** `dev` is at `62b5433`; its
parents are `711ac16`, `695c53c`, `2bba981`, `c8ef340`, `63cb81b`.
`origin/dev` = `ba56b3d`. Ahead **40**, clean tree, **stash present**
(`stash@{0}`: vfork 3b diagnostics; preserved, do not drop). No push,
no tag this session; banner still `v0.6.19`. `main` = `7e4748b` (the
`--no-ff` merge of `v0.6.19`). Scratch tag `20261104-gettimeofday-time`
is on `5a3b65e` (still preserved).

**`~/code/testme` is the `v0.6.19` reference tree (`ba56b3d`, tagged).**
It is the known-good *boot* comparison. **Do not modify it.** But its
busybox **omits `date`/`time`/`uptime`** from its applet set even
though `configs/busybox.config` enables them, so `cmp`-identical to
`testme` confirms ELF *shape*, not *applet set*. The applet-set
oracle is `busybox --list`; the build of record is
`configs/busybox.config` plus `toolchain/musl-gcc.sh`.

---

## START HERE

**The root cause of the `time` fault is settled and is not in
doubt:** the static busybox ELF has no `PT_PHDR` and no `_DYNAMIC`,
so musl's `static_init_tls` computes `base = 0` and the TLS image
copy faults. This is a busybox-link problem, not a kernel problem.
Read `docs/open-issues.md`'s section "The `time true` fix attempts of
the 2026-11-04 fifth session" before touching `time`.

**Three fix routes are refuted; do not re-walk them.**
`-Wl,--dynamic-linker` has no effect on a static non-PIE link;
`-fuse-ld=lld` emits `PHDR` but moves the base `0x400000`->`0x200000`,
adds `PT_INTERP`, and loses the applets; a from-scratch `SECTIONS`
block discards the real sections. The measurements are in that
open-issues section.

**Reproducibility is a first-class constraint.** A fix must be
tracked and built by the clone script, or a cloner does not get it.
`busybox_ldscript` is gitignored and binutils-version-dependent, so
the linker-script route is a local experiment, not a shippable fix.

**`time` is one of six `NEXT` items, not the only one.** Items 2-4
(`sys_mkdir -ENOTDIR`, SSE save/restore, the privilege model) are
independent, small, and can be taken without touching `time`. Item 1
is the long pole. Read `NEXT` in full.

**Do not re-apply `939e3ef` or `153dda9`.** Both were reverted
(`0f97048`, `e128c92`). They moved the `time true` fault forward but
broke the shell: `939e3ef` breaks `^C` on a foreground child,
`153dda9` breaks `busybox sh` re-entry. **Any `sys_execve` stack
change must be tested with both before it lands.** See the gate gap
below.

**Stale fault traces -- do not reason from these.** `num 13`
(`rt_sigaction`) / `rcx 0x44CC0D`, `RIP 0` / `CR2 0` / `num 61`
(`wait4`), and the VA-0 identity-map `CR2 0` / `pte 0x3` traces in
`docs/open-issues.md` are all from the reverted `153dda9`/`939e3ef`
state. The **live** fault is the busybox-side signature: `num 218`
(`set_tid_address`), `last sysret rcx 0x40FBC0`, `RSP =
g_user_rsp_save + 0x180`, with the `__init_tls` frame intact.

**`time true` is not a race.** It fails deterministically on the first
invocation in a boot where everything else passes.

---

## WHAT WORKS (verified in the fifth-session boot)

- `canary --full` **28/28**
- `test.sh` **57/57**
- `pipe7e` reaches `iter 1000`+ without a `#PF`
- **`^C` on a foreground `sleep 100`** returns the prompt
- **`^Z` -> `[1]+ Stopped`; `jobs` lists it; `fg` resumes**
- **`exit` from `ash` -> `donix>`; `busybox sh` at `donix>` -> the
  ash prompt** (re-entry works)
- **`date`** prints `Thu Jan 1 00:00:28 UTC 1970`; **`time`** (no
  args) prints usage
- **Interactive applets do NOT need `--install`**:
  `CONFIG_FEATURE_SH_STANDALONE=y` makes `ash` dispatch compiled-in
  applets in-process. Verified 2026-11-04: `date` printed the epoch
  date before any `--install`.
- **`--install -s /bin` is needed only for path-based exec**
  (`#!/bin/date`, `execve("/bin/date")`). Without it, `/bin/date` is
  `f_open -> 4` (`FR_NO_FILE`) and the shell prints
  `sh: /bin/date: not found`.
- **`mkdir -p` works**; `mkdir /b` prints `File exists`; `umask`
  returns `0022` and round-trips.
- **`tar`** creates/lists/extracts correctly, including directory
  members.

## WHAT IS BROKEN

- **`time true` / `time ls` fault** at the busybox-side signature
  (`num 218`, `rcx 0x40FBC0`, `RSP = g_user_rsp_save + 0x180`). The
  shell prints `Command terminated by signal 11` and returns to the
  prompt. **`time` is not usable as a command.**
- **`time`'s accounting is garbage**: `real` is plausible but `user`
  / `sys` print about `152710235h`. This is `getrusage`/`times`
  returning nonsense on donix -- **a kernel accounting bug, separate
  from the `time true` fault.** A hand-written `time` would inherit
  it.
- **The `^C` intermittence** (works once, fails later) is
  race-shaped and **separate** from the `time true` fault. Not
  diagnosed.
- **No `CR2=0x34` fault since `1bf2ca2`** -- that one is closed.
- **The C** is not observed since the 2026-11-02 disk-path fixes;
  still open -- evidence, not a close.

---

## NEXT -- the highest-value work, in order

The full ranking with the reproducibility analysis is in
`docs/open-issues.md`'s fifth-session section. **No decision has been
made between the `time` options.** Items 2–4 are independent of the
`time` question and can be taken without touching it; item 1 is the
long pole.

1. **The `time` question: which route.** Four options, with the
   constraint that a fix must be tracked and built by the clone
   script:
   - **Own `time` binary** (`userland/musl/apps/time.c` + image
     staging line). Tracked, built by the clone script, low risk.
     Gives a working `time`; does **not** fix the busybox `time
     true` fault, and inherits the `getrusage` accounting bug.
   - **musl `static_init_tls` patch** via a tracked
     `toolchain/*.patch` applied by `toolchain/install_musl.sh`.
     Reproducible; fixes the fault itself. **Blast radius: every
     static musl binary's startup.** The gate gap applies; the
     narrowest guard is for static `ET_EXEC` with `base == 0`.
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
3. **`docs/open-issues.md` item 10 -- SSE save/restore.** The test
   is in the item; two separable commits (kernel save/restore, then
   userland flag removal).
4. **`docs/open-issues.md` item 9 -- the privilege model.**
   **Before starting, ask what it unlocks.** If "nothing," skip.
5. **The `time` accounting bug** (item 5 above, under WHAT IS
   BROKEN). Kernel `getrusage`/`times` accounting; separate from the
   `time true` fault. **Do not fix in the same session as the `time
   true` fault.**
6. **The `^C` intermittence.** Race-shaped, separate, not
   diagnosed. Use multiple `^Z`/`fg` cycles to reach it.

---

## THE GATE GAP

**`canary --full` (28/28) and `test.sh` (57/57) do not cover `^C` on
a foreground child or `busybox sh` re-entry.** Both `939e3ef` and
`153dda9` passed the green gate and broke one of those two shell
behaviors. **Any change to `sys_execve`'s user stack, or to musl's
startup path, must be tested with:**

- **`sleep 100` then `^C`** -> the prompt returns
- **`exit` from `ash` -> `donix>` -> `busybox sh`** -> the ash
  prompt returns

before it is committed. **A green `canary`/`test.sh` is not
sufficient.**

---

## WHAT WAS REFUTED -- do NOT re-walk.

- **`-Wl,--dynamic-linker=/lib/ld-musl-x86_64.so.1` as a way to
  force `PT_PHDR`.** No effect on a static non-PIE link; the binary
  is byte-identical in shape.
- **`-fuse-ld=lld` as a fix.** Emits `PHDR` but moves the base
  `0x400000`->`0x200000` (entry `0x214140`), adds `PT_INTERP`, loses
  `date`/`time`/`uptime`, and the boot faults at `CR2 0x400040`
  (auxv `AT_PHDR`).
- **A from-scratch `SECTIONS` block in `busybox_ldscript`.** The
  mechanism works (`trylink` uses the file, `PHDR` appears, no
  `INTERP`), but the hand-built `SECTIONS` discarded the real
  sections (`.text` `0x2ca` instead of `0x541e5`, no applets).
- **`testme` as an applet-set reference.** Its busybox omits
  `date`/`time`/`uptime`. Use `busybox --list`.
- **Dynamic linking as a small option.** Kernel has no `PT_INTERP`
  handling, no `/lib` staging, static-only userland build, no
  `libc.so`.
- **`939e3ef`** (`pcb->user_rsp = rsp_init`). Broke `^C` on a
  foreground child. Reverted by `0f97048`.
- **`153dda9`** (16-byte-align `rsp_init`). Broke `busybox sh`
  re-entry. Reverted by `e128c92`.
- **The `0x44CC0D` / `0x44AC73` probe addresses.** Stale, downstream
  of the two reverted changes.
- **`time true` as a race.** Deterministic on the first invocation
  in a boot where everything else passes.
- **The `race_probe` `RACE PROBE DUMP` as a mechanism.** It reports
  the standing `wake_no_waiter == writers_nonzero` question; it does
  not mean the fault is a race.
- **`time /bin/true` as a control.** `/bin/true` does not exist.
  Valid real-file controls: `/usr/bin/HELLO`, `/usr/bin/ECHO`,
  `/usr/bin/CAT`.
- **`$fs_base` as a per-process marker.** Same VA across busybox
  processes. **Only `pid` or `CR3` distinguishes.**
- **`argv_scratch` / `envp_scratch` as the cause.** `argv_scratch`
  is 4 KB; `envp_scratch` is kmalloc'd.
- **A `call *%rax` with `rax = 0` as the fault.** The dumps show a
  `ret` popping a zeroed slot.
- **The boot-time `pid 3` `num 7` fault as a `time true`
  signature.** Recorded, not chased. Not `time true`.
- **The `clear_child_tid` store in `process_exit` as a `^C`
  cause.** `54e3b2d` (the `sys_execve` reset) is kept; the `^C`
  failure was `939e3ef`.
- **`sh: date: not found` as a missing symlink.** It was a *missing
  applet in the binary* (the `testme` busybox omitted
  `date`/`time`/`uptime`). Check `busybox --list` before
  diagnosing.

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
or cleared: re-write it in the same paste that runs it, or `cat` it
and read before running.** This session lost several `/tmp` scripts
to clearing; durable captures go under `~/code/donix-backup-*` or
another non-`/tmp` path.

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
current bytes** -- do not guess a second anchor. **When the anchor is
a full paragraph, use the full paragraph** -- first byte to last byte
-- not a prefix that ends mid-sentence; a prefix replacement leaves
the sentence tail behind.

**A post-write check string must be copied from the `new` literal,
not retyped.**

**The post-write check must assert the old anchor is ABSENT
(`after.count(old) == 0`), not only that the new text is present.**
A presence-only check passes on a partial replacement; the absence
check does not. This session shipped an orphaned sentence tail
because the check tested only `new`.

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
`/root/scripts/test.sh` (57/57). **Plus the two shell tests:**
`sleep 100` then `^C` returns the prompt; `exit` from `ash` ->
`donix>` -> `busybox sh` -> the ash prompt. **Use multiple
`^Z`/`fg` cycles.** One is not always enough to reach an
intermittent fault. **`--install -s /bin` is needed only if a test
execs an applet by path**; interactive applet use does not require
it.

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
`dev` is at `62b5433`; its parents are `711ac16`, `695c53c`,
`2bba981`, `c8ef340`, `63cb81b`. Ahead **40**, clean, **stash
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
usable as a command today.** See `docs/open-issues.md`'s fifth-session
section.

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`. Open issues:
`docs/open-issues.md`.

---

## State on disk

Kernel sources: `04_kernel_64bit/`. `capture.txt` is gitignored,
overwritten each `./run` -- **copy it aside after any failure or pass
you want to keep.**

**`~/code/testme` is the `v0.6.19` reference tree (`ba56b3d`,
tagged).** It is the known-good *boot* comparison. **Do not modify
it.** Its busybox omits `date`/`time`/`uptime`; see START HERE.

**Interactive applet use does NOT need `--install`.** BusyBox is
built with `CONFIG_FEATURE_SH_STANDALONE=y`, so `ash` recognizes the
compiled-in applet names and runs them in-process -- `date`, `time`,
`ls`, `cat`, `true` all work on a fresh image with only
`/bin/busybox` and `/bin/sh` staged. Verified 2026-11-04: `date`
printed the epoch date before any `--install`.

**`--install` is needed only for path-based exec** -- a shebang such
as `#!/bin/date`, or a program that `execve("/bin/date")`. Without
it, `/bin/date` is `f_open -> 4` (`FR_NO_FILE`) and the shell prints
`sh: /bin/date: not found`. After `/bin/busybox --install -s /bin`
the symlink exists and `/bin/date` runs. `/bin/sh` is staged by the
build as a copy of busybox, so shebang lines that name `/bin/sh`
work without `--install`.

The absolute path is required for `--install` itself: busybox reads
`/proc/self/exe` to find itself, donix has no `/proc/self/exe`, and
it falls back to `argv[0]` -- which must start with `/`. **Note:**
`busybox --install -s /bin` prints `Operation not permitted` for
`/bin/busybox` and `/bin/sh` (the `FR_EXIST` -> `-EPERM_` mapping for
those two, which are already there) and creates the rest. That is
expected; `ls /bin` is the check.

**Correction to an earlier claim:** a `sh: date: not found` seen in a
prior session was a *missing applet in the binary* (the `testme`
busybox omitted `date`/`time`/`uptime`), not a missing symlink. The
rebuild from `configs/busybox.config` fixed it. Do not diagnose
`not found` for an applet as "run `--install`" without checking
`busybox --list` first.

**Before `--install`, `/bin` contains only `busybox` and `sh`.**
There is no `/bin/true`, `/bin/echo`, `/bin/cat`; after
`--install -s /bin` the applet names are symlinks (see above).
**`time true` does NOT work** -- it faults at the `static_init_tls`
signature. The `true` applet itself works when run directly; it is
`time`'s re-exec of busybox that faults. `time /bin/true` fails at
`f_open` with `FR_NO_FILE` (there is no `/bin/true` file). Use
`/usr/bin/HELLO`, `/usr/bin/ECHO`, `/usr/bin/CAT` for real-file
control tests.

**`third_party/`** is **gitignored** -- do not commit edits to it.
`third_party/busybox/` has `busybox` (stripped) and
`busybox_unstripped`; the `.map` file is **not** present, so `nm -n` +
`objdump -d` are the symbol tools, and `readelf -lW` gives the program
headers. `third_party/musl-install/` is gitignored and is needed for
the build. If it's missing after a `git clean -xdf`, copy it back from
a working tree, or rebuild it with `toolchain/install_musl.sh`.
**`configs/busybox.config`** is the busybox build config;
`CONFIG_TIME=y`, `CONFIG_TRUE=y`, `CONFIG_LS=y`,
`CONFIG_FEATURE_PREFER_APPLETS=y`, `CONFIG_FEATURE_SH_STANDALONE=y`,
`CONFIG_SH_IS_ASH=y`. Turning more applets on will **not** touch the
`time true` fault; do not edit this config to chase it. A bare
`make -C third_party/busybox` uses host gcc and produces a **glibc**
binary; the musl build is
`make -C third_party/busybox CC="$PWD/toolchain/musl-gcc.sh"`, and it
runs through `userland/musl/Makefile`'s `musl-userland` target (which
`./run` reaches via `hdd-single.img`). `userland/musl/build/.busybox.stamp`
gates that rebuild and depends only on `.config` -- any change to the
busybox build flags or binary requires
`rm -f userland/musl/build/.busybox.stamp userland/musl/build/busybox.elf`
before the next build.

**`04_kernel_64bit/fatfs/ff.c`** is tracked and has two `sync_window`
fixes; any further edit there must re-run the full gate.

**`~/code/saved`** -- not a baseline. Run
`git -C ~/code/saved status --short` before any comparison.

**The reset baseline.** `dev` was reset to `873be27`, tag
`reset-baseline-873be27`. `efc8e08` is not on `dev`; it survives as a
commit object. Treat its guards as **refuted**.

**Stash.** `stash@{0}`: "vfork 3b (wake arm + sys_vfork) +
diagnostics; time true intermittent, not a complete fix." This is a
preserved diagnostic trail. **Do not drop, do not pop.**

---

## One-line summary

**`v0.6.19` is released; `dev` is at `62b5433`, ahead 40, clean, stash
present, nothing pushed.** The `time` root cause is settled (no
`PT_PHDR` / no `_DYNAMIC` -> `static_init_tls` base 0), three fix
routes are refuted by measurement, and the four remaining options are
recorded with their reproducibility in `docs/open-issues.md`'s
fifth-session section. **No decision has been made between them.**
Baseline verified in one boot: `canary --full` 28/28, `test.sh` 57/57,
`pipe7e` past `iter 1000`, `^C`/`^Z`/`fg` work, `busybox sh`
re-entry works, `date`/`time` (no args) work, interactive applets do
not need `--install`. **`time true` faults at the busybox-side
signature; `time` is not usable as a command.** The `time` accounting
bug is separate and kernel-side. **NEXT: the four `time` options (no
decision), or `sys_mkdir -ENOTDIR`, or SSE, or the privilege model
(items 2-4 are independent of `time`).** Do not re-walk the refuted
routes or the stale fault traces. **Do not move to `v0.6.20` until
the user says so.**
