# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-04 (third session), at `8dc6f99`.
**`v0.6.19` is the last pushed release.** `dev` is at `8dc6f99`; its
parents are `153dda9`, `939e3ef`, `86c8864`, `ae97ae9`, `4312aab`.
`origin/dev` = `ba56b3d`. Ahead **29**, clean tree, **stash present**
(`stash@{0}`: vfork 3b diagnostics; preserved, do not drop). No push,
no tag this session; banner still `v0.6.19`. `main` = `7e4748b` (the
`--no-ff` merge of `v0.6.19`). Scratch tag `20261104-gettimeofday-time`
is on `5a3b65e` (still preserved).

---

## START HERE

**Before doing anything else with `time`, `time true`, or
`time ls /`, read `docs/open-issues.md`'s section "The `time true`
fault, after the third-session fixes".** It names the two remaining
fault signatures, the two `sys_execve` fixes that already landed
(`939e3ef`, `153dda9`), the gate status, and the **exact next probe**
(`hbreak *0x44cc0d` / `hbreak *0x44ac73` printing `$rsp` and
`$fs_base`, plus a `serial_print` in `sys_execve` printing
`rsp_init` / `self->user_rsp` / `self->kernel_stack_top[-9]`).

**You do not need the conversation history.** You need this file's
`WHERE WE ARE` and `NEXT` sections, and that one section of
`docs/open-issues.md`. Everything else is either in `docs/` (read on
demand) or in the Refuted list below (do not re-walk).

**Do not re-run the `0x40FBC0` probe.** It is **closed by
measurement**: the slot was `0x40B163` at `0x40FBC0` in every run of
the third session. The writer is not there.

**Do not re-derive the `user_rsp` or alignment fixes.** They are in
(`939e3ef`, `153dda9`), verified by gate, and they moved the fault
forward.

---

## WHERE WE ARE

**`v0.6.19` is the last pushed release.** `dev` is at `8dc6f99`.
Ahead **29**, clean, stash present. Nothing pushed this session.

**WHAT LANDED THIS SESSION (four commits, all gate-green):**

1. **`86c8864`** (docs) -- second-session handoff rewrite and
   `docs/open-issues.md` patch, committed before this session's code
   work started.
2. **`939e3ef`** (code) -- `syscalls`: `sys_execve` now sets
   `pcb->user_rsp = rsp_init`. `sys_execve` rewrote the
   syscall-entry frame's user-RSP slot (`ktop[-9] = rsp_init`) but
   never updated `pcb->user_rsp`. The exec'ing process returns via
   `sysret` and reads `ktop[-9]` (correct); the **vfork child**
   resumes through `context_switch_resume`, which loads the user-RSP
   slot from `pcb->user_rsp` (`pcb` offset `0x610`, asserted in
   `process.c`). For the child, `pcb->user_rsp` was the stale pre-exec
   value, `0x180` above the new stack's `rsp_init`. **Effect: the
   busybox-side `0x40FBC0` / `+0x180` fault no longer occurs.**
   Gate 28/28 + 57/57.
3. **`153dda9`** (code) -- `syscalls`: `sys_execve` 16-byte-aligns
   `rsp_init` per the SysV AMD64 ABI. `sys_execve` placed `rsp_init`
   at `argv_region_bottom - 8`; `user_stack_top` is 16-aligned, so
   `rsp_init` was 8 mod 16. musl's `_start` does `pop %rsi` then
   `and -16, %rsp`; with the misalignment that `and` is a no-op, so
   `__libc_start_main`'s frame was 8 bytes higher than on Linux and a
   later `ret` popped a never-pushed slot. **Effect: the
   argc/argv/envp/auxv layout is now visibly at `rsp_init`** and the
   child gets past `__libc_start_main` into the applet-dispatch
   region. **Fault moved forward, not eliminated.** Gate 28/28 +
   57/57.
