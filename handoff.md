# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-06 (seventh session), at `5575868`.
**`v0.6.19` is the last pushed release.** `dev` is at `5575868`;
recent history is `fc10881`, `0b07cd8`, `f6dd91a`, `d9df0bc`,
`2e30866`, `3514bb1`, `23e37bb`. `origin/dev` = `ba56b3d`. Ahead
**77**, **clean**, **no stash**, **24 scratch tags**. No push, no
release this session; banner still `v0.6.19`. `main` = `7e4748b`
(the `--no-ff` merge of `v0.6.19`).

**Scratch tags: 24, all annotated, all preserved, none pushed.**
Newest first: `20261106-serial-ctrlc-ring` on `5575868`,
`20261106-sweep-socket-serial` on `fc10881`,
`20261106-sys-execve-no-elf-noise` on `0b07cd8`,
`20261106-open-issues-tz` on `f6dd91a`,
`20261106-rtc-localtime` on `d9df0bc`,
`20261106-open-issues-touch-d-fresh`,
`20261106-ata-flush-cache`, `20261106-get-fattime`,
`20261106-handoff-rtc`, `20261106-rtc-time-syscalls` on `9d7b5d3`,
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

**Stash:** none. `stash@{0}` ("vfork 3b", 2026-11-04) was dropped
this session after the `^C` work was verified independent of it;
the user chose not to keep it. There is no vfork-3b copy.

---

## START HERE

**The next session is `FR_NO_FILE` create-then-set in `f_utime`.**
It is the `sync_window` family (`a0664a8`, `d69c2ab`).  Visible
symptom, now that the RTC is plumbed: `touch f` (no `-d`) on a
nonexistent path shows `2026-01-01 00:00:00`, not the current time.
The fix is `f_sync` in `f_open` before returning -- a behavior change
to `f_open`, own commit, own gate.  See `open-issues.md`, the item at
the top of `Also open -- small defects`.

**After that:** `sync(162)` / `fsync(74)` + `CONFIG_SYNC`/`CONFIG_FSYNC`
(config-bound), then `getlogin` / `hostid` stubs (not in `test.sh`).

---

## WHAT WORKS (verified 2026-11-06, at `5575868`)

- **`canary --full` 28/28.**
- **`test.sh` 57/57.**
- **`EXEC_CHURN` 24/24 ALL-PASS.**
- **`tools/coreutils_sweep.exp` PASS=97 FAIL=0** -- the full gate,
  with both shell behaviors.  Its `sleep 100` + `^C` row is the
  honest two-step: `sleep 100` alone, send `0x03`, require the prompt
  back, then a separate `echo __INT__OK`.
- **Serial `^C` works** (`5575868`: RX ring + bounded tick drain +
  deferred SIGINT on the console fg pgrp).  Proven over three
  transports, each a `/tmp` probe that required `sleep 100` to be
  interrupted and a separate `__INT__OK` to run:
  - UNIX socket: `-serial unix:...,server,nowait` +
    `socat -,raw,echo=0` -- PASS.
  - plain TCP: `-serial tcp:...,server,nowait` + `socat -,raw` -- PASS.
  - **telnet: `-serial telnet:...,server,nowait` + a real `telnet`
    client -- PASS.**
  Caveats (see THE HARNESS RULE): `-serial stdio` cannot carry `^C`
  (the host tty eats it and SIGINTs QEMU); a telnet client in **line
  mode** handles `^C` client-side and never sends the byte.
- **PS/2 `^C` (SDL) works and is unchanged** (`irq1_handler`).
  `^Z` -> `[1]+ Stopped`; `jobs`; `fg`.  **`exit` from `ash` ->
  `donix>`; `busybox sh` -> the ash prompt** (cosmetic
  `sys_execve: f_open(usr/bin/BUSYBOX) -> 4` still appears).
- **`date` prints the host's real date** (RTC read at boot
  `1479723`, plumbed `9d7b5d3`); `touch -d '<date>' f; stat -c %y f`
  round-trips exactly.  `tar -xf` preserves mtime.
- **`flock`, `shred -n 1`, `nproc`, `patch a < p`, `dos2unix`,
  `unix2dos`, `mv x y` (replace), `ln f g` (`-EPERM` on FAT)** -- the
  FAT-coreutils set, `23e37bb` onward.
- **Interactive applets do NOT need `--install`**
  (`FEATURE_SH_STANDALONE=y`).

## WHAT IS BROKEN

- **`touch f` (no `-d`) on a nonexistent path** shows
  `2026-01-01 00:00:00` -- the `FR_NO_FILE` create-then-set in
  `f_utime`.  NEXT.
- **`logname`** prints `logname: getlogin` -- `SYS_GETLOGIN` not in
  dispatcher.  Not in `test.sh`.
- **`hostid`** prints `00000000` -- `sys_gethostid` not in
  dispatcher.  Not in `test.sh`.
- **`sync` / `fsync`** -- `CONFIG_SYNC`/`CONFIG_FSYNC` off, and
  `SYS_SYNC(162)` / `SYS_FSYNC(74)` not in dispatcher.  Config-bound.
- **`env | wc -l` is 2**; `printenv PATH` empty -- the shell does not
  export `PATH`.  Environment gap, not syscall.

---

## THE HARNESS RULE (load-bearing)

**A control byte (`^C` `0x03`, `^Z` `0x1A`) needs a byte path that
actually delivers it to the guest UART.**

- **`./run headless` (Pattern A) cannot**: `./run` pipes QEMU through
  `tee`; an expect-pty byte never reaches the guest, and
  `send "\003"` kills the wrapper (`spawn id ... not open`).
