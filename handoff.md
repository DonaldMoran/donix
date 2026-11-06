# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-06 (sixth session, part 2), at `b514b78`.
**`v0.6.19` is the last pushed release.** `dev` is at `b514b78`; its
recent history is `ac05bbf`, `bb8c8b2`, `6a6fb4d`, `4f48728`,
`26c3bde`, `c3deb8e`, `62b5433`, `711ac16`, `695c53c`, `2bba981`,
`c8ef340`, `63cb81b`. `origin/dev` = `ba56b3d`. Ahead **52**,
**clean**, **stash present** (`stash@{0}`: vfork 3b diagnostics;
preserved, do not drop). No push, no tag this session; banner still
`v0.6.19`. `main` = `7e4748b` (the `--no-ff` merge of `v0.6.19`).
Scratch tags: `20261104-gettimeofday-time` on `5a3b65e`,
`20261106-sys-execve-cr3` on `6406106`, and
`20261106-wait4-rusage` on `b514b78` -- all preserved, all local,
nothing is pushed.

**`time true` is FIXED** by `6406106`. The fault was a stale `%cr3`
register at the `sysret` after a vfork+exec: `sys_execve` switched
`self->cr3` to the new address space but never loaded it into `%cr3`,
and the sysret stub does not reload `%cr3`, so the vfork child
sysret'd into the parent's address space and its first instruction
fetch faulted at `CR2 = 0` / `RIP = 0` / `error = 0x15`. The fix
loads `%cr3` from `self->cr3` at the end of `sys_execve`'s section 8,
guarded by a compare so a regular syscall (the scheduler already
switched `%cr3`) is a no-op.

**`time`'s accounting is FIXED** by `b514b78`. The parent's struct
rusage was never written -- Linux `wait4`'s fourth argument was
absent from the signature and the dispatch, so busybox `time` read
uninitialized stack (`~152710235h`, deterministic garbage, not a
unit bug). `sys_wait4` now fills it from the reaped child's
`total_ticks`, and `sys_times` (43) / `sys_getrusage` (98) are
implemented from the same counters. **`total_ticks` is a single
runnable-tick counter** (`/proc/<pid>/stat` reports it as both
utime and stime), so `time sleep 1` charges ~1.1s to `user` -- the
honest value of this counter, not a new bug.

**`~/code/testme` is the `v0.6.19` reference tree (`ba56b3d`,
tagged).** It is the known-good *boot* comparison. **Do not modify
it.** Its busybox **omits `date`/`time`/`uptime`** from its applet
set even though `configs/busybox.config` enables them, so
`cmp`-identical to `testme` confirms ELF *shape*, not *applet set*.
The applet-set oracle is `busybox --list`; the build of record is
`configs/busybox.config` plus `toolchain/musl-gcc.sh`.

---

## START HERE

**The `time true` fault is fixed and verified.** The live signature
across the five-boot count (`CR2 = 0` / `RIP = 0` / `error = 0x15`,
`fs_base` correct) was an instruction fetch into the parent's
address space. The mechanism, the fix, and the refuted routes are
summarized here; the full record is in `git log` around `6406106`.

**The fix:**
`04_kernel_64bit/user_syscall.c`, end of `sys_execve`'s section 8
(after `self->state = PROC_STATE_RUNNING;`): load `%cr3` from
`self->cr3`, guarded by a compare. For a vfork child, `self->cr3` was
switched to a fresh address space at section 5, but the `%cr3`
register still held the parent's `cr3` (vfork shares the address
space until exec); the `sysret` stub does not reload `%cr3`, so the
child `sysret`'d into the parent's space, where the new image's entry
is unmapped and the first fetch faults. Kernel mappings are shared
(high half), so the switch is safe mid-function; earlier user-VA
writes already target `self->cr3` explicitly
(`safe_copy_to_user_cr3`).