4. **`8dc6f99`** (docs) -- `docs/open-issues.md` new section "The
   `time true` fault, after the third-session fixes", with the
   `$fs_base`-is-not-a-marker correction and the exact next probe.

**`time true` STILL FAULTS, in two signatures, both `RIP 0`,
`CR2 0`, `error 0x15`, `CS 0x33`:**

- **`num 13` (`rt_sigaction`), `last sysret rcx 0x44CC0D`,**
  `RSP = g_user_rsp_save + 0x390`.
- **`num 218` (`set_tid_address`), `last sysret rcx 0x44AC73`,**
  `RSP = g_user_rsp_save + 0x180`.

In both dumps the **entire stack region between `fault_rsp` and
`rsp_init` is zero** -- the popped slot was never written after
startup. That is the remaining bug. **It is not the
`pcb->user_rsp` slot and not the entry alignment.** A `ret` in
busybox's startup pops a slot that is zero on a fresh stack.

**`time` is not usable today; `true` alone works.** Verified in the
third-session boot: `true` (silent, returns) works; `time` (no args,
prints usage) works; `time true` faults; `time ls /` faults.
**`/usr/bin/TIME` does not exist.** The shell's PATH lookup tries it,
gets `FR_NO_FILE`, falls back to `/bin/time` (a busybox applet). That
is expected. `/bin/true` does not exist either. **`time /bin/true`
prints `can't execute '/bin/true': No such file or directory` and
exits 127 -- that is correct behavior, not a control.** Valid real-file
controls are `/usr/bin/HELLO`, `/usr/bin/ECHO`, `/usr/bin/CAT`.

---

## NEXT -- the highest-value work, in order

1. **The `0x44cc0d` / `0x44ac73` probe with `$rsp` and the
   `sys_execve` `serial_print`.** This is the single next step and it
   is scoped in `docs/open-issues.md`. In brief: `./run debug`, two
   `hbreak`s (`*0x44cc0d`, `*0x44ac73`) each with `printf
   "PROBE rsp=%lx fs=%lx\n", $rsp, $fs_base`, plus one
   `serial_print` in `sys_execve` at the `self->user_rsp = rsp_init`
   line printing `rsp_init`, `self->user_rsp`,
   `self->kernel_stack_top[-9]`. Run `time ls /` until it faults. The
   `$rsp` at the last hit compared to `rsp_init` decides:
   - **`$rsp == rsp_init`** -> delivered `_start` `rsp` is right; the
     fault is a busybox/musl startup issue in `third_party/`
     (not editable in-tree). Fix is a **workaround** (documented
     patch, force-initialize the slot, or shrink the frame).
   - **`$rsp != rsp_init`** -> the delivery path is the bug. The two
     candidates are `process_fork`'s
     `frame_rsp = is_user ? pcb->user_stack_top : pcb->kernel_stack_top`
     (`process.c:453`, wrong source for fork/vfork -- should be the
     parent's user `RSP`), and the resume path reading a slot
     `sys_execve` does not write. **Do not change `frame_rsp` without
     the probe result.**
2. **`sys_mkdir` `-ENOTDIR` vs `-EEXIST`** for the file case.
   POSIX wants `-ENOTDIR` when the target exists as a file. Small,
   `sys_mkdir`-local; no current caller depends on it.
3. **`docs/open-issues.md` item 10 -- SSE save/restore.** Unchanged.
   The test is in the item.
4. **`docs/open-issues.md` item 9 -- the privilege model.**
   **Before starting, ask what it unlocks.** If "nothing," skip.
5. **The `time` applet's accounting bug** (`real 0m 0.34s` on some
   runs, `1191h 48m 18s` on others; `user`/`sys` about `152710235h`
   in every run). Separate from the `time true` fault. **Do not fix
   in the same session as the `time true` fault.**
6. **The live `wake_no_waiter == writers_nonzero` observation
   (111 == 111).** Recorded in `docs/open-issues.md`; a standing
   question, not a mechanism. Do **not** chase it on this evidence.

