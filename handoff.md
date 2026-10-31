# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-31.  **`v0.6.18` is released.**  Work is
pre-`v0.6.19`.  The first backgrounded-exec failure is fixed
(`68185de` on `b5c5075`): the already-mounted fast path in
`mount_volume` did not invalidate the shared window, and the next
`dir_find`'s `move_window` was a no-op when `dp->sect ==
fs->winsect`, reading another process's sector as the root's
dirents.  The fix is `fs->wflag = 0; fs->winsect = (LBA_t)0 - 1;`
at the fast-path return, the same pair `check_fs` uses.  Gate on
`68185de`: `canary --full` 28/28 and `test.sh` 57/57, both green,
from `/` on a freshly built image.  Read "The concurrency
contract" in `docs/contract.md` before touching shared state, then
"Where we are".

**THE BUG THAT WAS FIXED, in one place.**  `FATFS` is one global
(`FatFs[vol]`).  `fs->win` and `fs->winsect` are a **volume-global**
one-sector cache shared by every process on the volume.
`move_window(fs, sect)` returns immediately when
`sect == fs->winsect`, trusting that `fs->win` already holds that
sector.  `mount_volume`'s already-mounted fast path
(`fs->fs_type != 0`, drive present) returned `FR_OK` **without
touching `winsect` or `win`**, so the first `f_open` after a
process switch trusted a window another process had left on a
different sector.  The first `f_open("bin/busybox")` returned
`FR_NO_PATH` because `dir_find`'s `move_window(root_sector)` was a
no-op against the wrong window.  The fix is the pair `check_fs`
already uses at every sector load:

    fs->wflag = 0; fs->winsect = (LBA_t)0 - 1;

applied at the fast-path return, so the caller's next `move_window`
always reads.

**The first attempt (`b5c5075`) copied only `winsect = -1`, not
`wflag = 0`.**  With `wflag` still set, the next write that reached
`sync_window` before a `move_window` re-read flushed the window to
sector `-1`, which wraps below the FAT partition.  The ATA driver
refused it (`ATA: refuse write below LBA 2048 on master (requested
LBA 2047)`), the failed write left FatFs corrupted, and `test.sh`
failed from `split` onward with `f_open(...) -> 1` (`FR_DISK_ERR`).
`split` is the first applet that writes files; `canary --full`
passed because none of its rows write below the partition.  The
correction (`68185de`) sets `wflag = 0` before `winsect = -1`, and
`test.sh` is 57/57.

**The one open bug, and the first test of the next session:**

**`kill` on a `RUNNING` background job hangs the OS.**  Killing a
`STOPPED` background job is clean.  Same build, same applets, same
job table; the only variable is the target's state when the signal
lands.  `kill` on a background job did work once, after `fg`/`^Z`
cycles, so the hang is either state-dependent or intermittent.  It
is **not** closed.  The standing repro is `[2]+ Running sleep 300`,
`kill %2` -> hang.

**Not yet run:** `sleep 300 &`, `fg`, `^C` -- does killing a
**foreground** `RUNNING` process hang?  If yes, the bug is
"terminate from `RUNNING`."  If no, it is "terminate a `RUNNING`
background process whose parent is in `wait4`."  Those need
different fixes.  **Blocked on the serial console's unmapped
`^C`**: host `intr = ^C` eats it before the guest sees it.  Run the
test on the framebuffer/PS/2 console, or from a **second serial
connection** (`ps` for the pid, `kill -TERM <pid>`), and state
which launcher and which console were used.

**Baseline on `HEAD`:** `canary --full` 28/28 and `test.sh` 57/57
green on `68185de`.  The backgrounded-exec acceptance passed:
`sleep 5 &` as the first command, multiple backgrounded execs in
one shell, `ls` after -- no `f_open(bin/busybox) -> 5`, root
intact, `jobs` works.  `dev` is ahead of `origin/dev` by 64
commits (nothing pushed).  **Tree: `68185de` on `bda5e40` on
`b5c5075` on `89c7f3f` on `4594038` on `950d4f0` on `23252d2` on
`a1f4eed` on `783b684` on `701dcc1`.**

**THE RELEASE TAGS.**  `v0.6.16` (the VFS), `v0.6.17` (FAT
`st_ino`, `tar`, `chmod`/`chown`), and `v0.6.18` (signals slices
1-5) are the release record; their annotations carry the
per-release summary.  The `20261030-*` and `20261031-mount-fastpath*`
scratch tags are the current restore points; the release procedure
draws the `v0.6.19` summary from them before dropping them.

**WHERE WE ARE.**  `v0.6.15` through `v0.6.18` are released.  Work
is pre-`v0.6.19`.  The working tree is `dev`, clean.  Job control
on the framebuffer/PS/2 console works end to end: Ctrl-Z raises
SIGTSTP to the foreground pgrp; `wait4(WUNTRACED)` reports
`[1]+ Stopped`; `jobs` lists it; `fg` resumes it for both a
`console_read` job (`cat`) and a `nanosleep` job (`sleep 300`).
`bg` works.  Shebang / `/bin/sh` is done (`0d93f8c` + `62ee388`).
The `wait4` stopped-child window is closed (`a1f4eed`).  The
first-backgrounded-exec failure is closed (`68185de`).

**NOT DONE, live:**

- **`kill` on a `RUNNING` background job hangs.**  Above.  The
  foreground `^C` test is blocked on the serial console; run it on
  the framebuffer/PS/2 console or from a second serial.
- `process_stop_and_yield`'s `while (STOPPED) process_yield()` loop
  has not been exercised with **no runnable peer**.
- A `SWITCH-REFUSE: next pid=8 name=busybox rsp outside stack ->
  idle` was observed once, after `fg` cycles, and not since.  Same
  family as the `kill`-RUNNING hang; `scheduler.c:1252` is the site
  and the `context_switch.asm` `0x110` consume is the mechanism.
  **Do not patch `process_stop_and_yield`'s trailing yield
  speculatively.**
- `kill(pid <= 0)` group semantics: `kill(-pgid, sig)` works;
  `kill(0, sig)` still returns `-EINVAL`.
- Ctrl-C on the serial console is not mapped to SIGINT (host
  `intr = ^C` eats it).

**REFUTED SHAPES (do not retry):**

1. `cli`-depth lock around the ATA transfer.  Crashed on boot.
2. `f_sync` on inherited `FIL`s.
3. `f_opendir("/tmp")` + `f_opendir("/")` warm-up.
4. `user_syscall_clear_file_table(self)` alone.
5. Removing `strip_dot_prefix(exec_path)` -- a real latent bug, but
   not the `&` bug.  One-line cleanup for a later commit.
6. `f_mount(NULL)` + `f_mount(fs)` in `sys_execve`.
7. `f_read`/`f_write` `winsect = -1`.
8. `move_window` in `dir_sdi` and `dir_next` tails.  Broke boot.
9. `sys_fork` giving a `FILE_KIND_DIR` child its own `DIR`
   (`bc7bef1`, reverted `950d4f0`).  Produced `/root` and
   `ls: .: No such file`, worse than the original.
10. `follow_path` reading the window for `dp->sect` before the
    sub-directory descent (`4594038`, reverted `89c7f3f`).  No
    change to the failure; aimed one level too deep.
11. Invalidating `winsect` alone at the mount fast-path return,
    without `wflag = 0` (`b5c5075`, corrected by `68185de`).
    `sync_window` flushed the window to sector `-1`; ATA refused;
    FatFs corrupted from `split` onward.

**RULED OUT, do not re-investigate** (theories refuted by
transcripts, not code changes):

- Disk corruption: the `hdd-single.img` recipe `rm -f`s and
  rebuilds the image every build; a transcript showed root intact
  after the same failure.
- Cold FAT cache: two backgrounded execs in one shell, the second
  succeeding after the first failed.
- `sys_*` mutex / cross-process race: the same two-in-a-row
  transcript; the failure reproduces single-threaded.
- CPU / mode / launcher: the QEMU line was identical on failing
  and succeeding runs.

**WHERE WE ARE, the rest:** no real `mount(2)`; file-backed `mmap`;
`link(2)`/`mknod(2)`; `chmod`/`chown` accept and drop; SSE state is
not saved (item 5); no privilege model (item 6).

**NEXT.**

1. **Test `sleep 300 &`, `fg`, `^C`** -- does a foreground `RUNNING`
   kill hang?  On the framebuffer/PS/2 console, or from a second
   serial (`ps`, `kill -TERM <pid>`).  State the launcher and the
   console.
2. **Fix the `kill`-RUNNING hang** -- start from the repro, read
   `sys_kill`'s TERMINATE branch and `process_exit`, and evaluate
   the park-then-kill shape against the foreground-`^C` result.
3. The no-peer stop soak; the interpreter-argv proof.
4. SSE (item 5).  5. Privilege model (item 6).  6. busybox batch 2.
7. PIPE7E soak at 100 Hz.  8. `run`'s `stty susp undef` restore.

### Working style -- how this project gets changed

Read this before proposing any command block.

**One change at a time.** A "change" is a commit, verified before
the next one starts.  **Verify by RUNNING, not by committing.** A
build can pass and the test still be the wrong test.

**A handoff claim is not evidence.** This session's fix was found
by re-running the repro from the committed tree and reading
`mount_volume`/`check_fs` from the bytes.  Every prior theory
(disk corruption, cold cache, `DIR` sharing, `follow_path`,
`sys_*` mutex) was refuted by a transcript.  **Re-run every named
repro on a fresh boot before acting on its diagnosis.**

**A command block is one paste -- write it to a file first.** `cat >
/tmp/x.sh <<'SCRIPT_EOF'` and run it as a **separate** paste.  Never
paste a multi-line block with a trailing backslash into an
interactive shell; it kills the terminal.

**Python edits with `count != 1` abort checks.** `set -e` at the
top.  Count in Python, not `grep`.  Anchor from the file's current
bytes (`cat -A`), including CRLF (`\r\n`) where the file has it,
and open `rb`/`wb` so line endings are preserved.

**A build can pass and the test still be the wrong test.** Run the
feature from a non-root cwd as well as `/`.  Run it the way a user
would reach it.

**Ask for source you do not have.** Before patching a file not seen
this session, ask for it.

**The build is TWO steps.** `make -C 04_kernel_64bit
FAT_CONFIG=single`; `make -C 05_boot_kernel64 hdd-single.img`.
Verify the mtime chain `source` < `ff.o` (if FatFs changed) <
`kernel.elf` < `kernel.bin` < `hdd-single.img`.  A patch with no
new string has only the mtime check; say so in the commit message.

**A write-path change needs a write test.** `b5c5075` invalidated
`winsect` without `wflag = 0`; `canary --full` (read-only and
small writes) stayed green, and `test.sh`'s `split` (the first
applet that writes files) found the regression.  Any change to
`move_window`/`sync_window`/`f_open` must run `test.sh`, not just
`canary --full`, and the `ATA: refuse write` line is the tell.

**`EXTRA_CFLAGS` is NOT forwarded.** `SW2_TRACE` is a `#define` in
`scheduler.c`.

