# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-07 (tenth session), at `f4dd5ee`.
**`v0.6.19` is the last pushed release** (annotated tag `v0.6.19` =
commit `ba56b3d`). `dev` is at `f4dd5ee`; recent history is
`f4dd5ee`, `04e584b`, `6d14dc9`, `f49c392`, `1a737e2`, `47ae47f`.
`origin/dev` = `ba56b3d`. Ahead **89**, **clean**, **no stash**,
**32 scratch tags**. No push, no release this session; banner still
`v0.6.19`. `main` = `7e4748b`.

**Scratch tags: 32, all annotated, all preserved, none pushed.**
Newest first: `20261107-logname-env` on `f4dd5ee`,
`20261106-handoff-sync-fsync` on `04e584b`,
`20261106-busybox-sync-fsync` on `6d14dc9`,
`20261106-sys-sync-fsync` on `f49c392`,
`20261106-fil-timeset` on `1a737e2`,
`20261106-kbd-drop-dead-ctrlz` on `f89f494`,
`20261106-serial-ctrlsz` on `446e3a0`,
`20261106-run-telnet-lan` on `d6969ea`,
`20261106-serial-ctrlc-ring` on `5575868`,
`20261106-sweep-socket-serial` on `fc10881`,
`20261106-sys-execve-no-elf-noise` on `0b07cd8`,
`20261106-open-issues-tz` on `f6dd91a`,
`20261106-rtc-localtime` on `d9df0bc`,
`20261106-open-issues-touch-d-fresh`, `20261106-ata-flush-cache`,
`20261106-get-fattime`, `20261106-handoff-rtc`,
`20261106-rtc-time-syscalls` on `9d7b5d3`,
`20261106-rtc-read` on `1479723`, `20261106-handoff-rewrite`,
`20261106-open-issues-fr-no-file`,
`20261106-busybox-coreutils-config` on `23e37bb`,
`20261106-coreutils-sweep` on `2d267da`,
`20261106-f-utime` on `5ccf862`, `20261106-sys-link` on `b19a0ab`,
`20261106-fat-rename-replace` on `6651be4`,
`20261106-sys-fdatasync` on `e31395f`,
`20261106-sys-flock` on `4ca001d`,
`20261106-sys-sched-getaffinity` on `3cade3f`,
`20261106-wait4-rusage` on `b514b78`,
`20261106-sys-execve-cr3` on `6406106`,
`20261104-gettimeofday-time` on `5a3b65e`.

`1a737e2` (the `FIL.timeset` fix) is tagged `20261106-fil-timeset`.
It is in the `v0.6.20` summary source.

**Stash:** none.

---

## START HERE

