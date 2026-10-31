# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-31 (session close, fifth session).
**`v0.6.18` is released; work is pre-`v0.6.19`.** This session
fixed the first-backgrounded-exec failure: `mount_volume`'s
already-mounted fast path returned `FR_OK` without invalidating
`fs->winsect`, and `FATFS` is one global (`FatFs[vol]`), so
`fs->win`/`fs->winsect` are shared across processes. The next
`dir_find`'s `move_window(dp->sect)` was a no-op when
`dp->sect == fs->winsect`, reading another process's sector as the
root's dirents, and `f_open("bin/busybox") -> FR_NO_PATH`. The fix
is `check_fs`'s own `fs->winsect = (LBA_t)0 - 1` at the fast-path
return (`b5c5075`).

**The fix is committed and verified.** Mtime chain
`ff.c` < `ff.o` < `kernel.elf` < `kernel.bin` < `hdd-single.img`,
and fresh-boot acceptance: boots covering the three shapes all
passing -- `sleep N &` as the first command with no
`f_open(bin/busybox) -> 5` and `Done` (no `(127)`); two
backgrounded execs in one shell, both succeeding; `ls` first then
`sleep N &`, the warm case still working.

**Tried and refuted this session, do not retry:**
- `bc7bef1`: `sys_fork` giving a `FILE_KIND_DIR` child its own
  `DIR`. Produced `/root` / `ls: .: No such file`, worse than the
  original. Reverted `950d4f0`.
- `4594038`: `follow_path` reading the window for `dp->sect` before
  the sub-directory descent. No change to the failure; aimed one
  level too deep. Reverted `89c7f3f`.
- Disk corruption: refuted by the `hdd-single.img` recipe, which
  `rm -f`s and rebuilds the image every time, and by a transcript
  showing root intact after the same failure.
- Cold FAT cache: refuted by two backgrounded execs in one shell,
  the second succeeding after the first failed.
- `sys_*` mutex / cross-process race: refuted by the same
  two-in-a-row transcript; the failure reproduces single-threaded.
- CPU / mode / launcher: refuted by the QEMU line being identical
  on failing and succeeding runs (`-enable-kvm -cpu host
  -display sdl`, `clean-nofreeze`).

**Test harness facts that confounded this session:**
- `./run` (no arg) is `clean-nofreeze`: full `make clean`, fresh
  `kernel.bin`, fresh `hdd-single.img` every time,
  `-enable-kvm -cpu host -display sdl`, no freeze.
- The Makefile run targets (`run-single`, `runkernel64-single`,
  `run-headless`, ...) use `-cpu max` + TCG, a different CPU from
  `./run`. A bug report must state which launcher was used.
- `./run` writes `capture.txt` every run and overwrites it. Copy
  it aside after any failure; it is the only record of the
  launcher, the build freshness, and the console transcript.
- The shell's default cwd is `/root` (prompt `~`); `pwd` confirms
  it. The `root/sleep` PATH candidate is correct for cwd `/root`,
  not a bug.

**Still open, unchanged:**
- **Bug (2):** `kill` on a `RUNNING` background job hangs the OS;
  a `STOPPED` target kills cleanly. Next session's first test:
  `sleep 300 &`, `fg`, `^C` -- does a foreground `RUNNING` kill
  hang? Blocked on the serial console's unmapped `^C` or a second
  serial / framebuffer console.
- The one-off `SWITCH-REFUSE` (`scheduler.c:1252`).
- `kill(pid <= 0)` group semantics: `kill(-pgid, sig)` works;
  `kill(0, sig)` returns `-EINVAL`.
- Ctrl-C on the serial console is not mapped to SIGINT.
- The rest of the NOT DONE list: no real `mount(2)`; file-backed
  `mmap`; `link(2)`/`mknod(2)`; `chmod`/`chown` accept and drop;
  SSE state is not saved (item 5); no privilege model (item 6);
  busybox batch 2; PIPE7E soak at 100 Hz; `run`'s `stty susp undef`
  restore; the no-peer stop soak; the interpreter-argv proof.