**`nm` output moves between builds.** Re-run `nm`.  `pcb_pool`
stride `0x840`; `_Static_assert` pins offsets through `user_rsp`
(0x610); `PROC_STATE_STOPPED == 6`.  `kernel.elf` has no `-g`.

**A commit message is not a diff and not a test.** `git show
<sha> --stat` is the check; do not write "verified" for something
the transcript does not show.

**Test notes.** `pidof <applet>` finds nothing (every applet's
`comm` is `busybox`); use `ps` and the `STAT` column (`T` is
stopped). `/proc/<pid>/` exists only for live pids.

**Test harness.** `./run` (no arg) is `clean-nofreeze`: full `make
clean`, fresh `kernel.bin`, fresh `hdd-single.img` every time,
`-enable-kvm -cpu host -display sdl`, no freeze.  The Makefile run
targets (`run-single`, `runkernel64-single`, `run-headless`, ...)
use `-cpu max` + TCG, a **different CPU**; a bug report must state
which launcher was used.  `./run` writes `capture.txt` every run
and **overwrites it** -- copy it aside after any failure.  The
shell's default cwd is `/root` (prompt `~`); `pwd` confirms it; the
`root/sleep` PATH candidate is correct for cwd `/root`, not a bug.

**Do not run `git reset --hard` without stashing.  Do not edit
`third_party/`.  Do not commit `kmain.c`'s `pit_init` knob.  Do not
call a race closed on one clean run.**

