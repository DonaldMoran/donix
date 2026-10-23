# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-23.  **`v0.6.17` is the last release.
Signals slices 1-5 are DONE on `dev`.**  The restore point is
`20261023-sigign`.  Every path syscall reaches its filesystem
through the mount table (`mount_driver(backend)->op(...)`); the
resolver is `vfs_resolve(path, follow_final)`; symlinks work end to
end.  `test.sh` is 57/57 and `canary --full` is 28/28, verified
from `/` on a freshly built image, three times interleaved.  Read
"The concurrency contract" in `docs/contract.md` before touching
shared state, then "Where we are".

**WHAT THE VFS IS NOW, in one paragraph.**  `g_mounts` maps `/`
(FAT), `/dev`, and `/proc` to `fs_driver_t` vtables: `unlink`,
`rename`, `mkdir`, `open`, `stat`, `access`.  `resolve_at` became
`vfs_resolve`, which canonicalizes the path, selects the mount by
longest prefix on a component boundary, and -- when `follow_final`
is 1 -- follows a `DONIX_LINK:` marker (an ordinary FAT file whose
first line is `DONIX_LINK:<target>`), capped at 8 hops (`-ELOOP`).
Every path syscall calls `vfs_resolve` and then the mount's
operation; no syscall tests `BACKEND_*` to choose an operation.
Devices are rows in `g_dev_table` with `(major, minor, ino, read,
write)`; `/dev/null` is 1,3; `console` 5,1; `tty` 5,0; `zero` 1,5;
`urandom` 1,9.  `st_dev`/`st_ino` are identity, `st_rdev` is the
device number.

**WHAT IS NOT DONE.**  A real `mount(2)` (the table is `static
const`); file-backed `mmap`; `link(2)`/`mknod(2)` (FAT cannot store
them); shebang + `/bin/sh` in the kernel (the image half is done);
`chmod`/`chown` (90-93) accept and drop, FAT has no mode or owner;
and three signal gaps, named in "Signals: what remains" below --
SIGCHLD does not interrupt a blocked `wait4`, there is no
`sigsuspend`/`pause`, and SIGSTOP/SIGTSTP default-terminate rather
than stop.  SSE state is not saved (open-issues item 10).  No
privilege model (item 9).

**THE RELEASE TAGS.**  `v0.6.16` (the VFS) and `v0.6.17` (FAT
`st_ino`, `tar`, `chmod`/`chown`) are the release record; their
annotations carry the per-release summary.  The scratch
`2026MMDD-*` tags since `v0.6.17` are development restore points
and are dropped when `v0.6.18` ships.

**WHERE WE ARE.**  `v0.6.15` (busybox batch 1), `v0.6.16` (the
VFS), and `v0.6.17` (FAT `st_ino`, `tar`, `chmod`/`chown`) are
released.  The working tree is `dev`, ahead of `origin/dev` by 37
commits, nothing pushed.  Pre-`v0.6.18`.

**NEXT.**  Signals are done for the scope they were given.  The
three remaining gaps are real work, not probes, and each is its
own session: (a) SIGCHLD interrupting a blocked `wait4` -- a
signal-arrival wake for a blocked syscall, which is a scheduler
change, not a delivery change; (b) `sigsuspend`/`pause` -- an
atomic unblock-and-wait, which is a syscall addition; (c)
SIGSTOP/SIGTSTP stop + SIGCONT resume -- job control, open-issues
item 13.  After those, in order: shebang + `/bin/sh` kernel half
(item 14); SSE save/restore then remove the userland `-mno-sse*`
flags (item 10); the privilege model (item 9); busybox batch 2.
`test.sh`'s md5 is still listed in the old work list; it is not
blocking anything.

### Signals: what is in, and what was proven

Delivered at three points: syscall exit (after the switch, with
the interrupted syscall's result passed to `signal_deliver_frame`
and preserved in `ucontext.gregs[RAX]`), the tick's `iretq`
(`signal_deliver_frame_iretq`), and the fault frame
(`signal_deliver_frame_exc`).  Each refuses a kernel-mode frame
(`(cs & 3) != 3`); the tick path additionally refuses to dispatch
mid-syscall.  `sys_rt_sigreturn` returns `v[G_RAX]` -- the
interrupted syscall's result -- so a `wait4` that a signal was
delivered over still reports the child pid.