**Tree:** `b5c5075` on `89c7f3f` on `4594038` on `950d4f0` on
`23252d2` on `a1f4eed` on `783b684` on `701dcc1`. `dev` is ahead
of `origin/dev` by 62 commits (nothing pushed).

**Read "The concurrency contract" in `docs/contract.md` before
touching shared state, then "Where we are".**

### Working style -- how this project gets changed

Read this before proposing any command block.

**One change at a time.** A "change" is a commit, verified before
the next one starts. **Verify by RUNNING, not by committing.** A
build can pass and the test still be the wrong test.

**A handoff claim is not evidence.** This session found the
mechanism only by re-running the repro from the committed tree and
reading `mount_volume`/`check_fs` from the bytes. Every prior
theory (disk corruption, cold cache, `DIR` sharing, `follow_path`)
was refuted by a transcript.

**A command block is one paste -- write it to a file first.** `cat >
/tmp/x.sh <<'SCRIPT_EOF'` and run it as a **separate** paste. Never
paste a multi-line block with a trailing backslash into an
interactive shell; it kills the terminal.

**Python edits with `count != 1` abort checks.** `set -e` at the
top. Count in Python, not `grep`. Anchor from the file's current
bytes (`cat -A`), including CRLF (`\r\n`) where the file has it,
and open `rb`/`wb` so line endings are preserved.

**A build can pass and the test still be the wrong test.** Run the
feature from a non-root cwd as well as `/`. Run it the way a user
would reach it.

**Ask for source you do not have.** Before patching a file not seen
this session, ask for it.

**The build is TWO steps.** `make -C 04_kernel_64bit
FAT_CONFIG=single`; `make -C 05_boot_kernel64 hdd-single.img`.
Verify the mtime chain `source` < `ff.o` (if FatFs changed) <
`kernel.elf` < `kernel.bin` < `hdd-single.img`. A patch with no new
string has only the mtime check; say so in the commit message.

**`EXTRA_CFLAGS` is NOT forwarded.** `SW2_TRACE` is a `#define` in
`scheduler.c`.

**`nm` output moves between builds.** Re-run `nm`. `pcb_pool`
stride `0x840`; `_Static_assert` pins offsets through `user_rsp`
(0x610); `PROC_STATE_STOPPED == 6`. `kernel.elf` has no `-g`.

**A commit message is not a diff and not a test.** `git show
<sha> --stat` is the check; do not write "verified" for something
the transcript does not show.

**Test notes.** `pidof <applet>` finds nothing (every applet's
`comm` is `busybox`); use `ps` and the `STAT` column (`T` is
stopped). `/proc/<pid>/` exists only for live pids.

**Do not run `git reset --hard` without stashing. Do not edit
`third_party/`. Do not commit `kmain.c`'s `pit_init` knob. Do not
call a race closed on one clean run.**

---

## Releases

**`v0.6.18` is released.** Work is pre-**`v0.6.19`**. The
`20261030-*` tags are the restore points. **Nothing is pushed**
until `v0.6.19` is ready and verified. Bug (2) (`kill` on
`RUNNING`) is **not** a release blocker for `v0.6.19` unless the
next session decides it is; it is recorded so it is not lost.

---

## One-line summary

**The first backgrounded exec failure is fixed (`b5c5075`):
`mount_volume`'s fast path now invalidates `fs->winsect`, the same
way `check_fs` already did, so the next `move_window` reads.**
`FATFS` is one global; `fs->win`/`fs->winsect` are shared across
processes, and the fast path had been trusting them. Verified by
the build mtime chain and fresh-boot acceptance covering the three
shapes (`sleep N &` first, two backgrounded execs, `ls`-first warm
case). Two fixes tried this session and reverted (`bc7bef1`
`DIR`-share, `4594038` `follow_path`), plus disk corruption, cold
cache, `sys_*` mutex, and CPU/mode/launcher all refuted by
transcripts. **Bug (2), `kill` on a `RUNNING` background job, is
still open and is the next session's first test.** Read
`docs/contract.md` and "Still open" before any code.
