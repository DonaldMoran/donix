# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-21.  **`v0.6.16` is the VFS release;
`v0.6.17` fixes FAT `st_ino` and re-enables `tar`.**  Every path syscall reaches its filesystem through the
mount table (`mount_driver(backend)->op(...)`); the resolver is
`vfs_resolve(path, follow_final)`; symlinks work end to end.  `test.sh`
is 57/57 and `canary --full` is 28/28, verified from `/` on a freshly
built image.  Read "The concurrency contract" in `docs/contract.md`
before touching shared state, then "Where we are".

**WHAT THE VFS IS NOW, in one paragraph.**  `g_mounts` maps `/`
(FAT), `/dev`, and `/proc` to `fs_driver_t` vtables: `unlink`,
`rename`, `mkdir`, `open`, `stat`, `access`.  `resolve_at` became
`vfs_resolve`, which canonicalizes the path, selects the mount by
longest prefix on a component boundary, and -- when `follow_final` is
1 -- follows a `DONIX_LINK:` marker (an ordinary FAT file whose first
line is `DONIX_LINK:<target>`), capped at 8 hops (`-ELOOP`).  Every
path syscall calls `vfs_resolve` and then the mount's operation; no
syscall tests `BACKEND_*` to choose an operation.  Devices are rows in
`g_dev_table` with `(major, minor, ino, read, write)`; `/dev/null` is
1,3; `console` 5,1; `tty` 5,0; `zero` 1,5; `urandom` 1,9.
`st_dev`/`st_ino` are identity, `st_rdev` is the device number.

**WHAT IS NOT DONE.**  A real `mount(2)` (the table is `static const`);
file-backed `mmap`; `link(2)`/`mknod(2)` (FAT cannot store them);
signals beyond slice 2 (see "NEXT": slices 3-5 are open);
shebang + `/bin/sh`.  `tar` is enabled and link restoration is verified (the v0.6.16
note that called it a busybox issue was wrong -- FAT `st_ino` was 0;
`76d66db` gives it a real value).  `chmod`/`chown` (90-93) accept and
drop: FAT has no mode or owner.

**THE RELEASE TAGS.**  `v0.6.16` (the VFS) and `v0.6.17` (FAT
`st_ino`, `tar`, `chmod`/`chown`) are the release record; their
annotations carry the per-release summary.  The scratch
`2026MMDD-*` tags were development restore points and have been
dropped, as the release procedure says to do once the tag and the
summary exist.

**WHERE WE ARE.**  `v0.6.15` (busybox batch 1), `v0.6.16` (the VFS),
and `v0.6.17` (FAT `st_ino`, `tar`, `chmod`/`chown`) are released.
The working tree is `dev`.

**NEXT.**  Signals (open-issues 12).  **Slice 1 is DONE** --
three commits (`588ae53`, `3f60a19`, `ce69a7b`), verified by
running.  **Slice 2 is DONE** (`866588f` docs, `d2126d8` code),
verified by running: `kill_self` in the user shell installs a
handler, raises SIGUSR1, the handler runs (`sig=0xa`), returns via
a restorer trampoline (`mov $15,%rax; syscall`), and the caller
resumes ("caller resumed after handler").  `killtest` in the
kernel shell still reads `sig_raise=1 sig_deliver=0` -- expected,
because `killtest` has no handler and does not return to user mode.
`sys_rt_sigreturn` (15) is new: `SYS_RT_SIGRETURN` in `syscall.h`,
`sys_rt_sigreturn` in `user_syscall.c`, `ucontext_t` frame at
`restorer (8) + 816 bytes`.  **Slice 3 is delivery on `iretq`**
(the interrupt-return path), which is the work in
`context_switch.asm`; it needs that file open and its own session.

**What slice 2 taught the slice-3 author, so it is not re-walked:**

