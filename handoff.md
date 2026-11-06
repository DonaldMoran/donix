# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-06 (sixth session, part 2), at `23e37bb`.
**`v0.6.19` is the last pushed release.** `dev` is at `23e37bb`; its
recent history is `2d267da`, `5ccf862`, `b19a0ab`, `6651be4`,
`3cade3f`, `e31395f`, `4ca001d`, `c5066e3`, `269ed07`, `bacbea1`,
`65dac04`, `b514b78`. `origin/dev` = `ba56b3d`. Ahead **64**,
**clean**, **no stash**. No push, no tag this session; banner still
`v0.6.19`. `main` = `7e4748b` (the `--no-ff` merge of `v0.6.19`).
Scratch tags, all annotated, all preserved, nothing pushed:
`20261104-gettimeofday-time` on `5a3b65e`,
`20261106-sys-execve-cr3` on `6406106`,
`20261106-wait4-rusage` on `b514b78`,
`20261106-sys-flock` on `4ca001d`,
`20261106-sys-fdatasync` on `e31395f`,
`20261106-sys-sched-getaffinity` on `3cade3f`,
`20261106-fat-rename-replace` on `6651be4`,
`20261106-sys-link` on `b19a0ab`,
`20261106-f-utime` on `5ccf862`,
`20261106-coreutils-sweep` on `2d267da`,
`20261106-busybox-coreutils-config` on `23e37bb`.
**Eleven scratch tags.**

**The FAT-coreutils set is provable.** `tools/coreutils_sweep.exp`
(self-checking, expect, per docs/expect-harness.md) runs ~95 applets
PASS, `touch -d` PASS (five real timestamps via `5ccf862`), `ln`
OBSERVE (honest `-EPERM` on FAT), six missing syscalls closed this
session. The two shell behaviors `tools/gate.exp` leaves manual are
in the sweep, but `^C`-over-serial `FAIL`s (see NEXT).

---

## START HERE