**Verified before commit:** `time true` x5, `time ls`, `time echo
hi`; `canary --full` 28/28; `test.sh` 57/57; `EXEC_CHURN` 24/24;
`pipe7e` 250000 iterations all-pass; `sleep 100` + `^C` returns the
prompt; `exit` from `ash` -> `donix>` -> `busybox sh` re-entry
returns the ash prompt; `^Z`/`jobs`/`fg` cycles.

**Do NOT re-walk the refuted `time true` routes:** `static_init_tls`
/ `base = 0` (the build of record has no `PT_TLS`); `fs_base` (was
correct at every probe); `-Wl,--dynamic-linker` (no effect on a
static non-PIE link); `-fuse-ld=lld` (moved the base, added
`PT_INTERP`, lost the applets); a from-scratch `SECTIONS` (discarded
the real sections); `939e3ef` (`pcb->user_rsp = rsp_init`, broke
`^C`); `153dda9` (16-byte-align `rsp_init`, broke `busybox sh`
re-entry). **Any `sys_execve` change must still pass the gate gap
below** -- both `939e3ef` and `153dda9` passed `canary`/`test.sh` and
broke the shell.

**`busybox_unstripped` is stale and is not the build of record.**
`third_party/busybox/busybox_unstripped` is a *different link*.
Every `readelf`/`nm`/`objdump` diagnostic must point at
`userland/musl/build/busybox.elf` (== `third_party/busybox/busybox`).

**`time`'s accounting is FIXED** by `b514b78` (see START HERE).
`user`/`sys` are now in seconds; `real` was always correct.

---

## WHAT WORKS (verified in the 2026-11-06 boot)

- `canary --full` **28/28**
- `test.sh` **57/57**
- **`pipe7e` 250000 iterations all-pass** (`PIPE7E-ALL-PASS`)
- **`time true`, `time ls`, `time echo hi`** complete; `real` is
  correct (`user`/`sys` are the accounting bug above)
- **`^C` on a foreground `sleep 100`** returns the prompt
- **`^Z` -> `[1]+ Stopped`; `jobs` lists it; `fg` resumes**
- **`exit` from `ash` -> `donix>`; `busybox sh` at `donix>` -> the
  ash prompt** (re-entry works; it prints a cosmetic
  `sys_execve: f_open(usr/bin/BUSYBOX) -> 4` and recovers)
- **`date`** prints the epoch date; **`time`** (no args) prints usage
- **Interactive applets do NOT need `--install`**:
  `CONFIG_FEATURE_SH_STANDALONE=y` makes `ash` dispatch compiled-in
  applets in-process.
- **`--install -s /bin` is needed only for path-based exec**
  (`#!/bin/date`, `execve("/bin/date")`). Without it, `/bin/date` is
  `f_open -> 4` (`FR_NO_FILE`) and the shell prints
  `sh: /bin/date: not found`.
- **`mkdir -p` works**; `mkdir /b` prints `File exists`; `umask`
  returns `0022` and round-trips.
- **`tar`** creates/lists/extracts correctly, including directory
  members.

## WHAT IS BROKEN

Nothing currently open. The `time true` fault (`6406106`) and the
`time` accounting bug (`b514b78`) are both fixed and verified.

---

## NEXT -- the highest-value work, in order

The `time true` fault and the `time` accounting bug are both fixed.
The remaining items are independent and small.

1. **`sys_mkdir` `-ENOTDIR` vs `-EEXIST`** for the file case.
   POSIX wants `-ENOTDIR` when the target exists as a file. Small,
   `sys_mkdir`-local; no current caller depends on it.
2. **`docs/open-issues.md` item 10 -- SSE save/restore.** The test
   is in the item; two separable commits (kernel save/restore, then
   userland flag removal).
3. **`docs/open-issues.md` item 9 -- the privilege model.**
   **Before starting, ask what it unlocks.** If "nothing," skip.
4. **The cosmetic `f_open(usr/bin/BUSYBOX) -> 4`** on `busybox sh`
   re-entry. Recovered; worth one line when the exec path is next
   touched.

---

## THE GATE GAP