**NOT DONE, NOT THIS SESSION'S WORK.** The C halt
(`context_switch_resume`'s unknown-CS branch) has not been observed
since the 2026-11-02 disk-path fixes. No mechanism was named. Do not
chase it without new evidence.

---

## WHAT WAS REFUTED -- do NOT re-walk.

*This session's additions first.*

- **The `0x40FBC0` probe as the writer-finder.** It fired with
  `slot=0x40b163` and `fs=0x46a138` in every run; the busybox-side
  slot is correct at `0x40FBC0`. **Closed by measurement.** Do not
  re-run it.
- **`$fs_base` as a per-process marker.** It is the **same VA**
  (`0x46a138`) in every busybox process and in most musl binaries.
  `%rsp` is the same VA across processes. **Only `pid` or `CR3`
  distinguishes processes.** A `WATCH` line with `fs=0x46a138` and
  `rip=0x40b061` (a `/usr/bin/*` binary's `__init_libc`) was
  indistinguishable, by `fs` alone, from a busybox process. **The
  working-style rule below is corrected to say: print `pid` or
  `CR3`.**
- **`self->user_rsp = rsp_init` as the sole fix.** It moved the
  busybox-side fault from `0x40FBC0`/`+0x180` to `0x44AC73`/`+0x160`.
  It did not eliminate `time true`.
- **The 16-byte alignment as the sole fix.** It moved the fault
  further (past `__libc_start_main`) but did not eliminate it.
- **`process_fork`'s `frame_rsp = pcb->user_stack_top` as the fix.**
  Named as a **candidate**, not proven. Do not change it without
  the probe result.
- **`time /bin/true` as a control, again.** `/bin/true` does not
  exist; the line is correct behavior for a missing file. **Valid
  real-file controls are `/usr/bin/HELLO`, `/usr/bin/ECHO`,
  `/usr/bin/CAT`.** The `time true` test does **not** depend on a
  second exec of a `true` binary; `time` is a busybox applet and
  `true` is a busybox applet, and `time`'s own startup faults before
  it reaches `true`.
- **`argv_scratch` / `envp_scratch` as the cause.** `argv_scratch`
  is 4 KB, `envp_scratch` is kmalloc'd. Already refuted; unchanged.
- **A `call *%rax` with `rax = 0` as the fault.** The third-session
  dumps show a `ret` popping a zeroed slot, not a `call`.
- **The boot-time `pid 3` `num 7` fault as a `time true` signature.**
  It occurs before any `time true` in the boot, on `pid 3` (the
  shell), and its frame is different. Recorded in
  `docs/open-issues.md`; do not fold into `time true`.

*Earlier, unchanged (see the 2026-11-04 evening and second-session
sections in `git log` for the full list).*  Do not re-walk: the
`cli` window; signal delivery as the zeroer; `memset` as the zeroer;
the sysret slot arithmetic; "the slot flips `0x40B163`→0 in one
process"; the three `exec_path` forms; `.bss`-not-zeroed;
`0x448C6A`/`CR2=0x34` as the root cause (real, separate bug, fixed by
`1bf2ca2`); `lbb_prepare`/`run_applet_and_exit`/`applet_main[rbx]`;
the auxv; `set_tid_address`'s return; mmap; V3/V3b register
inheritance; the `efc8e08` rsp split and `frame_kind`; the four
uncommitted scheduler guards; the `EXIT-REFUSE frame-CS` guard; the
canary `.text` `#PF` probe stack; the physical-address gates on
`.text` PTE zeroing; the `f91d31a` claim; `exec_churn`'s KVM slowness
is the console; `ata_poll_drq` spinning; `exec_churn` can measure the
`sys_fork` eager-copy cost; the `perf kvm stat record` `double free`
is a donix issue; the 2026-11-04 PTAUDIT "22 anomalies"; the
2026-11-04 empty-low-half `vmm_create_address_space`; V3/V3b register
zeroing; the missing auxv; `static_init_tls` at `0x40FBC0` as a
`call *%rax`; `kmain.c`'s self-tests map over the ELF text page.

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
Two steps, both explicit.

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
`ABORT` line. The patch script's own `sys.exit(1)` is enough to
signal failure; `set -e` on top of it is a trap. `set -e` **inside**
a written script is fine and is the preferred style for the script
itself; it is the *runner* that must not have it.

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
(not `os.replace`) is supposed to preserve inode and mode bits, but
two files this session came out `755` after a patch script
(`04_kernel_64bit/user_syscall.c`) and were fixed with `chmod 644`.
**Verify `stat -c '%a'` before and after every patch script; if the
mode is not `644` on a tracked `.c` or `.md` file, `chmod 644` as its
own paste.**

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
same VA in every busybox process. **Print `pid` or `CR3` at every hit,
or the lines are cross-process and the inference is invalid.**
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
`/root/scripts/test.sh` (57/57). **Use multiple `^Z`/`fg` cycles.**
One is not always enough to reach an intermittent fault.

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
`dev` is at `8dc6f99`; its parents are `153dda9`, `939e3ef`,
`86c8864`, `ae97ae9`, `4312aab`. Ahead **29**, clean, **stash
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
"The `time true` fault, after the third-session fixes".

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`. Open issues:
`docs/open-issues.md`.

---

## Where we are

### Current baseline

- `test.sh` **57/57**, `canary --full` **28/28** at every gate point
  this session, on a fresh boot, after both code commits. **Re-run
  both on a fresh boot before trusting them.**
- `date` prints `Thu Jan 1 00:00:44 UTC 1970`, `uptime` prints
  `up 0 min`, load `0.00` -- correct for donix.
- `mktemp` works (both `/tmp/tmp.XXXXXX` forms).
- `^Z` on a foreground child -> `[1]+ Stopped`; `jobs` lists it; `fg`
  resumes. `^C` -> SIGINT reaches the foreground pgrp; the console
  survives.
- **`mkdir -p /b/c/d` succeeds silently**, `ls /b/c/d` empty,
  `mkdir /b` prints `File exists`, `mkdir ""` prints `No such file
  or directory`. `umask` returns `0022` and round-trips through
  `077` and `022`.
- **`time` is not usable**; `true` alone works; `time` (no args)
  prints usage. **`/usr/bin/TIME` and `/bin/true` do not exist.**
- **`time true` and `time ls /` fault at `RIP 0`** in **two
  signatures** (see `docs/open-issues.md`): `num 13` /
  `rcx 0x44CC0D` / `+0x390`, and `num 218` / `rcx 0x44AC73` /
  `+0x180`. The shell prints `Command terminated by signal 11` and
  returns to the prompt. **No `CR2=0x34` fault since `1bf2ca2`.**
- The C is not observed since the 2026-11-02 disk-path fixes; still
  open -- evidence, not a close.

### Next -- the highest-value work

1. The `0x44cc0d` / `0x44ac73` probe with `$rsp` and the
   `sys_execve` `serial_print`. Scoped in `docs/open-issues.md`.
   **Two-branch decision** (`$rsp == rsp_init` -> busybox-only,
   workaround; `$rsp != rsp_init` -> delivery-path fix).
2. `sys_mkdir` `-ENOTDIR` vs `-EEXIST` for the file case.
3. Item 10 SSE.
4. Item 9 privilege model -- **with the "is this worth a session"
   note.**
5. The `time` accounting bug (separate; do not fix with the `time
   true` fault).
6. The `111 == 111` pipe equality -- recorded, not chased.

### Refuted -- do NOT re-walk

See the **WHAT WAS REFUTED** section above. It is the single list;
this section is a pointer, not a duplicate.

### Open issues

The full list is `docs/open-issues.md`. The numbered items are 9 (no
privilege model) and 10 (no SSE save/restore). **The 2026-11-04 third
session added the section "The `time true` fault, after the
third-session fixes"** -- read that section before doing anything
with `time`/`time true`. **The `$fs_base`-is-not-a-per-process-marker
correction is in that section** and the working-style rule above has
been updated to match.

---

## State on disk

Kernel sources: `04_kernel_64bit/`. `capture.txt` is gitignored,
overwritten each `./run` -- **copy it aside after any failure or pass
you want to keep.**

**After a fresh image, one manual step:** the image stages
`/bin/busybox` alone. To get the applet symlinks (including `/bin/sh`,
which shebang needs), boot and run `/bin/busybox --install -s /bin`.
The absolute path is required: busybox reads `/proc/self/exe` to find
itself, donix has no `/proc/self/exe`, and it falls back to `argv[0]`
-- which must start with `/`. This is a manual step, not part of the
build. **Note:** on this image, `busybox --install -s /bin` prints
`Operation not permitted` for `/bin/busybox` and `/bin/sh` (the
`FR_EXIST` -> `-EPERM_` mapping for those two, which are already
there) and creates the rest. That is expected; `ls /bin` is the
check.

**`/bin` contains only `busybox` and `sh`.** There is no `/bin/true`,
`/bin/echo`, `/bin/cat`. **`time true` works because `true` is a
busybox applet** -- but `time`'s own startup faults today, so `time
true` is not a working test. `time /bin/true` fails at `f_open` with
`FR_NO_FILE` -- the file does not exist. Use `/usr/bin/HELLO`,
`/usr/bin/ECHO`, `/usr/bin/CAT` for real-file control tests.

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

**The 2026-11-04 commits (27, on top of `9b62951`):**

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
  `pcb->user_rsp = rsp_init`.**
- `153dda9` -- **`syscalls`: `sys_execve` -- 16-byte-align
  `rsp_init` per SysV AMD64 ABI.**
- `8dc6f99` -- docs: open-issues -- third-session `time true` status
  and next probe.

**Stash.** `stash@{0}`: "vfork 3b (wake arm + sys_vfork) +
diagnostics; time true intermittent, not a complete fix." This is a
preserved diagnostic trail. **Do not drop, do not pop.**

---

## One-line summary

**`v0.6.19` is released; `dev` is at `8dc6f99` (parents `153dda9`,
`939e3ef`, `86c8864`, `ae97ae9`, `4312aab`), ahead 29, clean, stash
present, nothing pushed.** The 2026-11-04 third session landed two
real `sys_execve` fixes -- **`939e3ef`** sets `pcb->user_rsp =
rsp_init` (closes the busybox-side `0x40FBC0`/`+0x180` fault), and
**`153dda9`** 16-byte-aligns `rsp_init` per SysV AMD64 ABI (moves the
fault past `__libc_start_main` and into the applet-dispatch region).
Both are gate-green (28/28 + 57/57). **`time true` still faults, in
two signatures, both `RIP 0` from a `ret` popping a zeroed slot:
`num 13` / `rcx 0x44CC0D` / `+0x390`, and `num 218` /
`rcx 0x44AC73` / `+0x180`. The region between `fault_rsp` and
`rsp_init` is zero in both.** **`time` is not usable today; `true`
alone works; `time` with no args prints usage.** **NEXT: the
`hbreak *0x44cc0d` / `hbreak *0x44ac73` probe with `$rsp` and
`$fs_base`, plus a `serial_print` in `sys_execve` printing
`rsp_init` / `self->user_rsp` / `self->kernel_stack_top[-9]` -- the
`$rsp` vs `rsp_init` comparison decides whether the fix is a
`third_party/` workaround or the delivery path.** **Read
`docs/open-issues.md`'s section "The `time true` fault, after the
third-session fixes" before doing anything with `time`.** Every
earlier hypothesis is in the Refuted list. Read `docs/contract.md`
and this file's NEXT before any code. **Do not move to `v0.6.20`
until the user says so.**