Raises: SIGCHLD in `process_exit` (bit-set only, after the wake
and ZOMBIE, inside the existing `cli`); SIGPIPE in `sys_write`'s
pipe branch (`readers_open == 0`); SIGKILL acted on at the raise
(it cannot be caught or blocked); everything else via
`kill`/`tgkill`/`tkill`.

Dispositions: `signal_handler[]` stores 0 (`SIG_DFL`), 1
(`SIG_IGN`), or a handler address.  The three deliver helpers
branch: `0` -> `signal_default_action`; `1` -> clear the bit,
resume without a handler; otherwise build the user-stack frame
and enter the handler.  `signal_default_action` is ignore for
SIGCHLD, SIGURG, SIGWINCH, SIGCONT and terminate for everything
else.

**Verified by running, 2026-10-23, on the `20261023-sigign`
build:**

- Gate: `echo hi | wc` -> `1 1 3`; `sigchld_probe` -> handler
  ran, `wait4` returned 5, reaped; `sleep 1` returns;
  `canary --full` 28/28; `sh /root/scripts/test.sh` 57/57.
  Run three times interleaved.
- SIGPIPE: `yes | head -n 1 ; echo after` -> `y` / `after`.
- Default-terminate: `kill -9 $$` -> `Killed`, status 137;
  `kill -15 $$` -> `Terminated`, status 143; `kill -2 $$` ->
  silent, status 130 (busybox suppresses the SIGINT message;
  the exit status is the proof).  Parent shell survives all
  three.
- SIG_IGN: `sigterm_ignore` -> `survived kill(SIGTERM)`,
  status 0.
- Real handlers unchanged: `kill_self` (handler before `kill`
  returns), `sigsegv_probe` (handler round-trip, resumes, exit
  0), `sigspin` (tick delivery, `HANDLER-RAN i=1`).

`tkill` (200) is implemented as `kill(tid, sig)`, a thin wrapper
over `sys_kill`; busybox `kill` uses it to signal itself, and
without it `kill -2 $$` printed `Unknown syscall: 200`.

**SIG_IGN was broken and is fixed.**  `sys_rt_sigaction` used to
collapse `SIG_IGN` (1) to 0, making it indistinguishable from
`SIG_DFL`; a process that ignored a signal was terminated by it.
`sigterm_ignore` found it (status 143).  The collapse is deleted;
`SIG_IGN` stays 1 and the deliver helpers clear-and-resume.  This
is commit `6de2d63`, tag `20261023-sigign`.

**Known limits carried forward:**
- SIGCHLD does not interrupt a blocked `wait4`.  The shell's
  `wait` loop works because `process_wake_parent_if_waiting`
  wakes the parent; a program that blocks in `read` and expects
  SIGCHLD's handler to run stays blocked until something else
  wakes it.
- No `sigsuspend`/`pause`/`sigwait`; there is no atomic
  unblock-and-wait.
- SIGSTOP/SIGTSTP default-terminate (STOP is not implemented);
  SIGCONT's default is ignore, which is correct, but there is no
  stopped state to continue.  Job control is open-issues item 13.
- `signal_deliver_frame*` requires a nonzero restorer (musl
  always sets `SA_RESTORER`; a caller that installs a handler
  with `restorer == 0` gets no delivery and the bit stays set).
- No SIGBUS generator: x86 has no `#BUS` vector and donix's
  `mmap` is anonymous-only.
- `siginfo` is passed as NULL; the handler is the simple
  `void(int)` shape.  `sigaction` masks and `SA_*` flags beyond
  `SA_RESTORER` are not honored.
- `signal_deliver_frame` and `sys_rt_sigreturn` both compute the
  frame base as `g_syscall_stack_top - 120` while
  `user_syscall_entry.asm` pushes 128 bytes.  The gate passing
  means the constant is consistent with what is tested, not that
  it is right.  A change to the stub's pushes or the delivery
  point must re-derive it.

**What slice 2 taught the slice-3 author, so it is not re-walked:**