**`canary --full` (28/28) and `test.sh` (57/57) do not cover `^C` on
a foreground child or `busybox sh` re-entry.** Both `939e3ef` and
`153dda9` passed the green gate and broke one of those two shell
behaviors. **Any change to `sys_execve`'s user stack, to the
`%fs_base` restore path, or to musl's startup path, must be tested
with:**

- **`sleep 100` then `^C`** -> the prompt returns
- **`exit` from `ash` -> `donix>` -> `busybox sh`** -> the ash
  prompt returns

before it is committed. **A green `canary`/`test.sh` is not
sufficient.** Also useful: `pipe7e` (250000 iterations) and
`EXEC_CHURN` (24 rounds).

---

## WHAT WAS REFUTED -- do NOT re-walk.

- **`-Wl,--dynamic-linker=/lib/ld-musl-x86_64.so.1` as a way to
  force `PT_PHDR`.** No effect on a static non-PIE link.
- **`-fuse-ld=lld` as a fix.** Emits `PHDR` but moves the base
  `0x400000`->`0x200000`, adds `PT_INTERP`, loses
  `date`/`time`/`uptime`.
- **A from-scratch `SECTIONS` block in `busybox_ldscript`.** The
  mechanism works but the hand-built `SECTIONS` discarded the real
  sections (`.text` `0x2ca` instead of `0x541e5`, no applets).
- **Patching musl's `static_init_tls` `base` computation.** The
  build of record has no `PT_TLS`, so `base` is never read.
- **The `static_init_tls` at `0x40FBC0` as a `call *%rax`.**
  `0x40FA40` is `__init_tls`; `0x40FBC0` is the `ret` after its
  `set_tid_address` syscall.
- **`busybox_unstripped` as the build-of-record oracle.** It is
  stale and is a different link from the shipped 428032-byte
  `busybox.elf` (== `busybox`). Use `busybox.elf`.
- **`testme` as an applet-set reference.** Its busybox omits
  `date`/`time`/`uptime`. Use `busybox --list`.
- **`939e3ef`** (`pcb->user_rsp = rsp_init`). Broke `^C` on a
  foreground child. Reverted by `0f97048`.
- **`153dda9`** (16-byte-align `rsp_init`). Broke `busybox sh`
  re-entry. Reverted by `e128c92`.
- **`time /bin/true` as a control.** `/bin/true` does not exist.
  Valid real-file controls: `/usr/bin/HELLO`, `/usr/bin/ECHO`,
  `/usr/bin/CAT`.
- **`$fs_base` as a per-process marker.** Same VA across busybox
  processes. **Only `pid` or `CR3` distinguishes.**
- **`argv_scratch` / `envp_scratch` as the cause.** `argv_scratch`
  is 4 KB; `envp_scratch` is kmalloc'd.
- **`sh: date: not found` as a missing symlink.** It was a *missing
  applet in the binary* (the `testme` busybox omitted
  `date`/`time`/`uptime`). Check `busybox --list` before
  diagnosing.

*Earlier, unchanged.*  Do not re-walk: the `cli` window; signal
delivery as the zeroer; `memset` as the zeroer; the sysret slot
arithmetic; the three `exec_path` forms; `.bss`-not-zeroed; the
auxv; `set_tid_address`'s return; mmap; V3/V3b register inheritance;
the `efc8e08` rsp split and `frame_kind`; the four uncommitted
scheduler guards; the `EXIT-REFUSE frame-CS` guard; the canary
`.text` `#PF` probe stack; the physical-address gates on `.text` PTE
zeroing; the `f91d31a` claim; `exec_churn`'s KVM slowness;
`ata_poll_drq` spinning; the `perf kvm stat record` `double free` is
a donix issue; the 2026-11-04 PTAUDIT "22 anomalies"; the
empty-low-half `vmm_create_address_space`; V3/V3b register zeroing;
the missing auxv; `kmain.c`'s self-tests map over the ELF text page.

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
and read before running.** Durable captures go under
`~/code/donix-backup-*` or another non-`/tmp` path.