- **`-serial stdio` cannot**: QEMU's stdin is a host tty; `^C` is the
  tty INTR char and SIGINTs QEMU (`terminating on signal 2`).
- **Use one of:** UNIX socket (`-serial unix:...,server,nowait` +
  `socat -,raw,echo=0`), plain TCP (`-serial tcp:...` + raw socket),
  or telnet with a raw/character-mode client.  `-,raw,echo=0` on
  `socat` is load-bearing; without it `0x03` kills socat.
  `tools/coreutils_sweep.exp`'s `^C` row is the worked example;
  `docs/expect-harness.md` has the section.

**A marker on the same line as `sleep` is not a verdict.**
`send "sleep 100; echo __SLEPT__\r"` echoes the command line, so
`expect __SLEPT__` matches the echo and always "passes".  Use the
two-step form.

---

## WHAT WAS REFUTED -- do NOT re-walk.

**This session (the `^C` work):**
- **Tick-time UART *read* to spot `0x03`.**  The data-register read is
  destructive: the tick stole bytes the reader needed.  With a
  one-byte pushback, the token printed and no prompt returned
  (`cmp`); without it, the line corrupted and the shell showed its
  `>` continuation prompt (`awk`).
- **Unbounded drain `while (data-ready)`.**  Froze the guest on
  `ed -s` -- the first row that stays in one syscall long enough for
  the tick to spin.  The committed drain is **bounded to 16 bytes per
  tick**.
- **`__SLEPT__` on the `sleep` line** (matches the shell echo).
- **`socat -v` as a probe** (verbose output fills the expect pty and
  stalls the relay; the `^C` was delayed ~27 s).
- **Raw `socat TCP:` against a `-serial telnet:` chardev**
  (QEMU sends telnet `IAC` negotiation; expect's `i_read` fails on
  the non-UTF-8 bytes).  Use a real telnet client, or `-serial tcp:`.

**Earlier, unchanged.**  Do not re-walk: `-Wl,--dynamic-linker`,
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
the two spawn patterns, the two verdict-pattern rules, the
`-no-shutdown` shutdown rule, the capture rule, **and the
control-byte transport rule**. Read it before writing a `.exp`;
`tools/gate.exp` and `tools/coreutils_sweep.exp` are worked examples.

**In-guest gate.** `cd /`, then `canary --full` (28/28) and
`/root/scripts/test.sh` (57/57). **Plus the two shell tests:**
`sleep 100` then `^C` returns the prompt (PS/2 and, via a socket/
TCP/telnet harness, serial); `exit` from `ash` -> `donix>` ->
`busybox sh` -> the ash prompt. **Use multiple `^Z`/`fg` cycles.**
**`--install -s /bin` is needed only if a test execs an applet by
path**; interactive applet use does not require it.

**Automated gate.** `tools/gate.exp` boots `./run headless` and
runs the three automated gates. `tools/coreutils_sweep.exp` boots
the same and runs the FAT-coreutils sweep **plus the two shell
behaviors**; its `^C` row uses the socket serial form. A green
`gate.exp` is **not** the gate; a green `coreutils_sweep.exp` is.

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

**Current release state.** **`v0.6.19` is the last pushed release.**
`dev` is at `5575868`; ahead **77**, **clean**, **no stash**,
**24 scratch tags**. `origin/dev` = `ba56b3d`. Work is
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
work, job control works (`^C`, `^Z`, `jobs`, `fg`), and the applet
set includes `bc`, `dc`, `diff`, `du`, `awk`, `date`, `uptime`,
`time`, `ed`, `patch`, `xargs`, `flock`, `dd`, `shred`, `timeout`,
`nproc`, `less`, `more`, `hexedit` in addition to the file/text
applets. **`tar` creates, lists and extracts correct archive
members, including directory members, preserving mtime.** **`mkdir
-p` works.** **`flock`, `fdatasync`, `sched_getaffinity`, `link`
are implemented.** **`rename(2)` replaces on FAT (`6651be4`).**
**`utimensat` writes and `stat` reads mtime (`5ccf862`).** `date`
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

**`04_kernel_64bit/fatfs/ff.c`** is tracked and has two
`sync_window` fixes; any further edit there must re-run the full
gate. **`04_kernel_64bit/fatfs/ffconf.h`** is CRLF; binary-safe
edits only.

**`~/code/saved`** -- not a baseline. Run
`git -C ~/code/saved status --short` before any comparison.

**The reset baseline.** `dev` was reset to `873be27`, tag
`reset-baseline-873be27`. `efc8e08` is not on `dev`; it survives as a
commit object. Treat its guards as **refuted**.

---

## One-line summary

**`v0.6.19` is released; `dev` is at `5575868`, ahead 77, clean, no
stash, 24 scratch tags, nothing pushed. Serial `^C` is FIXED and
proven over UNIX socket, plain TCP, and telnet with a real client
(RX ring + bounded tick drain + deferred SIGINT); `-serial stdio`
still cannot carry `^C` (host tty SIGINTs QEMU) and line-mode telnet
clients can eat it. `tools/coreutils_sweep.exp` PASS=97 FAIL=0 with
an honest `^C` row; `gate.exp` CANARY 28/28, TEST_SH 57/57,
EXEC_CHURN ALL-PASS. **NEXT: the `FR_NO_FILE` create-then-set in
`f_utime`** (`f_sync` in `f_open`). Do not move to `v0.6.20` until
the user says so.**