---

## Releases

**Before a release:** the scratch `2026MMDD-*` tags are annotated
and are the source for the version summary.  **Write the summary
from them BEFORE dropping them.**  The release tag and the merge
commit are local objects; once they exist, the scratch tags may be
dropped.

**The release procedure** (on `dev`, pre-release):

1. **Patch the docs first.**  Update this file's release paragraph
   and (optionally) `docs/version-history.md`.  Commit.
2. **Bump the version string in `kmain.c` manually** (the banner).
   Commit.
3. **Tag** `vX.Y.Z` annotated, on `dev`, summary from the scratch
   tags' annotations and `vPREV..HEAD`:

       git tag -a vX.Y.Z -m "<version summary>"

4. **Drop the scratch tags locally** (the tag and summary now
   exist):

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

Do not use `git push --tags` -- it would push the scratch tags.
Push `vX.Y.Z` by name.

**Current release state.**  **`v0.6.18` is released.**  Work is
now pre-**`v0.6.19`**.  The release tags `v0.6.15` through
`v0.6.18` carry their summaries; the scratch tags they were drawn
from have been dropped.  The two live bugs recorded in the prior
handoff are now one: the backgrounded-exec failure is fixed
(`68185de`), and the `kill`-RUNNING hang is the remaining open bug.