**The next session is `^C`-over-serial.** It is the gate-gap: PS/2
`^C` reaches `irq1_handler` and raises SIGINT on the foreground
pgrp; UART `0x03` from `-serial stdio` is a data byte to the shell's
line editor and does not. `tools/coreutils_sweep.exp`'s `sleep 100 +
^C` row FAILs for exactly this reason. It is the last gate-gap item.

**The diagnostic plan (own session):**
1. `stty -a` on the serial console -- is `isig` set?
2. Send `^C` over `-serial stdio` while `sleep 100` runs -- does
   `sys_kill(-fg, SIGINT)` fire from the UART path, or does the
   shell's line editor see `0x03` as a byte?
3. Whichever, the fix is in the UART RX path (route `0x03`
   through the PS/2 signal logic) or in the shell's line editor
   (honor `ISIG` on the UART path).

After that fix, `tools/coreutils_sweep.exp`'s two shell `FAIL` lines
become `PASS`, and a green `tools/coreutils_sweep.exp` plus a green
`tools/gate.exp` is the full gate -- no manual shell tests.

**The second item, for the session after that:** the `FR_NO_FILE`
create-then-set in `f_utime` (see `open-issues.md`, the new item at
the top of `Also open -- small defects`). The `sync_window` family
as `a0664a8` and `d69c2ab`; a future fix would `f_sync` in
`f_open` before returning, which is a behavior change to `f_open`
and belongs in its own commit.

---

## WHAT WORKS (verified in the 2026-11-06 boots)

- `canary --full` **28/28** (green on the split tree `5ccf862`,
  byte-identical to `5129ac5`).
- `test.sh` **57/57**.
- **`EXEC_CHURN` 24/24 ALL-PASS.**
- **`tools/coreutils_sweep.exp` ~95 PASS** (FAT-coreutils set).
- **`time true` / `time ls` / `time echo hi`** complete.
- **`touch -d '2020-01-01 12:34:56' f; stat -c %y f`** reads
  `2020-01-01 12:34:56.000000000 +0000` (exact round-trip;
  `5ccf862`).
- **`tar -xf`** preserves mtime of extracted file (same commit).
- **`flock f true`** rc=0, no noise (`4ca001d`).
- **`shred -n 1 f`** rc=0, no noise (`e31395f`).
- **`nproc`** prints `1`, rc=0, no noise (`3cade3f`).
- **`patch a < p`**, **`dos2unix`**, **`unix2dos`**, **`mv x y`**
  all succeed where the destination exists (`6651be4`; Linux
  rename(2) replace semantics on FAT).
- **`ln f g`** prints "Operation not permitted" rc=1, source
  untouched, no noise (`b19a0ab`; Linux on FAT returns `-EPERM` too).
- **`^C` on a foreground `sleep 100`** returns the prompt from the
  PS/2 keyboard (SDL window). **From `-serial stdio` it does not**
  -- the gate-gap.
- **`^Z` -> `[1]+ Stopped`; `jobs` lists it; `fg` resumes.**
- **`exit` from `ash` -> `donix>`; `busybox sh` at `donix>` -> the
  ash prompt** (re-entry works; the cosmetic
  `sys_execve: f_open(usr/bin/BUSYBOX) -> 4` still appears).
- **Interactive applets do NOT need `--install`**
  (`FEATURE_SH_STANDALONE=y`).

## WHAT IS BROKEN

- **`^C` over `-serial stdio` does not raise SIGINT.** The
  gate-gap. See NEXT.
- **`logname`** prints `logname: getlogin` -- `SYS_GETLOGIN` not in
  dispatcher. Not in `test.sh`.
- **`hostid`** prints `00000000` -- `sys_gethostid` not in
  dispatcher. Not in `test.sh`.
- **`sync` / `fsync`** -- `CONFIG_SYNC`/`CONFIG_FSYNC` off, and
  `SYS_SYNC(162)` / `SYS_FSYNC(74)` not in dispatcher. Config-bound,
  separate round.
- **`FR_NO_FILE` on create-then-set in `f_utime`** (see
  `open-issues.md`).
- **`env | wc -l` is 2**; `printenv PATH` empty -- the shell does
  not export `PATH`. Environment gap, not syscall.

---

## NEXT -- the highest-value work, in order

1. **`^C`-over-serial.** The gate-gap. Kernel diagnostic session.
   Fix is in the UART RX path or the shell's line editor. See
   START HERE.
2. **`FR_NO_FILE` on create-then-set in `f_utime`.** The
   `sync_window` family. One-line `open-issues.md` record, but a
   real fix is `f_sync` in `f_open`, which is a behavior change and
   needs its own commit. Low user-visible impact.
3. **`sync(162)` / `fsync(74)` + `CONFIG_SYNC`/`CONFIG_FSYNC`** if
   a future applet needs them. Config-bound.
4. **`getlogin` / `hostid`** stubs if a future test needs them. Not
   in `test.sh`.

---

## THE GATE GAP

**`canary --full` (28/28) and `test.sh` (57/57) do not cover `^C` on
a foreground child or `busybox sh` re-entry.** Both `939e3ef` and
`153dda9` passed the green gate and broke one of those two shell
behaviors. **Any change to `sys_execve`'s user stack, to the
`%fs_base` restore path, or to musl's startup path, must be tested
with:**

- **`sleep 100` then `^C`** -> the prompt returns (PS/2 keyboard)
- **`exit` from `ash` -> `donix>` -> `busybox sh`** -> the ash
  prompt returns

before it is committed. **A green `canary`/`test.sh` is not
sufficient.** Also useful: `pipe7e` (250000 iterations) and
`EXEC_CHURN` (24 rounds).

**`tools/coreutils_sweep.exp` now covers both**, plus the FAT-
coreutils sweep. A green run is the gate. **But `^C`-over-serial
FAILs today**, so the sweep's shell `FAIL` line is the current
state, not a bug in the sweep.

---

## WHAT WAS REFUTED -- do NOT re-walk.

- **`-Wl,--dynamic-linker=/lib/ld-musl-x86_64.so.1`** -- no effect
  on a static non-PIE link.
- **`-fuse-ld=lld`** -- moves the base, adds `PT_INTERP`, loses
  `date`/`time`/`uptime`.
- **From-scratch `SECTIONS` in `busybox_ldscript`** -- discarded the
  real sections.
- **Patching musl's `static_init_tls` `base`** -- build has no
  `PT_TLS`.
- **`busybox_unstripped` as oracle** -- stale, different link. Use
  `busybox.elf`.
- **`testme` as applet-set reference** -- omits `date`/`time`/
  `uptime`. Use `busybox --list`.
- **`939e3ef`, `153dda9`** -- broke `^C` and `busybox sh` re-entry.
- **`time /bin/true` as control** -- `/bin/true` does not exist.
- **`$fs_base` as per-process marker** -- same VA every busybox.
- **`argv_scratch` / `envp_scratch` as the cause** -- sizes are
  fine; `envp_scratch` is kmalloc'd.
- **`sh: date: not found` as missing symlink** -- missing applet in
  the binary. Check `busybox --list`.

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

**This session's refutations:** the marker-only `run_cmd` in the
sweep (fixed by numeric tokens and marker+prompt drain);
`(?s)` in expect's ARE (unsupported, use `(\r?\n|^)...$`
anchoring); `git checkout -- <file>` after `git reset --soft` (does
not discard the diff -- restores from the index; use `git reset
--hard HEAD`); `grep -c 'UTIM'` as probe-existence check (counts
legitimate `UTIME_NOW`/`UTIME_OMIT`; use `grep -c 'serial_print("UTIM'`).

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
diagnostics.** A `git checkout -- <file>`, `git restore`, or `git
reset --hard` in a block whose other lines are `git status`/`git
log`/`grep` is a trap. If a revert is wanted, name it on its own
line, say what it discards, and run it as its own paste.

**Python edits with `count != 1` abort checks are the preferred edit
style.** **Idempotent edit scripts.** **Do the count check IN PYTHON
(`str.count` / `bytes.count`), not with `grep`.** `grep -Fc` does not
do multi-line matches; `grep -Fxc` requires a whole-line match;
`str.count` is the only honest multi-line count. **A post-write check
must assert the old anchor is ABSENT (`after.count(old) == 0`), and
`new` must not re-emit `old` as a prefix.**

**Binary-safe edits for files with CRLF line endings.** `open(p,
"rb").read()` / `open(p, "wb").write(data)` -- Python's universal-
newline mode silently rewrites CRLF to LF on read/write, which turns
a one-line diff into a whole-file diff. `configs/busybox.config` and
`04_kernel_64bit/fatfs/ffconf.h` are CRLF. **Check with `file` first
if unsure.**

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
`-enable-kvm`). Default cwd `/root`.

**Writing a new `expect` harness:** `docs/expect-harness.md` has
the two spawn patterns (`./run headless` vs. QEMU directly for a
staged binary), the two verdict-pattern rules, the `-no-shutdown`
shutdown rule, and the capture rule. Read it before writing a
`.exp`; `tools/gate.exp` and `tools/coreutils_sweep.exp` are worked
examples.

**In-guest gate.** `cd /`, then `canary --full` (28/28) and
`/root/scripts/test.sh` (57/57). **Plus the two shell tests:**
`sleep 100` then `^C` returns the prompt; `exit` from `ash` ->
`donix>` -> `busybox sh` -> the ash prompt. **Use multiple
`^Z`/`fg` cycles.** **`--install -s /bin` is needed only if a test
execs an applet by path**; interactive applet use does not require
it.

**Automated gate.** `tools/gate.exp` boots `./run headless` and
runs the three automated gates. `tools/coreutils_sweep.exp` boots
the same and runs the FAT-coreutils sweep **plus the two shell
behaviors** -- this is the fuller gate. A green `gate.exp` is **not**
the gate; a green `coreutils_sweep.exp` is, once `^C`-over-serial is
fixed. `capture.txt` is written by `./run` and overwritten each
run; the sweeps do not copy it (they write their own `.expect.log`).

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
`dev` is at `23e37bb`; recent history `2d267da`, `5ccf862`,
`b19a0ab`, `6651be4`, `3cade3f`, `e31395f`, `4ca001d`, `c5066e3`.
Ahead **64**, **clean**, **no stash**, **eleven scratch tags**.
`origin/dev` = `ba56b3d`. Work is now pre-`v0.6.20`. **There is no
release gate recorded for `v0.6.20`; if a gate is wanted it is set
in this file before that release session, not during it. The user
has said not to move to `v0.6.20` until they say so.**

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 ABI, natively. Newlib is gone. Userland
is a tracked source tree at `userland/musl/`. Phase B (busybox) is
well underway: the shell is interactive, pipelines and redirection
work, job control works (`^C`, `^Z`, `jobs`, `fg`), and the applet
set includes `bc`, `dc`, `diff`, `du`, `awk`, `date`, `uptime`,
`time`, `ed`, `patch`, `xargs`, `flock`, `dd`, `shred`, `timeout`,
`nproc`, `less`, `more`, `hexedit` in addition to the file/text
applets. **`tar` creates, lists and extracts correct archive
members, including directory members, preserving mtime.** **`mkdir
-p` works.** **`flock`, `fdatasync`, `sched_getaffinity`, `link`
are implemented.** **`rename(2)` replaces on FAT (`6651be4`).**
**`utimensat` writes and `stat` reads mtime (`5ccf862`; FAT's
2-second resolution is the honest limit).** `date`/`uptime` report
seconds since boot as a 1970-epoch date (no RTC -- the honest
base). **`time`'s accounting is correct in seconds**
(`b514b78`); `time sleep 1` charges ~1.1s to `user` because
`total_ticks` is a single runnable-tick counter reported as both
utime and stime.

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`. Open issues:
`docs/open-issues.md`.