- **Do NOT call `signal_deliver_frame` from the asm stub.**  The
  first attempt did (after the exit-check `cli`); the extra call +
  return address + callee frame pushed the 16 KB kernel stack over
  on the deepest syscalls (`musl_sh`'s `read(0)` at its prompt),
  and the stub's `pop`s read user-stack data as the resume frame.
  Boot-time `#PF` at RIP `0x40B845`, CR2 `0x1000`.  The deliver
  check runs at the top of `syscall_dispatch`, inside the C frame
  every syscall already returns through.  Slice 3's `iretq` path
  has no such C frame; it will need its own shape, and the
  alignment/depth problem is the same one.
- **The handler entry `RSP` is `frame`, not `frame + 8`.**  The
  frame is `[restorer][ucontext_t]`; the handler is a normal C
  function whose epilogue `pop rbp; ret` pops `frame[0]`.  If entry
  RSP is `frame + 8`, the `ret` pops `frame[16]` (`uc_link = 0`)
  and jumps to address 0.  `sys_rt_sigreturn` reads the
  `ucontext_t` from the user RSP saved at syscall entry, which is
  `frame + 8` because the restorer was popped; it does not subtract
  8.
- **`killtest` cannot close a slice's delivery acceptance.**  It
  has no handler and does not return to user mode.  Use
  `kill_self` (user shell) for delivery-and-restore, and add a
  slice-3-specific test in the user shell for the tick-delivered
  path.  `sigdump`/`killtest` stay as slice-1 instruments.
- **Known limit (not fixed):** `signal_deliver_frame` requires a
  nonzero `restorer` (matching `SA_RESTORER`).  A caller that
  installs a handler with `restorer == 0` gets no delivery and the
  pending bit stays set.  Musl always sets `SA_RESTORER`; this does
  not bite today.  Tracked as a follow-up.

**DONE SINCE `v0.6.17`** (scratch tag `20261020-psl-sysinfo`, on
`dev`, no version bump): `ps -l` works -- busybox gained
`FEATURE_PS_LONG`/`PS_TIME`, and the kernel gained `sys_sysinfo`
(99).  **99 is `sysinfo`, not `gettimeofday`** (96; `time` is 201),
and neither 96 nor 201 is reached by anything on the image.
`ps -l`'s `STIME` was a separate defect: `proc_build_stat` emits
`p->creation_time`, which was never assigned; `process_initialize_pcb`
now records `g_ticks`.  Also in this batch: `tar` added to the
README applet list; `syscall.h` defines 90-93 moved into numeric
order; `user_syscall.c`'s 90-93 `case` lines moved to match.

**Signals slice 1 and 2** are also on `dev` since `v0.6.17`:
`588ae53`, `3f60a19`, `ce69a7b` (slice 1: pending/blocked masks,
`kill`/`tgkill` raise, `sigdump`/`killtest`), `866588f` (slice-2
design amendment, `rt_sigreturn` and the `ucontext_t` frame), and
`d2126d8` (slice 2: delivery at the top of `syscall_dispatch`,
`sys_rt_sigreturn`, the widened `rt_sigaction` read).  No scratch
tag yet; the next release procedure draws the summary from the
commit log, not from a tag.

**Working style -- how this project gets changed**

Read this before proposing any command block.

**One change at a time.** A "change" is a commit, verified before the
next one starts.  **Verify by RUNNING, not by committing** -- build,
run, see the behavior, THEN commit.

**The state check comes before the command block.** Every command
block starts with `git status -sb` (or `git log`).

**A command block is one paste -- write it to a file first.**
`git add`, `git status`, `git commit`, and `git tag` belong in **one**
block, with the message inline via heredoc.  **Do not paste a block
with `cd ... || exit 1` into an interactive shell** -- the `exit`
closes the terminal.  Write to `/tmp/<name>.sh` with
`cat > ... <<'SCRIPT_EOF'`, then `bash /tmp/<name>.sh`.

**Python edits with `count != 1` abort checks are the preferred edit
style.**  **`set -e` at the top.**  **Idempotent edit scripts.**
**Do the count check IN PYTHON (`str.count`), not with `grep`.**
`grep -Fc` does not do multi-line matches; `grep -Fxc` requires a
whole-line match.  `str.count` is the only honest multi-line count.

**BEFORE ASSERTING A COUNT, count the occurrences in the file and
write the expected number from that count, not from memory.**

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION.**  Grep the
file, confirm the count is 1 **and** the match is in the intended
function.

**A post-write check string must be copied from the `new` literal,
not retyped.**

**Ask for source you do not have.**  Before patching a file it has
not seen **in this session**, ask for it.  **Before writing an anchor,
read the file with `cat -A` and quote the bytes.**

**The build is TWO steps.**  `make -C 04_kernel_64bit
FAT_CONFIG=single` builds the kernel; `make -C 05_boot_kernel64
hdd-single.img` builds the image.  Running only the second copies a
STALE `kernel.bin`.  Always run both, and verify `kernel.elf` is newer
than the source you edited.

**`nm` output moves between builds.**  Re-run `nm` before using any
address.

**A diagnostic on the SUCCESS path is noise.  A diagnostic on the
FAILURE path is signal.**  `SW2-*`, `HEAP_TRACE_MERGE`, the
`HEAP: corrupt` prints, and `heap_validate`'s check prints are
success-or-transient-path; they are gated.  The refuse lines
(`SWITCH-DEAD`, `YIELD-REFUSE`, `SWITCH-REFUSE`, `EXIT-REFUSE`) and
the `C` report are failure-path (kept).

**GDB over `-s` has no types** (`kernel.elf` is built WITHOUT `-g`).
Use raw offsets: `pid` 0x000, `name` 0x008, `state` 0x028, `cr3`
0x030, `entry_point` 0x038, `kernel_stack_virt` 0x048,
`kernel_stack_top` 0x050, `rsp` 0x110, `rip` 0x118, `next` 0x120,
`prev` 0x128, `block_kind` 0x158.  `sizeof(pcb_t)` = 0x618.
**CONSIDER BUILDING WITH `-g`.**

**Do not run `git reset --hard` without stashing first.  Do not edit
`third_party/`.  Do not commit `kmain.c`'s `pit_init` knob -- the
committed value is 100.  Do not call a race closed on one clean run.**

---

## Releases

**Before a release:** the scratch `2026MMDD-*` tags are annotated and
are the source for the version summary.  **Write the summary from
them BEFORE dropping them.**  The release tag and the merge commit
are local objects; once they exist, the scratch tags may be dropped.

**The release procedure** (on `dev`, pre-release):

1. **Patch the docs first.**  Update this file's release paragraph and
   (optionally) `docs/version-history.md`.  Commit.
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

Do not use `git push --tags` -- it would push the scratch tags.  Push
`vX.Y.Z` by name.

**Current release state.**  **`v0.6.17` is released.**  Work is now
pre-**`v0.6.18`**.  The release tags `v0.6.15`, `v0.6.16`, and
`v0.6.17` carry their summaries; the scratch tags they were drawn
from have been dropped.

---

## What donix is

A fork of dons-os.  Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 ABI, natively.  Newlib is gone.
Userland is a tracked source tree at `userland/musl/`.  Phase B
(busybox) is well underway: the shell is interactive, pipelines and
redirection work, and the applet set now includes `bc`, `dc`, `diff`,
`du`, `awk` in addition to the file/text applets.

Full strategy: `docs/strategy.md`.  Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`.

---

## Where we are

### CLOSED -- the `C` (stale-ZOMBIE dispatch) and the heap cycle hang

Both fixed.  See "What was fixed" and "THE VERIFICATION" at the top.
Do NOT re-walk the refuted paths below.

### Current baseline

- `test.sh` 57/57; canary `--full` 28/28; pipe7e clean at 100 kHz and
  on a >3.5-hour 100 Hz soak.
- Busybox batch 1 (prompt, readlink, `ls` features, human-readable,
  long options) and five applets (`bc`, `dc`, `diff`, `du`, `awk`)
  enabled and verified by running them.
- `docs/contract.md` is the durable concurrency rule; read it before
  any shared-state change.

### Next -- the highest-value work

1. **Signals (open-issues item 12).**  The first asynchronous state
   writer.  **Design: `docs/signal-design.md`** (written, five
   slices).  Start with slice 1 -- the pending mask, no delivery --
   which does not touch the syscall-return frame.  Delivery (slice
   2) is the frame work; it needs `user_syscall_entry.asm` and
   `context_switch.asm` open, and its own session.  This is what
   makes `SIGPIPE`, `SIGSEGV` handlers, and `ash` job control
   possible.
2. **Shebang + `/bin/sh`.**  `sys_execve` `#!` parsing with a
   recursion guard, and an interpreter on the image.
3. The open-issues list (top 6, below).
4. **The empty-file `st_ino` collision** -- two empty files with the
   same name in different directories share an inode, because
   `FILINFO` carries no parent.  A parent-directory input is needed.

### Refuted -- do NOT re-walk

- **The reserve / frame placement.**  `67701df` + `5587d70`.
- **The `next->rsp = 0` clear.**  Broke blocking; reverted.
- **The pipe-waiter fields as the `C`'s stale holder.**  The pipe7e
  helper writes 2 bytes and never blocks, so they are never set.
- **The `current_process` dual-copy divergence at the `C`.**  Both
  copies agreed.  (The `process_destroy` fix is still correct.)
- **A switch-to-self guard in `scheduler_switch_to`'s TAIL.**  It
  fires after `current_process = next` and refuses every dispatch.
  The tail check is `state != READY`, not `next == current_process`.
- **A caller heap overflow.**  The enriched trace + the merge trace
  proved the merge arithmetic correct; the "corruption" was an
  interleaved `kfree`.  The heap had a race, not an overflow.
- Do not re-open: item 11, the session-48 zone scan,
  the `fs_base` path, the `longjmp`/`setjmp` family.

---

## Open issues (top 6)

1. **`open-issues.md` item 12: signals, slices 3-5 remain.**  Slices
   1 and 2 are done (`d2126d8`).  Slice 3 is `iretq` delivery; slice
   4 is fault raises (`SIGSEGV`/`SIGBUS`/`SIGILL`/`SIGFPE` set the
   pending bit at fault entry instead of killing); slice 5 is
   `SIGPIPE`/`SIGCHLD`/default actions.  Known limit from slice 2:
   `signal_deliver_frame` requires a nonzero `restorer`.
2. **`open-issues.md` item 10: `CR4.OSFXSR` without an XMM save.**
3. **`open-issues.md` item 13: `CONFIG_ASH_JOB_CONTROL` gap.**
4. **`process_dump_all`'s `DETACHED` state** -- may mask a stranded
   process bug.
5. **Item 7a/7b** -- the boot-time `#PF` and the busybox `#GP`.
6. **`gettimeofday` (96) / `time` (201) not implemented** -- no
   caller on the image today; `date`/`time` need them if enabled.

Plus: **the VFS** (see Next), **shebang + `/bin/sh`**, and the
open-issues list.

---

## State on disk

Kernel sources: `04_kernel_64bit/`.  `capture.txt` is gitignored.

**After a fresh image, one manual step:** the image stages
`/bin/busybox` alone.  To get the applet symlinks (including
`/bin/sh`, which shebang will need), boot and run
`/bin/busybox --install -s /bin`.  The absolute path is required:
busybox reads `/proc/self/exe` to find itself, donix has no
`/proc/self/exe`, and it falls back to `argv[0]` -- which must
start with `/`.  This is a manual step, not part of the build;
automating it is a Makefile/boot change and is not done.
`v0.6.15`, `v0.6.16`, and `v0.6.17` are the release record.  One
scratch tag exists: `20261020-psl-sysinfo`, the restore point for
the `ps -l` / sysinfo work after `v0.6.17`.  It is dropped by the
next release procedure.

---

## One-line summary

**The VFS is DONE and released (`v0.6.16`); `v0.6.17` fixes FAT
`st_ino`, re-enables `tar`, and accepts `chmod`/`chown`.  Since
`v0.6.17`: `ps -l` works via `sys_sysinfo` (99) and a real
`creation_time` for `STIME`; **signals slices 1 and 2 are DONE**
(`d2126d8`), verified by running `kill_self` end to end (install,
raise, handler, `sys_rt_sigreturn`, caller resumes).  Next: slice 3,
`iretq` delivery.  Read `docs/contract.md`'s Signals section and
"NEXT" in this file before any code.**