**The next session is the `env` / `PATH` gap.**
`printenv PATH` is currently empty because `ash` does not export `PATH`
(asserted by `canary.c`'s `printenv PATH is empty` check). Evaluate
whether `PATH` should be exported by default (which requires updating
`canary.c`), or if the current unexported behavior is the intended
donix design. See `open-issues.md`.

---

## WHAT WORKS (verified 2026-11-06, at `1a737e2`)

- **`canary --full` 28/28.**
- **`test.sh` 57/57.**
- **`EXEC_CHURN` 24/24 ALL-PASS.**
- **`tools/coreutils_sweep.exp` PASS=100 FAIL=0 XFAIL=0 OBSERVE=5.**
- **`sync` and `fsync` work.** Configs flipped on (`6d14dc9`), backed
  by `sys_sync(162)` (iterates mounted volumes, calls `f_syncfs` and
  `ata_flush_cache`) and `sys_fsync(74)` (`f_sync` + `f_sync_volume`).
  Verified with `test_sync` in-guest and the automated sweep.
- **`logname` prints `root`** (`f4dd5ee`), backed by `LOGNAME=root` in
  `musl_sh.c` default environment. `hostid` prints `00000000` (verified
  upstream Musl `gethostid()` behavior). Both pass the coreutils sweep.
  The full gate, with both shell behaviors. **The `touch` row is now
  a real `auto` row** (`rm -f /tmp/td; touch -d '2020-01-01
  12:34:56' /tmp/td; stat -c %y /tmp/td`, asserting the
  stat-output-only token `12:34:56.000000000`) and it **PASSes**.
  Before `1a737e2` it was a hard-coded `xfail` that ran the command
  and then recorded `XFAIL` regardless.
- **`touch -d '<date>'` on a nonexistent path now round-trips**
  (`1a737e2`). The create-then-set case in a single `touch` is
  fixed. `touch f` (no `-d`), `touch -d` on an existing path, `vi`,
  and `tar -xf` mtime preservation all still work.
- **`futimens(fd, times)` works.** `sys_utimensat`'s `path == NULL`
  branch resolves the slot's `FIL*` and calls the new `f_settime`,
  which writes `DIR_ModTime` on the open file's entry.
- **Serial `^C` AND `^Z` work** over UNIX socket, plain TCP, and
  telnet. The kernel drains the UART into a 64-byte RX ring on the
  100 Hz tick (bounded 16 bytes/tick), consumes `0x03` as SIGINT and
  `0x1A` as SIGTSTP, and raises them through the deferred
  `console_raise_pending` path. PS/2 `^C`/`^Z` (SDL) unchanged.
- **`./run telnet`** = guest console on `127.0.0.1:45456`;
  **`./run telnet-lan`** = same on `0.0.0.0:45456`, gated on
  `DONIX_ALLOW_LAN=1`.
- **`date` prints the host's real date** (RTC read at boot
  `1479723`, plumbed `9d7b5d3`); `touch -d '<date>' f; stat -c %y f`
  round-trips exactly on an existing file and on a fresh path.
- **`flock`, `shred -n 1`, `nproc`, `patch a < p`, `dos2unix`,
  `unix2dos`, `mv x y` (replace), `ln f g` (`-EPERM` on FAT)** -- the
  FAT-coreutils set, `23e37bb` onward.
- **`exit` from `ash` -> `donix>` -> `busybox sh` -> ash prompt**
  (cosmetic `sys_execve: f_open(usr/bin/BUSYBOX) -> 4` still appears).
- **Interactive applets do NOT need `--install`**
  (`FEATURE_SH_STANDALONE=y`).

## WHAT IS BROKEN

- **`printenv PATH` empty** -- the shell does not export `PATH` (asserted
  by canary). Environment gap, not syscall.

---

## THE HARNESS RULE (load-bearing)

**A control byte (`^C` `0x03`, `^Z` `0x1A`) needs a byte path that
actually delivers it to the guest UART.**

- **`./run headless` (Pattern A) cannot**: `./run` pipes QEMU through
  `tee`; an expect-pty byte never reaches the guest, and
  `send "\003"` kills the wrapper. Plain text commands (no control
  byte) *do* reach the guest through this path -- but not reliably,
  and this session burned several boots on `.exp` writes that never
  arrived. **For anything that matters, prefer `./run telnet` or a
  UNIX socket.**
- **`-serial stdio` cannot**: QEMU's stdin is a host tty; `^C` is
  the tty INTR char and SIGINTs QEMU.
- **Use one of:** `./run telnet` (easiest, loopback), `./run
  telnet-lan` (LAN, gated), UNIX socket (`-serial unix:...` +
  `socat -,raw,echo=0`), or plain TCP. `-,raw,echo=0` on socat is
  load-bearing; without it `0x03` kills socat. A telnet client must
  be in **character mode**.
- **Ports:** 23 is not used (privileged). **1234 is the gdb stub**
  (`-s`). donix uses 45456.

**A marker on the same line as the command is not a verdict.**
`send "sleep 100; echo __SLEPT__\r"` echoes the command line, so
`expect __SLEPT__` matches the echo. Use the two-step form.

**The same trap bit the `coreutils_sweep` `touch` row**: the shell
echoes the command, so a `want` token that appears in the command
line passes on the echo. The row now asserts
`12:34:56.000000000`, which appears only in `stat -c %y` output.