- **Do NOT call `signal_deliver_frame` from the asm stub.**  The
  first attempt did; the extra call + return address + callee
  frame pushed the 16 KB kernel stack over on the deepest
  syscalls (`musl_sh`'s `read(0)` at its prompt), and the stub's
  `pop`s read user-stack data as the resume frame.  Boot-time
  `#PF` at RIP `0x40B845`, CR2 `0x1000`.  The deliver check runs
  at the top of `syscall_dispatch`, inside the C frame every
  syscall already returns through.
- **The handler entry `RSP` is `frame`, not `frame + 8`.**  The
  frame is `[restorer][ucontext_t]`; the handler is a normal C
  function whose epilogue `pop rbp; ret` pops `frame[0]`.  If
  entry RSP is `frame + 8`, the `ret` pops `frame[16]`
  (`uc_link = 0`) and jumps to address 0.  `sys_rt_sigreturn`
  reads the `ucontext_t` from the user RSP saved at syscall
  entry, which is `frame + 8`.
- **`killtest` cannot close a slice's delivery acceptance.**  Use
  `kill_self` for delivery-and-restore, `sigspin` for the tick
  path, `sigsegv_probe` for the fault path, `sigchld_probe` for
  SIGCHLD, `sigterm_ignore` for SIG_IGN.

**DONE SINCE `v0.6.17`** (restore points: `20261021-slice5-done`,
`20261023-tkill`, `20261023-sigign`, on `dev`, no version bump):

*Batch 1 -- `ps -l` / sysinfo* (`20261020-psl-sysinfo`): `ps -l`
works -- busybox gained `FEATURE_PS_LONG`/`PS_TIME`, and the
kernel gained `sys_sysinfo` (99).  **99 is `sysinfo`, not
`gettimeofday`** (96; `time` is 201).  `ps -l`'s `STIME` was a
separate defect: `proc_build_stat` emits `p->creation_time`,
which was never assigned; `process_initialize_pcb` now records
`g_ticks`.

*Batch 2 -- kernel shell echo* (`b06787d`): the kernel shell's
echo and command output go to the serial terminal.

*Batch 3 -- signals slices 1-5:*
- Slice 1 (`588ae53`, `3f60a19`, `ce69a7b`): pending and blocked
  masks, `kill`/`tgkill` raise, `sigdump`/`killtest`.  No
  delivery.
- Slice 2 (`866588f` design, `d2126d8` code): delivery at the top
  of `syscall_dispatch`, `sys_rt_sigreturn` (15), the
  `ucontext_t` frame at `restorer (8) + 816 bytes`.  Verified by
  `kill_self`.
- Slice 3 (`17ffeb9`, `a1c90ff`, `c4b3ba7`): delivery on `iretq`,
  `signal_deliver_frame_iretq` taking the target PCB as a
  parameter.  Verified by `sigspin`.
- Slice 4 (`1d256ed`, `f90d517`): fault raises.  Verified by
  `sigsegv_probe`.
- Slice 5 (`7245390` = `20261021-slice5-done`): SIGPIPE,
  default-action table, SIGKILL.  Then the SIGCHLD half:
  `593b349` (deliver on the way out, result preserved),
  `dfd5240` (stage `sigchld_probe`), `42302e7` (SIGCHLD raise in
  `process_exit`, result preserved via `v[G_RAX]`).  Verified by
  `sigchld_probe`, `yes | head`, and the `kill -9/-15/-2 $$`
  exit statuses 137/143/130.
- `7994fce`: `sigchld_probe` source brought into the tree -- it
  had existed only on the scratch copy, and a clean build failed
  without it.
- `b976b40`: `tkill` (200).
- `33c4384` = `20261023-tkill`: busybox `CONFIG_KILL=y`, so the
  default-action table could be exercised.
- `6de2d63` = `20261023-sigign`: `SIG_IGN` is a disposition, not
  `SIG_DFL`; `sigterm_ignore` added and surviving.

**The scratch tags since `v0.6.17` are the restore points**:
`20261020-psl-sysinfo`, `20261021-signal-slice1-base`,
`-slice1-pre-docs`, `-slice1-docs-post`, `-slice2-done`,
`-slice3-done`, `-slice4-done`, `-slice5-done`,
`-shell-echo-pre-fix`, `-shell-echo-post-fix`, `20261023-session-work`,
`20261023-tkill`, `20261023-sigign`.  Nothing is pushed: the
policy is not to push until `v0.6.18` is ready and verified.  The
release procedure draws the summary from these and drops them.

**Working style -- how this project gets changed**

Read this before proposing any command block.

**One change at a time.** A "change" is a commit, verified before
the next one starts.  **Verify by RUNNING, not by committing** --
build, run, see the behavior, THEN commit.

**The state check comes before the command block.** Every command
block starts with `git status -sb` (or `git log`).

**A command block is one paste -- write it to a file first.**
`git add`, `git status`, `git commit`, and `git tag` belong in
**one** block, with the message inline via heredoc.  **Do not
paste a block with `cd ... || exit 1` into an interactive
shell** -- the `exit` closes the terminal.  Write to
`/tmp/<name>.sh` with `cat > ... <<'SCRIPT_EOF'`, then
`bash /tmp/<name>.sh`.

**Python edits with `count != 1` abort checks are the preferred
edit style.**  **`set -e` at the top.**  **Idempotent edit
scripts.**  **Do the count check IN PYTHON (`str.count`), not
with `grep`.**  `grep -Fc` does not do multi-line matches;
`grep -Fxc` requires a whole-line match.  `str.count` is the only
honest multi-line count.

**BEFORE ASSERTING A COUNT, count the occurrences in the file and
write the expected number from that count, not from memory.**

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION.**  Grep the
file, confirm the count is 1 **and** the match is in the intended
function.

**A post-write check string must be copied from the `new`
literal, not retyped.**

**Ask for source you do not have.**  Before patching a file it
has not seen **in this session**, ask for it.  **Before writing
an anchor, read the file with `cat -A` and quote the bytes.**

**The build is TWO steps.**  `make -C 04_kernel_64bit
FAT_CONFIG=single` builds the kernel; `make -C 05_boot_kernel64
hdd-single.img` builds the image.  Running only the second copies
a STALE `kernel.bin`.  Always run both, and verify `kernel.elf`
is newer than the source you edited.

**`nm` output moves between builds.**  Re-run `nm` before using
any address.

**A diagnostic on the SUCCESS path is noise.  A diagnostic on
the FAILURE path is signal.**  `SW2-*`, `HEAP_TRACE_MERGE`, the
`HEAP: corrupt` prints, and `heap_validate`'s check prints are
success-or-transient-path; they are gated.  The refuse lines
(`SWITCH-DEAD`, `YIELD-REFUSE`, `SWITCH-REFUSE`, `EXIT-REFUSE`)
and the `C` report are failure-path (kept).

**GDB over `-s` has no types** (`kernel.elf` is built WITHOUT
`-g`).  Use raw offsets: `pid` 0x000, `name` 0x008, `state`
0x028, `cr3` 0x030, `entry_point` 0x038,
`kernel_stack_virt` 0x048, `kernel_stack_top` 0x050, `rsp`
0x110, `rip` 0x118, `next` 0x120, `prev` 0x128, `block_kind`
0x158.  `sizeof(pcb_t)` = 0x618.  **CONSIDER BUILDING WITH
`-g`.**

**Do not run `git reset --hard` without stashing first.  Do not
edit `third_party/`.  Do not commit `kmain.c`'s `pit_init` knob
-- the committed value is 100.  Do not call a race closed on one
clean run.**

---

## Releases

**Before a release:** the scratch `2026MMDD-*` tags are annotated
and are the source for the version summary.  **Write the summary
from them BEFORE dropping them.**  The release tag and the merge
commit are local objects; once they exist, the scratch tags may
be dropped.

**The release procedure** (on `dev`, pre-release):

1. **Patch the docs first.**  Update this file's release
   paragraph and (optionally) `docs/version-history.md`.  Commit.
2. **Bump the version string in `kmain.c` manually** (the
   banner).  Commit.
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

**Current release state.**  **`v0.6.17` is released.**  Work is
now pre-**`v0.6.18`**.  The release tags `v0.6.15`, `v0.6.16`,
and `v0.6.17` carry their summaries; the scratch tags they were
drawn from have been dropped.

---

## What donix is

A fork of dons-os.  Goal: a Unix-like OS that runs static
musl-linked binaries on the Linux x86_64 ABI, natively.  Newlib
is gone.  Userland is a tracked source tree at `userland/musl/`.
Phase B (busybox) is well underway: the shell is interactive,
pipelines and redirection work, and the applet set now includes
`bc`, `dc`, `diff`, `du`, `awk` in addition to the file/text
applets.

Full strategy: `docs/strategy.md`.  Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`.

---

## Where we are

### Current baseline

- `test.sh` 57/57; canary `--full` 28/28; pipe7e clean at 100 kHz
  and on a >3.5-hour 100 Hz soak.  The 2026-10-23 signal gate ran
  28/28 + 57/57 three times interleaved.
- Busybox batch 1 (prompt, readlink, `ls` features,
  human-readable, long options) and five applets (`bc`, `dc`,
  `diff`, `du`, `awk`) enabled and verified by running them.
- Signals slices 1-5 complete and verified (see "Signals: what is
  in" above).  `tkill` (200) in; `CONFIG_KILL=y`;
  `SIG_IGN` fixed and proven.
- `docs/contract.md` is the durable concurrency rule; read it
  before any shared-state change.

### Next -- the highest-value work

1. **SIGCHLD interrupting a blocked `wait4`.**  A program that
  blocks in `read`/`pause` and expects the handler to run does
  not get it.  This is a scheduler change (a signal-arrival wake
  for a blocked syscall), not a delivery change.
2. **`sigsuspend`/`pause`/`sigwait`.**  Atomic unblock-and-wait.
3. **Job control** (open-issues item 13): SIGSTOP/SIGTSTP stop,
  SIGCONT resume, process groups, controlling terminal,
  `tcsetpgrp`.  Needs (1) and (2).
4. **Shebang + `/bin/sh` kernel half** (item 14): `#!` parsing in
  `sys_execve` with a depth guard.  Image half is done.
5. **SSE** (item 10): save/restore XMM on context switch and the
  interrupt path, then remove the userland `-mno-sse*` flags.
6. **Privilege model** (item 9).
7. busybox batch 2; `test.sh`'s md5.

### Refuted -- do NOT re-walk

- **The reserve / frame placement.**  `67701df` + `5587d70`.
- **The `next->rsp = 0` clear.**  Broke blocking; reverted.
- **The pipe-waiter fields as the `C`'s stale holder.**  The
  pipe7e helper writes 2 bytes and never blocks, so they are
  never set.
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

1. **Signals: three gaps remain** -- SIGCHLD does not interrupt a
  blocked `wait4`; no `sigsuspend`/`pause`; SIGSTOP/SIGTSTP
  default-terminate.  See "Signals: what remains" in this file.
  Delivery, dispositions (including `SIG_IGN`), the default-action
  table, SIGPIPE, SIGCHLD-to-`wait4`, and `tkill` are done and
  verified.
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

`v0.6.15`, `v0.6.16`, and `v0.6.17` are the release record.  The
scratch tags listed above are the current restore points; there
is no single `20261021-slice5-done` tag as a release, and nothing
is pushed until `v0.6.18` is complete and verified.  The next
release procedure draws the release summary from these tags
before dropping them.

---

## One-line summary

**`v0.6.17` is the last release (VFS, FAT `st_ino`, `tar`,
`chmod`/`chown`).  Since then, on `dev` (37 commits, nothing
pushed): `ps -l` via `sys_sysinfo` (99) and a real
`creation_time`; the kernel shell's echo goes to serial
(`b06787d`); and **signals slices 1-5 are DONE** -- slice 1
`588ae53`/`3f60a19`/`ce69a7b`; slice 2 `866588f`/`d2126d8`;
slice 3 `17ffeb9`/`a1c90ff`/`c4b3ba7`; slice 4
`1d256ed`/`f90d517`; slice 5 `7245390` plus the SIGCHLD half
`593b349`/`dfd5240`/`42302e7`; `tkill` `b976b40`; busybox
`CONFIG_KILL` `33c4384`; `SIG_IGN` fix `6de2d63`.  Gate:
`test.sh` 57/57, `canary --full` 28/28, three times interleaved.
Default actions verified by exit status: SIGKILL 137, SIGTERM
143, SIGINT 130; SIGPIPE by `yes | head`; SIG_IGN by
`sigterm_ignore` status 0.  Restore point: `20261023-sigign`.
Next: the three signal gaps (SIGCHLD-interrupts-wait,
`sigsuspend`, job control), then shebang+/bin/sh, SSE,
privilege.  Read `docs/contract.md` and "NEXT" in this file
before any code.**