**Never put a destructive git command inside a block that also runs
diagnostics.** A `git checkout -- <file>` or `git restore` in a block
whose other lines are `git status`/`git log`/`grep` is a trap. If a
revert is wanted, name it on its own line, say what it discards, and
run it as its own paste.

**Python edits with `count != 1` abort checks are the preferred edit
style.** **Idempotent edit scripts.** **Do the count check IN PYTHON
(`str.count`), not with `grep`.** `grep -Fc` does not do multi-line
matches; `grep -Fxc` requires a whole-line match; `str.count` is the
only honest multi-line count. **A post-write check must assert the
old anchor is ABSENT (`after.count(old) == 0`), and `new` must not
re-emit `old` as a prefix.**

**NO `set -e` in a runner pasted at the prompt.** It closes the
terminal on the non-zero exit and hides the abort message, which is
the signal. Use `python3 /tmp/patch.py ; git diff --stat`
(semicolon) and read the `ABORT` line. `set -e` **inside** a written
`/tmp/*.sh` script is fine and is the preferred style for the script
itself; it is the *runner* pasted at the prompt that must not have
it.

**A one-run diagnostic on the FAILURE path beats three guesses.**

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION -- AND ON A
STATEMENT BOUNDARY, NOT A FRAGMENT INSIDE A STRING LITERAL.** Grep
the file, confirm the count is 1 **and** the match is in the
intended function. Anchor from the file's current bytes. If an
anchor aborts, **ask for the current bytes** -- do not guess a second
anchor. **When the anchor is a full paragraph, use the full
paragraph** -- first byte to last byte.

**The build is TWO steps.** `make -C 04_kernel_64bit
FAT_CONFIG=single` builds the kernel; `make -C 05_boot_kernel64
hdd-single.img` builds the image. Running only the second copies a
STALE `kernel.bin`. Always run both, and verify the mtime chain
`source` < `.o` < `kernel.elf` < `kernel.bin` < `hdd-single.img`.
**`make: Nothing to be done` after editing a header means the object
was not rebuilt -- run `make -C 04_kernel_64bit clean` first.**
**After a session of probe edits and reverts, run a full `make
clean` before the next behavioral test.**

**The patch-script write changes the mode of the file it writes in
some environments.** Verify `stat -c '%a'` before and after every
patch script; if the mode is not `644` on a tracked file, `chmod
644` as its own paste before staging.

**`nm` output moves between builds.** Re-run `nm` before using any
address. **And the symbol at an address may be the *next* symbol,
not the function the address is in** -- the first symbol `<=` the
address is the enclosing function; the first `>=` is the *next*
function. Verify both.

**gdb printf with no per-process marker is not evidence about a
process.** `%rsp` is the same VA in every process; `$fs_base` is the
same VA in every busybox process. **Print `pid` or `CR3` at every
hit, or the lines are cross-process and the inference is invalid.**
This is the single most expensive mistake this project has made.

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
`^Z`/`fg` cycles.** **`--install -s /bin` is needed only if a test
execs an applet by path**; interactive applet use does not require
it.

**Automated gate (optional).** `tools/gate.exp` boots
`./run headless` and runs the three automated gates (`canary
--full`, `/root/scripts/test.sh`, `/usr/bin/EXEC_CHURN`), printing
a three-line verdict. From the tree root:

    expect -f tools/gate.exp

`exit=0` on all-green. **It does not cover the two shell tests
above** -- a green `tools/gate.exp` is not the gate. `./run
headless` uses `-no-shutdown`, so the script kills the `./run`
child rather than waiting for a clean guest shutdown.
`capture.txt` is written by `./run` and overwritten each run; the
script does not copy it. `tools/README.md` has the details.

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
ready and verified.** `origin/dev` is not force-pushed; every
unpushed `dev` commit is intentional work. Run `git log --oneline
origin/dev..dev | wc -l` for the current count.