---

## WHAT WAS REFUTED -- do NOT re-walk.

**This session (the `touch -d` / `futimens` work):**
- **`f_sync` in `f_open`** -- the previous START HERE. It was already
  there (`ff.c` create path calls `sync_window` after
  `dir_register`), and `touch f` (no `-d`) already worked. The
  symptom was never "the create entry is not flushed".
- **`fp->dir_sect = dj.sect`** (instead of `fs->winsect`). Tried,
  built, booted, **still failed**. `dir_sect` was never wrong.
- **"`dir_sect = 128` collides with `dd … seek=128`"** -- a
  coincidence. `128` is the **volume-relative** sector of `::/`'s
  root directory (`fs->dirbase`), and `diskio.c` adds the
  `FAT_PARTITION_OFFSET` 2048 on every read/write. `etc/passwd` at
  `132` is the same root directory, four sectors on. `128` is
  correct.
- **`follow_path` / `dir_sdi` parent-directory resolution.** Read in
  full: `follow_path` sets `dp->obj.sclust` for each non-last
  segment, `dir_sdi` takes the `clst2sect` branch for a
  sub-directory. Both correct.
- **The path-based `futimens`** (record the open path on the slot,
  call `f_utime(slot->file_path)`). Never applied -- the patch
  script's anchor did not match and it aborted. Then the real cause
  was found, so it is unnecessary. **Do not resurrect it**: it would
  have worked, but `f_settime` + `FIL.timeset` is smaller and does
  not grow `file_slot_t`.
- **`PROBE_FSYNC` / `PROBE_FUTIME_FR`** -- removed in `1a737e2`.
  Do not re-add them.

**THE PROVEN CAUSE (so it is not re-derived):** `f_settime` wrote
`DIR_ModTime` correctly; `f_close` -> `f_sync` then re-stamped it to
`GET_FATTIME()` because `FA_MODIFIED` is set on create (`f_open`:
`if (mode & FA_CREATE_ALWAYS) mode |= FA_MODIFIED;`). The serial
probe printed `cur_mod=0x50241A5C` (the 2020 stamp) and
`new_mod=GET_FATTIME()`. **The fix is `FIL.timeset`**: set by
`f_settime` on success, initialised to 0 in `f_open`, `f_sync`
skips the `GET_FATTIME()` re-stamp when it is set and clears it after
the sync. `f_utime` (path form) needs no flag -- no open `FIL`
remains to overwrite it.

**Earlier, unchanged.** Do not re-walk: `-Wl,--dynamic-linker`,
`-fuse-ld=lld`, from-scratch `SECTIONS`, patching musl
`static_init_tls` `base`, `busybox_unstripped` as oracle, `testme` as
applet-set reference, `939e3ef`/`153dda9`, `time /bin/true` as
control, `$fs_base` as per-process marker, `argv_scratch`/
`envp_scratch`, `sh: date: not found` as a missing symlink; the
`cli` window, signal delivery as the zeroer, `memset` as the zeroer,
the sysret slot arithmetic, the three `exec_path` forms,
`.bss`-not-zeroed, the auxv, `set_tid_address`'s return, mmap,
V3/V3b register inheritance, the `efc8e08` rsp split and
`frame_kind`, the four uncommitted scheduler guards, the
`EXIT-REFUSE frame-CS` guard, the canary `.text` `#PF` probe stack,
the physical-address gates on `.text` PTE zeroing, the `f91d31a`
claim, `exec_churn`'s KVM slowness, `ata_poll_drq` spinning, the
`perf kvm stat record` `double free`, the 2026-11-04 PTAUDIT "22
anomalies", the empty-low-half `vmm_create_address_space`, V3/V3b
register zeroing, `kmain.c`'s self-tests map over the ELF text page.

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