---

## State on disk

Kernel sources: `04_kernel_64bit/`. `capture.txt` is gitignored,
overwritten each `./run` -- **copy it aside after any failure or pass
you want to keep.**

**`~/code/testme` is the `v0.6.19` reference tree (`ba56b3d`,
tagged).** Known-good *boot* comparison. **Do not modify it.** Its
busybox omits `date`/`time`/`uptime`; use `busybox --list` for the
applet-set oracle.

**Interactive applet use does NOT need `--install`.** BusyBox is
built with `CONFIG_FEATURE_SH_STANDALONE=y`.

**`--install` is needed only for path-based exec** (`#!/bin/date`,
`execve("/bin/date")`). `/bin/sh` is staged by the build as a copy
of busybox.

**`third_party/`** is **gitignored** -- do not commit edits to it.
`third_party/busybox/` has `busybox` (stripped, 448512 bytes, sha
changes across config flips -- verify with `readelf -lW` on
`userland/musl/build/busybox.elf` if needed) and
`busybox_unstripped` (stale, different link -- **do not use as
oracle**). The `.map` file is **not** present. `third_party/musl-
install/` is gitignored and needed for the build. **`configs/busybox.config`** is the busybox build config (currently
with the FAT-coreutils flips from `23e37bb`); a bare
`make -C third_party/busybox` uses host gcc and produces a glibc
binary, the musl build is `make -C third_party/busybox
CC="$PWD/toolchain/musl-gcc.sh"`.
`userland/musl/build/.busybox.stamp` gates the rebuild and depends
only on `.config`; any change to the busybox build flags or binary
requires `rm -f userland/musl/build/.busybox.stamp
userland/musl/build/busybox.elf` before the next build.