**Current release state.** **`v0.6.19` is the last pushed release.**
`dev` is at `b514b78`; recent history `43c4c6a`, `b1477fb`,
`6406106`, `ac05bbf`, `bb8c8b2`, `6a6fb4d`, `4f48728`, `26c3bde`,
`c3deb8e`, `62b5433`, `711ac16`, `695c53c`, `2bba981`, `c8ef340`,
`63cb81b`. Ahead **52**, **clean**, **stash present**.
`origin/dev` = `ba56b3d`. Scratch tags
`20261104-gettimeofday-time` on `5a3b65e`,
`20261106-sys-execve-cr3` on `6406106`, and
`20261106-wait4-rusage` on `b514b78` -- all preserved. Work is now
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
-- the honest base). **`time` runs** (`time true`, `time ls`, `time
echo hi` all complete); its `real` is correct and its `user`/`sys` are
the open accounting bug.

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
`/bin/busybox` and `/bin/sh` staged.

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

**Before `--install`, `/bin` contains only `busybox` and `sh`.**
There is no `/bin/true`, `/bin/echo`, `/bin/cat`; after
`--install -s /bin` the applet names are symlinks (see above).
Use `/usr/bin/HELLO`, `/usr/bin/ECHO`, `/usr/bin/CAT` for real-file
control tests.

**`third_party/`** is **gitignored** -- do not commit edits to it.
`third_party/busybox/` has `busybox` (stripped, 428032 bytes, sha
`2d62a991...`) and `busybox_unstripped` (516232 bytes, sha
`6bb18f92...`) -- **`busybox_unstripped` is a stale, different link;
the build of record is `userland/musl/build/busybox.elf` (== the
stripped `busybox`).** The `.map` file is **not** present, so `nm -n`
+ `objdump -d` are the symbol tools, and `readelf -lW` gives the
program headers -- **run them on `busybox.elf`, not on
`busybox_unstripped`.** `third_party/musl-install/` is gitignored and
is needed for the build. If it's missing after a `git clean -xdf`,
copy it back from a working tree, or rebuild it with
`toolchain/install_musl.sh`. **`configs/busybox.config`** is the
busybox build config; `CONFIG_TIME=y`, `CONFIG_TRUE=y`, `CONFIG_LS=y`,
`CONFIG_FEATURE_PREFER_APPLETS=y`, `CONFIG_FEATURE_SH_STANDALONE=y`,
`CONFIG_SH_IS_ASH=y`. A bare
`make -C third_party/busybox` uses host gcc and produces a **glibc**
binary; the musl build is
`make -C third_party/busybox CC="$PWD/toolchain/musl-gcc.sh"`, and it
runs through `userland/musl/Makefile`'s `musl-userland` target (which
`./run` reaches via `hdd-single.img`). `userland/musl/build/.busybox.stamp`
gates that rebuild and depends only on `.config` -- any change to the
busybox build flags or binary requires
`rm -f userland/musl/build/.busybox.stamp userland/musl/build/busybox.elf`
before the next build.

**`toolchain/install_musl.sh` reuses `third_party/musl-src` in place
if it is already at `v1.2.5`; it does not re-materialize it.** A
patch applied to `musl-src` therefore persists across runs, and
reverting the tracked script does **not** undo it -- the revert is
`git -C third_party/musl-src checkout -- <file>` followed by a
rebuild.

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

**`v0.6.19` is released; `dev` is at `b514b78`, ahead 52, clean,
stash present, three scratch tags, nothing pushed.** **`time true`
is fixed** by `6406106` (stale `%cr3` at the `sysret` after
vfork+exec; `sys_execve` now loads it from `self->cr3`). **`time`'s
`user`/`sys` accounting is fixed** by `b514b78` (`sys_wait4` now
fills the parent's struct rusage from the reaped child's
`total_ticks`; `sys_times`/`sys_getrusage` implemented). Verified:
`time true` / `time ls` / `time sleep 1` all in seconds,
`canary --full` 28/28, `test.sh` 57/57, `sleep 100` + `^C`,
`exit` -> `donix>` -> `busybox sh` re-entry. **NEXT: `sys_mkdir
-ENOTDIR`, SSE save/restore, or the privilege model.** Do not
re-walk the refuted `time true` routes. **Do not move to
`v0.6.20` until the user says so.**