**A multi-line paste can silently skip its `python3`.** This session
lost three boots to pastes where `cat > script` ran but the
`python3 /tmp/script.py` line did not. **Put the apply and the
verification in one paste**, and verify with a number: `grep -c
'PROBE_…'` in the source, and **`strings kernel.bin | grep -c`**
before any boot counts.

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
a one-line diff into a whole-file diff. **This session's line-ending
facts:** `04_kernel_64bit/fatfs/ff.c` and `ffconf.h` and `ff.h` are
**CRLF**; `04_kernel_64bit/user_syscall.c` and
`tools/coreutils_sweep.exp` are **LF**. **Check with `file` (or a
`b"\r\n" in b` test) before every edit; do not assume.**

**NO `set -e` in a runner pasted at the prompt.** It closes the
terminal on the non-zero exit and hides the abort message, which is
the signal. Use `python3 /tmp/patch.py ; git diff --stat`
(semicolon) and read the `ABORT` line. `set -e` **inside** a written
`/tmp/*.sh` script is fine and is the preferred style for the script
itself; it is the *runner* pasted at the prompt that must not have
it.

**A one-run diagnostic on the FAILURE path beats three guesses.**
This session proved the point: five theories (f_sync-in-f_open,
dj.sect, the 128 collision, follow_path, path-based futimens) were
each wrong, and one serial probe (`cur_mod` vs `new_mod`) named the
real cause. **Instrument, one boot, read the number.**

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION -- AND ON A
STATEMENT BOUNDARY, NOT A FRAGMENT INSIDE A STRING LITERAL.** Grep
the file, confirm the count is 1 **and** the match is in the
intended function. Anchor from the file's current bytes. If an
anchor aborts, **ask for the current bytes -- do not guess a second
anchor.** **When the anchor is a full paragraph, use the full
paragraph** -- first byte to last byte. **This session's anchor
failures were whitespace**: the `coreutils_sweep` touch row has
**four spaces** before `""`, not one; the right way to find that out
is `awk '{print "["$0"]"}'` or a Python `repr`, not memory.

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
`./run debug` adds `-S`. **`./run tcg` / `./run tcg-debug`** = TCG
(`-cpu max`, no `-enable-kvm`). **`./run telnet` / `./run
telnet-lan`** = console on `127.0.0.1:45456` / `0.0.0.0:45456`
(LAN gated by `DONIX_ALLOW_LAN=1`). Default cwd `/root`.

**Writing a new `expect` harness:** `docs/expect-harness.md` has the
two spawn patterns, the two verdict-pattern rules, the
`-no-shutdown` shutdown rule, the capture rule, the control-byte
transport rule, and the `./run telnet` note. Read it before writing
a `.exp`; `tools/gate.exp` and `tools/coreutils_sweep.exp` are
worked examples.

**In-guest gate.** `cd /`, then `canary --full` (28/28) and
`/root/scripts/test.sh` (57/57). **Plus the two shell tests:**
`sleep 100` then `^C` returns the prompt; `sleep 100` then `^Z` stops
it and `jobs`/`fg` work; `exit` from `ash` -> `donix>` ->
`busybox sh` -> the ash prompt. **`--install -s /bin` is needed only
if a test execs an applet by path**; interactive applet use does not
require it.

**Automated gate.** `tools/gate.exp` boots `./run headless` and runs
the three automated gates. `tools/coreutils_sweep.exp` boots the
same and runs the FAT-coreutils sweep **plus the two shell
behaviors**; its `^C` row uses the socket serial form. A green
`gate.exp` is **not** the gate; a green `coreutils_sweep.exp` is.
**Stop any manual QEMU before running either** -- both boot their own
and collide on `hdd-single.img` and `:1234`.

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

6. **Merge to `main` with `--no-ff`**, push, return to `dev`:

       git checkout main
       git merge --no-ff dev -m "<version summary>"
       git push origin main
       git checkout dev

Do not use `git push --tags` -- it would push the scratch tags. Push
`vX.Y.Z` by name.