---

## What donix is

A fork of dons-os.  Goal: a Unix-like OS that runs static
musl-linked binaries on the Linux x86_64 ABI, natively.  Newlib
is gone.  Userland is a tracked source tree at `userland/musl/`.
Phase B (busybox) is well underway: the shell is interactive,
pipelines and redirection work, job control works on the
framebuffer/PS/2 console, and shebang / `/bin/sh` is in.  The
applet set includes `bc`, `dc`, `diff`, `du`, `awk` in addition
to the file/text applets.

Full strategy: `docs/strategy.md`.  Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`.

---

## Where we are

### Current baseline

- `test.sh` 57/57; `canary --full` 28/28; pipe7e clean at 100 kHz
  and on a >3.5-hour 100 Hz soak.  The gate ran on `68185de`,
  from `/` on a freshly built image.
- Busybox batch 1 (prompt, readlink, `ls` features,
  human-readable, long options) and five applets (`bc`, `dc`,
  `diff`, `du`, `awk`) enabled and verified by running them.
  Batch 2 is open (see NEXT).
- Signals slices 1-5 complete and verified.  `tkill` (200) in;
  `CONFIG_KILL=y`; `SIG_IGN` fixed and proven.
- Job control works end to end on the framebuffer/PS/2 console:
  Ctrl-Z raises SIGTSTP to the foreground pgrp, `wait4(WUNTRACED)`
  reports `[1]+ Stopped`, `jobs` lists it, `fg` resumes it for both
  `console_read` and `nanosleep` jobs.  `bg` works.
- `docs/contract.md` is the durable concurrency rule; read it
  before any shared-state change.

### Next -- the highest-value work

1. **`kill` on a `RUNNING` background job hangs.**  The standing
   repro and the disambiguating foreground-`^C` test are above.
  This is the more serious open bug.
2. **The no-peer stop soak.**  `process_stop_and_yield`'s
   `while (STOPPED) process_yield()` loop has not been exercised
   with no runnable peer.
3. **The interpreter-argv proof.**  The optional interpreter
   argument reaching the interpreter as a real argv is not
   proven; `-x` is ash-ignorable and the earlier transcripts were
   the fallback path.  A test with an interpreter that can observe
   argv is the way to prove it.
4. **SSE** (item 5): save/restore XMM on context switch and the
   interrupt path, then remove the userland `-mno-sse*` flags.
5. **Privilege model** (item 6).
6. busybox batch 2; `test.sh`'s md5.
7. **PIPE7E soak** at 100 Hz.
8. **`run`'s `stty susp undef` restore** -- save with `stty -g`
   before QEMU and restore with `stty "$old"` after it exits (not
   a bare `stty susp ^Z`, which assumes the original value).

### Refuted -- do NOT re-walk

The numbered `REFUTED SHAPES` above are the full list.  In
addition, the following theories were refuted by transcripts this
session and are **not** shapes (nobody committed them):

- Disk corruption across reboots.
- A cold FAT cache as the first-call failure.
- A `sys_*` mutex / cross-process race.
- CPU / mode / launcher as the failing variable.

---

## Open issues (top 6)

1. **`kill` on a `RUNNING` background job hangs.**  See "The one
   open bug" above and NEXT item 1.
2. **`open-issues.md` item 5: SSE state is not saved.**
3. **`open-issues.md` item 6: no privilege model.**
4. **`process_dump_all`'s `DETACHED` state** -- may mask a
   stranded process bug.
5. **`kill(pid <= 0)` group semantics** -- `kill(-pgid, sig)`
   works; `kill(0, sig)` returns `-EINVAL`.
6. **Ctrl-C on the serial console is not mapped to SIGINT.**

Plus: busybox batch 2, PIPE7E soak, `run`'s `stty` restore, and
the interpreter-argv proof.

---

## State on disk

Kernel sources: `04_kernel_64bit/`.  `capture.txt` is gitignored
and is overwritten every `./run` -- **copy it aside after any
failure.**  `04_kernel_64bit/fatfs/ff.c` is **tracked**, built
directly by the Makefile with the cross-GCC; it is **not**
`third_party/`, and a clone gets it.  The `ff16.zip` archive is
tracked but nothing references or unzips it.

**After a fresh image, one manual step:** the image stages
`/bin/busybox` and `/bin/sh` (a second copy of busybox).  To get
the applet symlinks, boot and run `/bin/busybox --install -s /bin`.
The absolute path is required: busybox reads `/proc/self/exe` to
find itself, donix has no `/proc/self/exe`, and it falls back to
`argv[0]` -- which must start with `/`.  This is a manual step,
not part of the build; automating it is a Makefile/boot change and
is not done.

`v0.6.15` through `v0.6.18` are the release record.  The
`20261030-*` and `20261031-mount-fastpath*` scratch tags are the
current restore points; there is no single tag as a release, and
nothing is pushed until `v0.6.19` is complete and verified.  The
next release procedure draws the release summary from these tags
before dropping them.

---

## One-line summary

**`v0.6.18` is the last release.  The first backgrounded-exec
failure is fixed (`68185de` on `b5c5075`): `mount_volume`'s
already-mounted fast path now does `fs->wflag = 0; fs->winsect =
(LBA_t)0 - 1;`, the same pair `check_fs` uses, so the next
`move_window` reads instead of trusting a window another process
left.**  `FATFS` is one global; `fs->win` and `fs->winsect` are
shared across processes.  The first attempt (`b5c5075`) copied
only `winsect = -1`; the missing `wflag = 0` let `sync_window`
flush to sector `-1`, ATA refused the write, and `test.sh` failed
from `split` onward -- found by `test.sh`, fixed by `68185de`,
`canary --full` 28/28 and `test.sh` 57/57 green.  Backgrounded-exec
acceptance passed: `sleep 5 &` first, multiple backgrounded
execs, `ls` after, root intact.  **The `kill`-RUNNING hang is the
one open bug and the next session's first test, blocked on the
serial console's unmapped `^C`.**  Read `docs/contract.md` and
"NEXT" before any code.