**`third_party/busybox/.config` is a durable copy of the last-built
config** -- it survives `git reset --hard` on `configs/busybox.config`
(the tracked source). **If `configs/busybox.config` loses the flip
on a reset, restore from `third_party/busybox/.config`.** That
happened this session and was recovered.

**`toolchain/install_musl.sh` reuses `third_party/musl-src` in place
if it is already at `v1.2.5`.** A patch applied to `musl-src`
persists across runs; revert is `git -C third_party/musl-src
checkout -- <file>` followed by a rebuild.

**`04_kernel_64bit/fatfs/ff.c`** is tracked and has two
`sync_window` fixes; any further edit there must re-run the full
gate. **`04_kernel_64bit/fatfs/ffconf.h`** is CRLF; binary-safe
edits only.

**`~/code/saved`** -- not a baseline. Run
`git -C ~/code/saved status --short` before any comparison.

**The reset baseline.** `dev` was reset to `873be27`, tag
`reset-baseline-873be27`. `efc8e08` is not on `dev`; it survives as a
commit object. Treat its guards as **refuted**.

**Stash.** No stash. `stash@{0}` was dropped this session (its
content -- the sys_link + f-utime split -- was re-applied as two
commits).

---

## One-line summary

**`v0.6.19` is released; `dev` is at `23e37bb`, ahead 64, clean, no
stash, eleven scratch tags, nothing pushed. The FAT-coreutils set is
provable via `tools/coreutils_sweep.exp` (~95 PASS, `touch -d`
PASS, `ln` OBSERVE). Six missing syscalls closed this session
(`flock`, `fdatasync`, `sched_getaffinity`, `link`, `utimensat`
write + read, `rename` replace); config flipped for the FAT-
coreutils applets; sweep harness tracked. **NEXT: `^C`-over-serial
(the last gate-gap item).** Then the `FR_NO_FILE` create-then-set in
`f_utime`. Do not move to `v0.6.20` until the user says so.**