**The GitHub push policy.** **Nothing is pushed until `vX.Y.Z` is
ready and verified.** `origin/dev` is not force-pushed; every
unpushed `dev` commit is intentional work. Run
`git log --oneline origin/dev..dev | wc -l` for the current count.

**Current release state.** **`v0.6.19` is the last pushed release**
(tag `v0.6.19` = `ba56b3d`, which is also `origin/dev`). `dev` is at
`1a737e2`; ahead **83**, **clean**, **no stash**, **27 scratch
tags**. `main` = `7e4748b` (the `--no-ff` merge of `v0.6.19`). Work
is pre-`v0.6.20`. **There is no release gate recorded for
`v0.6.20`; if a gate is wanted it is set in this file before that
release session, not during it. The user has said not to move to
`v0.6.20` until they say so.**

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 ABI, natively. Newlib is gone. Userland
is a tracked source tree at `userland/musl/`. Phase B (busybox) is
well underway: the shell is interactive, pipelines and redirection
work, job control works (`^C`, `^Z`, `jobs`, `fg`) on both the PS/2
and serial consoles, and the applet set includes `bc`, `dc`, `diff`,
`du`, `awk`, `date`, `uptime`, `time`, `ed`, `patch`, `xargs`,
`flock`, `dd`, `shred`, `timeout`, `nproc`, `less`, `more`,
`hexedit` in addition to the file/text applets. **`tar` creates,
lists and extracts correct archive members, including directory
members, preserving mtime.** **`mkdir -p` works.** **`flock`,
`fdatasync`, `sched_getaffinity`, `link` are implemented.**
**`rename(2)` replaces on FAT (`6651be4`).** **`utimensat` writes and
`stat` reads mtime (`5ccf862`), and a timestamp set via
`futimens`/`f_settime` now survives `f_close` (`1a737e2`).** `date`
reports the real date -- the MC146818A RTC is read at boot
(`1479723`) and plumbed through the time syscalls (`9d7b5d3`).
`uptime` still reports seconds since boot.

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
`third_party/busybox/` has `busybox` (stripped) and
`busybox_unstripped` (stale, different link -- **do not use as
oracle**). `third_party/musl-install/` is gitignored and needed for
the build. **`configs/busybox.config`** is the busybox build config.
`third_party/busybox/.config` is a durable copy of the last-built
config -- restore from it if `configs/busybox.config` loses a flip
on a reset.

**`toolchain/install_musl.sh` reuses `third_party/musl-src` in place
if it is already at `v1.2.5`.** A patch applied to `musl-src`
persists across runs.

**`04_kernel_64bit/fatfs/ff.c`** is tracked and has three timestamp
fixes now (`a0664a8` f_open flush, `d69c2ab` f_write, `1a737e2`
`FIL.timeset`); any further edit there must re-run the full gate.
**`ff.c` is CRLF. `ffconf.h` is CRLF. `ff.h` is CRLF.** Binary-safe
edits only.

**`~/code/saved`** -- not a baseline. Run
`git -C ~/code/saved status --short` before any comparison.

**The reset baseline.** `dev` was reset to `873be27`, tag
`reset-baseline-873be27`. `efc8e08` is not on `dev`; it survives as a
commit object. Treat its guards as **refuted**.

---

## One-line summary

**`v0.6.19` is released (tag on `ba56b3d` = `origin/dev`); `dev` is
at `f4dd5ee`, ahead 89, clean, no stash, 32 scratch tags, nothing
pushed. `sync(162)` and `fsync(74)` are IMPLEMENTED and gated
(`f49c392`, `6d14dc9`); `logname` prints `root` and `hostid` prints
`00000000` (`f4dd5ee`, resolved via `musl_sh.c` default environment).
`tools/coreutils_sweep.exp` PASS=100 FAIL=0 XFAIL=0 OBSERVE=5;
`gate.exp` CANARY 28/28, TEST_SH 57/57, EXEC_CHURN ALL-PASS.
**NEXT: the `env` / `PATH` gap.** Do not move to `v0.6.20` until the
user says so.**
