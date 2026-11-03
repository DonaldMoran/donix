# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-03, documentation pass for `v0.6.19`.
**`v0.6.18` is the last pushed release; nothing was pushed.** `dev` =
`d69c2ab` plus the uncommitted docs edits (ahead of `origin/dev` by
100 before the edits). `origin/dev` = `799e98f` = `v0.6.18`. No
stashes. Build green. `canary --full` 28/28 and `test.sh` 57/57 at
`d69c2ab` (clean rebuild, no probes). Two scratch tags from the
2026-11-03 tar session: `20261103-tar-dirflush` (`a0664a8`) and
`20261103-tar-padding` (`d69c2ab`).

**WHERE WE ARE.** The 2026-11-03 session fixed **busybox `tar`**.
It was two bugs, both in vendored FatFs, both the same class: **state
staged in `fs->win` is not flushed before something else moves the
window.** `move_window()` is a no-op when `fs->winsect` already
equals the requested sector even if `fs->win` holds something else.

1. **`a0664a8` (`20261103-tar-dirflush`)** -- `f_open`: `dir_register`
   stages the new directory entry into `fs->win` via `dj.dir` but
   does not flush it. A later `f_open`/`f_read`/`f_write` on the same
   volume calls `move_window`, reloads `fs->win`, and the staged name
   is discarded; the close-time `sync_window` then writes the **old**
   sector back over the entry. Fix: `sync_window(fs)` right after
   `dir_register` succeeds in `f_open`, while `dj.dir` is valid.
2. **`d69c2ab` (`20261103-tar-padding`)** -- `f_write`: the
   cluster-boundary code above the **direct-write branch** sets the
   new FAT link in `fs->win` (`wflag=1`), but the direct branch
   writes data sectors from the caller's buffer and `continue`s
   without flushing. A later `create_chain`'s `move_window` is a
   no-op if `winsect` already equals the FAT sector, the chain read
   is stale, `create_chain` returns 1, `ABORT(FR_INT_ERR)` sets
   `FIL.err=2` -- **sticky** -- and every later `f_write` fails. That
   made tar's end-of-archive padding write return short:
   `write error: I/O error` on create, `short read` on list/extract.
   Fix: `sync_window(fs)` before the direct `disk_write`.

Both verified on a clean rebuild: `canary --full` 28/28,
`test.sh` 57/57, full `tar -cvf` / `-tvf` / `-xvf` round trip
**silent with `rc=0`** and matching contents (3072-byte archive for
two small members; 2048 for one), and the six-line redirect/tar
matrix green. **Directory members work too:** `tar -cvf dir.tar
a.txt b.txt src` (with `src/c.txt`) creates a 4608-byte archive
listing `src/` as `drwxr-xr-x`, and `tar -xvf` recreates the tree
with matching contents and `rc=0`. One cosmetic warning remains on
extract -- see NEXT item 4.

The prior session (2026-11-02) closed the `exec_churn`
performance issue (ATA PIO data port, `rep insw`/`rep outsw`) and
the `sys_fork` eager-copy issue (byte loop -> `rep movsb`); see
the 2026-11-02 commits list under "State on disk".

**NEXT -- highest-value work.**
1. **`docs/open-issues.md` item 10 -- SSE save/restore.**
   `CR4.OSFXSR` is set but the kernel saves no SSE state;
   userland is pinned to `-mno-sse*` as a stopgap. The fix is
   `fxsave`/`fxrstor`/`xsave` in `context_switch.asm` and the
   interrupt path, then remove the `-mno-sse*` flags from the
   userland build. One commit, one test. The risky part is
   alignment of the save region and any latent SSE use in the
   kernel once `-mno-sse` is dropped from kernel CFLAGS (do not
   drop the kernel's flags in the same commit as the userland
   flags -- kernel SSE and userland SSE are two separate
   decisions).
2. **`docs/open-issues.md` item 9 -- the privilege model.**
   uid/gid are 0 for every process; no per-process uid/euid
   split, no setuid, no `chown`. The item lists the shape:
   `setuid`/`seteuid` (105/117), a setuid-root marker on a
   FAT-hostile filesystem (magic-marker, same shape as item 8),
   a `sudoers` equivalent, then 102/104/107/108/115 return 1000
   with root as an escalation.

   **NOTE FOR THE NEXT SESSION BEFORE STARTING ITEM 2.** Item 9
   is the second-biggest numbered item, not a thing donix needs
   right now. There is no current user-facing feature it
   unlocks: every binary on the image is built here, there is no
   network service, no multi-user login, and no applet in
   `test.sh` that behaves differently as non-root. The cost is
   real -- every filesystem syscall grows a permission check,
   the five identity syscalls flip from 0 to 1000 together (and
   `tar -x`/`ps`/`whoami` output changes with them), `chmod`/
   `chown` become real (which means FAT needs a mode store or a
   side-channel), and the setuid marker is security-relevant in
   a way item 8's symlink marker is not. **Before starting item
   9, ask what it unlocks that will be used soon.** If the
   answer is "nothing in the near term," pick one of the
   visible-payoff items instead: real FatFs timestamps,
   `gettimeofday` (96) / `time` (201), or SSE (item 10). Item 9
   is a multi-session project and is correctly deferred until
   there is a reason for it.
3. **`umask` is unimplemented (syscall 95).** `mkdir -p` fails
   with `Unknown syscall: 95` and then mis-creates; `mkdir`
   without `-p` works. Small, self-contained, and it unblocks
   `mkdir -p` for scripts. Candidate for a short session.
4. **`mkdir` on an existing directory returns `EPERM` instead of
   `EEXIST`.** Symptom: `tar -xvf` of an archive with a directory
   member prints `tar: can't create directory 'src': Operation
   not permitted`, even though the directory is created and the
   tree extracts correctly (`rc=0`, contents match). Cause:
   busybox `tar` does `mkdir`, then treats `EEXIST` as "already
   there, fine" but warns on any other errno; the kernel's
   `sys_mkdir` returns `-EPERM` for an existing directory.
   Correct POSIX: `-EEXIST` when the target exists and is a
   directory, `-ENOTDIR` when it exists as a file. Small; read
   `sys_mkdir` before patching (not read this session). Pair it
   with `umask` -- both are small syscall-completion fixes, both
   unblock script-level behavior, neither touches FatFs.

**THE C HALT -- OPEN, NOT THE CURRENT FOCUS.** The C is
`context_switch_resume`'s unknown-CS branch. Not observed since the
2026-11-02 disk-path fixes (`905bc70` ATA rep-pio, `a0664a8` /
`d69c2ab` FatFs window flush). Three clean `canary --full` runs
before that (`873be27`, `6df4fef`, `78216bc`), and none since. That
is evidence, not a close -- no mechanism was ever named, and the
fault is intermittent. No C work is planned. The refuted paths below
are still refuted.

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
Never paste a multi-line block with trailing backslashes into an
interactive shell. **`cd "$HOME/..."` or a literal path in the
script, never `cd "$(dirname "$0")"` when the script is pasted --
`$0` is the interactive shell, and the `cd` fails.** A `set -e`
at the top of the *script* is fine; a `set -e` pasted into the
interactive shell is a terminal-killer.

**Never put a destructive git command inside a block that also
runs diagnostics.** A `git checkout -- <file>` or `git restore`
in a block whose other lines are `git status`/`git log`/`grep` is
a trap. If a revert is wanted, name it on its own line, say what
it discards, and run it as its own paste.

**Python edits with `count != 1` abort checks are the preferred
edit style.** **`set -e` at the top.** **Idempotent edit scripts.**
**Do the count check IN PYTHON (`str.count`), not with `grep`.**
`grep -Fc` does not do multi-line matches; `grep -Fxc` requires a
whole-line match. `str.count` is the only honest multi-line count.

**BEFORE ASSERTING A COUNT, count the occurrences in the file and
write the expected number from that count, not from memory.**

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION.** Grep the
file, confirm the count is 1 **and** the match is in the intended
function. Anchor from the file's current bytes (`cat -A`), open
`rb`/`wb` to preserve line endings. If an anchor aborts, **ask for
the current bytes** -- do not guess a second anchor.

**A post-write check string must be copied from the `new`
literal, not retyped.**

**Ask for source you do not have.** Before patching a file it has
not seen **in this session**, ask for it. **Before writing an
anchor, read the file with `cat -A` and quote the bytes.**

**The build is TWO steps.** `make -C 04_kernel_64bit
FAT_CONFIG=single` builds the kernel; `make -C 05_boot_kernel64
hdd-single.img` builds the image. Running only the second copies
a STALE `kernel.bin`. Always run both, and verify the mtime chain
`source` < `.o` < `kernel.elf` < `kernel.bin` < `hdd-single.img`.
**After a session of probe edits and reverts, run a full
`make clean` before the next behavioral test** -- per-file `rm`
is not enough when multiple files were patched.

**The patch-script write changes the mode of the file it writes.**
A Python `open(..., "wb")` or `os.replace` drops the `+x` bit
that the file had. `78216bc`'s follow-up `ea4dc2e` restored
`user_syscall.c` from 100644 to 100755 for exactly this reason.
If a patched file was executable, `chmod +x` it back, or commit
the mode change as its own explicit commit -- do not leave a
mode change hidden inside a content commit.

**`nm` output moves between builds.** Re-run `nm` before using
any address.

**A diagnostic on the SUCCESS path is noise. A diagnostic on
the FAILURE path is signal.** `SW2-*`, `HEAP_TRACE_MERGE`, the
`HEAP: corrupt` prints, and `heap_validate`'s check prints are
success-or-transient-path; they are gated. The refuse lines
(`SWITCH-DEAD`, `YIELD-REFUSE`, `SWITCH-REFUSE`, `EXIT-REFUSE`)
and the `C` report are failure-path (kept).

**Test harness.** `./run` (no arg) = clean rebuild + fresh
`hdd-single.img` + `-enable-kvm -cpu host -display sdl`, no freeze,
gdb stub on `:1234`, `capture.txt` written (overwritten each run --
**copy it aside after any failure or pass you want to keep**).
`./run debug` adds `-S` (frozen at reset). **`./run tcg` /
`./run tcg-debug`** = the same flow in TCG (`-cpu max`, no
`-enable-kvm`). The Makefile run targets also use `-cpu max` + TCG
-- **state which launcher was used.** Default cwd `/root` (prompt
`~`).

**In-guest gate.** `cd /`, then `canary --full` (28/28) and
`/root/scripts/test.sh` (57/57). **Use multiple `^Z`/`fg` cycles.**
One is not always enough to reach an intermittent fault; several
reliably do.

**`perf kvm stat` is the way to count VM-Exits.** `sudo dnf
install -y perf` on the Fedora host. Two terminals: guest runs
the test; host runs `sudo perf kvm stat record -a sleep 15`, then
`sudo perf kvm stat report` (`q` to quit the pager), then
`rm -f perf.data.guest`. The report names `io`, `npf`,
`interrupt`, `vintr`, `idle-halt`. **An `io`-dominated report
does not mean "port I/O is slow"; it means "the guest is doing
port I/O on a hot path."** The 2026-11-02 session spent hours
chasing that to the console before finding the ATA data port.
Read the guest source before reading the exit reason.

**On Fedora 44, `perf kvm stat record` aborts with `double free
or corruption (!prev)` in cleanup, after writing `perf.data.guest`.**
The capture is complete and `perf kvm stat report` on it works.
The abort is in `kvm_events_record()` (it `strdup`s `rec_argv`
entries and frees them after `parse_options()` reorders `argv`),
not in the guest and not in the kernel. Do not chase it as a
donix issue. If `report` also aborts on the same host, use
`sudo perf kvm stat live` as the workaround. The `perf.data*`
files are now in `.gitignore` (`e52bd2f`).

**GDB over `-s` has no types** (`kernel.elf` is built WITHOUT
`-g`). Use raw offsets: `pid` 0x000, `name` 0x008, `state` 0x028,
`cr3` 0x030, `entry_point` 0x038, `kernel_stack_virt` 0x048,
`kernel_stack_top` 0x050, `rsp` 0x110, `rip` 0x118, `next` 0x120,
`prev` 0x128, `block_kind` 0x158. `sizeof(pcb_t)` = 0x618.
**CONSIDER BUILDING WITH `-g`.**

**gdb write watchpoint is safe for the canary `.text` `#PF`.** The
"do not use gdb" applies to timing-sensitive repro (gdb-at-fault
masks the race). A **write watchpoint** on the physical or virtual
PTE does not mask it.

**Do not** `git reset --hard` without stashing; **do not** edit
`third_party/`; **do not** commit `kmain.c`'s `pit_init` knob -- the
committed value is 100; **do not** call a race closed on one clean
run.

**Do not** assume a commit fixed something because its message says
it did. `f91d31a` claims to fix canary/pipe7e/ctrl-Z; it does not.
`7cb26fb` claims to fix `^C`/`^Z`/nesting and DOES -- and it also
introduces the pipe7e `#GP` (`0196d66`, `162a1d3` fixed that).
**Test every commit before trusting its message -- and note that one
commit can be both a fix and a regression at once.**

**A handoff claim is not evidence.** The old handoff said `d1ae213` was
green; it was not. **Re-run every repro on a fresh boot and read the
bytes before acting.**

---

## Releases

**Before a release:** the scratch `2026MMDD-*` tags are annotated
and are the source for the version summary. **The scratch tags
are PRESERVED until the user decides to push to GitHub, and
are dropped ONLY after the summary is written and reviewed by
the user.** A session does NOT drop them on its own initiative.

**The release procedure** (on `dev`, pre-release):

1. **Patch the docs first.** Update this file's release paragraph
   and (optionally) `docs/version-history.md`. Commit.
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

**The GitHub push policy.** **Nothing is pushed until `vX.Y.Z` is
ready and verified.** `origin/dev` is not force-pushed; every
unpushed `dev` commit is intentional work. Run
`git log --oneline origin/dev..dev | wc -l` for the current count.

**Current release state.** **`v0.6.18` is the last pushed release.**
This session is the `v0.6.19` documentation pass; the banner bump
and tag happen only when the user says so, and **nothing is pushed
until the user says so.** The scratch tags are preserved. Do not
push, tag a release, or drop the scratch tags on the session's own
initiative.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static
musl-linked binaries on the Linux x86_64 ABI, natively. Newlib is
gone. Userland is a tracked source tree at `userland/musl/`. Phase B
(busybox) is well underway: the shell is interactive, pipelines and
redirection work, job control works (`^C`, `^Z`, `jobs`, `fg`), and
the applet set now includes `bc`, `dc`, `diff`, `du`, `awk` in
addition to the file/text applets. **`tar` now creates, lists and
extracts correct, well-formed archives, including directory
members** (`a0664a8` + `d69c2ab`).

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`. Open issues:
`docs/open-issues.md`.

---

## Where we are

### Current baseline

- `test.sh` **57/57** at `78216bc` (2026-11-02 very late, gate for
  the fork commit).
- `canary --full` **28/28** at `78216bc` (same run). The full 28-row
  sequence, no skips.
- `exec_churn` 24 rounds pass at `78216bc`; every round reports
  `fork 0` (tick-quantized). See the Refuted section -- this is
  not a measurement of the fork copy.
- `^Z` on a foreground child -> `[1]+ Stopped`; `jobs` lists it;
  `fg` resumes.
- `^C` -> SIGINT reaches the foreground pgrp; the console survives
  and the prompt returns.
- `busybox sh` nesting -> works.
- The C is not observed since the 2026-11-02 disk-path fixes; still
  open -- evidence, not a close. See "THE C HALT" above.

### Next -- the highest-value work

1. `docs/open-issues.md` item 10 -- SSE save/restore.
2. `docs/open-issues.md` item 9 -- the privilege model, **with the
   note above**: no current user-facing payoff; ask what it unlocks
   before starting it. Real FatFs timestamps, `gettimeofday`/
   `time`, or SSE may be a better use of the next session.

### Refuted -- do NOT re-walk

These were discarded in earlier sessions. They are listed so a
future session does not spend the same cycles. If any turns out
to be a different root cause, that is a **new** hypothesis, not a
revival of these.

- **The `efc8e08` rsp/irq0_rsp split, and `frame_kind`.** Discarded
  with the 2026-11-02 reset; not on `dev`.
- **The four uncommitted scheduler guards** (`process_yield`
  FRAME_IRQ0 skip, `scheduler_ready_queue_add` non-PCB guard,
  `scheduler_switch_to` non-PCB guard, `scheduler_switch_to`
  frame-CS validate). Discarded with the reset and the stash drop.
- **The `EXIT-REFUSE frame-CS` guard in `process_exit`.** Discarded
  with the reset and the stash drop.
- **The canary `.text` `#PF` probe stack** (`[PT-CANARY]`,
  `[PT-WP]`, `[PHDR]`/`[EHDR]`, `[UNMAP_IN_CR3]`, `[FREE_CLONE]`,
  `[PCLAIM]`). Dropped with `stash@{1}`.
- **The physical-address gates on `.text` PTE zeroing.** Boot
  dependent; do not reapply without new evidence.
- **The `f91d31a` "scheduler guards fix canary/pipe7e/ctrl-Z"
  claim.** Disproved by running.
- **`exec_churn`'s KVM slowness is the console / the serial poll
  loop / the VGA cursor CRTC writes.** Refuted 2026-11-02: the
  serial `inb` poll was commented out and `prnt` was unchanged;
  `console_timer` measured the console at microseconds in the
  same boot that ran `exec_churn` at 4 s; the VGA cursor patch
  (later committed as `20261102-console-cursor-defer`) also did
  not move `prnt`. The cost was the ATA data port in `ata.c`,
  fixed by `rep insw`/`rep outsw`.
- **`ata_poll_drq` spinning.** Instrumented 2026-11-02:
  2,113 calls, 128 total spins. Not the cost.
- **`exec_churn` can measure the `sys_fork` eager-copy cost.**
  Refuted 2026-11-02, same session as the fork `rep movsb` commit
  (`78216bc`). `exec_churn`'s floor is the 10 ms PIT tick; every
  round reports `fork 0` **before and after** the fix. The byte
  loop was ~1-3 ms per fork (below one tick), and so is the
  `rep movsb` -- the two are indistinguishable to `exec_churn`.
  The round totals vary between boots (`360,000 us` in the
  handoff's baseline vs `440,000 us` at `78216bc`) because they
  are dominated by how many rounds happened to land on 1 vs 2
  ticks, not by the fork copy. **A sub-tick fork benchmark is
  what the next session needs if this is ever to be measured.**
  Do not re-walk `exec_churn` as evidence for or against a fork
  change.
- **The `perf kvm stat record` `double free` is a donix issue.**
  Refuted 2026-11-02. It is a Fedora 44 `perf` bug in
  `kvm_events_record()`'s cleanup (see "Test harness" above). The
  capture writes and the report reads correctly. Do not chase it
  in the kernel.

### Open issues

The full list is `docs/open-issues.md`. The numbered items are 9
(no privilege model) and 10 (no SSE save/restore). The
un-numbered items (7a, 7b, 7e, the `0x413962` `#PF`, the
`.bss`-adjacent faults, and the small VFS/proc/pipe entries) are
real and stay in that file; they are not in this session's NEXT.
Read `docs/open-issues.md` before touching the VFS, fork, exec,
or the signal paths.

---

## State on disk

Kernel sources: `04_kernel_64bit/`. `capture.txt` is gitignored,
overwritten each `./run` -- **copy it aside after any failure or
pass you want to keep.**

**After a fresh image, one manual step:** the image stages
`/bin/busybox` alone. To get the applet symlinks (including
`/bin/sh`, which shebang needs), boot and run
`/bin/busybox --install -s /bin`. The absolute path is required:
busybox reads `/proc/self/exe` to find itself, donix has no
`/proc/self/exe`, and it falls back to `argv[0]` -- which must
start with `/`. This is a manual step, not part of the build.

**`third_party/`** is **gitignored** -- do not commit edits to it.
`third_party/busybox/` has `busybox` (stripped) and
`busybox_unstripped` + `.map` if a userspace symbol is ever needed.
**`third_party/musl-install/` is gitignored and is needed for the
build.** If it's missing after a `git clean -xdf`, copy it back from
a working tree, or rebuild it with `toolchain/install_musl.sh`.
`04_kernel_64bit/fatfs/ff.c` is **tracked** (not `third_party/`),
built by the cross-GCC, and now contains **two** `sync_window` fixes
(`a0664a8` in `f_open`, `d69c2ab` in `f_write`). Any further edit
there must re-run the full gate (`canary --full` + `test.sh` + the
tar round trip + the redirect/tar matrix), not just a build.

**`~/code/saved`** -- not a baseline. Run
`git -C ~/code/saved status --short` before any comparison; treat
"not a baseline" as the default. It has been dirty twice.

**The reset baseline.** `dev` = `873be27`, tag
`reset-baseline-873be27`. No stashes exist. `efc8e08` is not on
`dev`; it survives as a commit object. If a future session needs
the discarded guards, they are not in the tree -- read
`git log --all --oneline` for `efc8e08` and the session log, but
treat them as **refuted** per the section above.

**The 2026-11-02 performance and correctness commits** (six, on
top of `f902af6`):
- `78216bc` `20261102-fork-rep-movsb` -- fork `EAGER_COPY_REGION`
  `rep movsb`. **The item 1 commit.**
- `ea4dc2e` -- `user_syscall.c` mode restore (100644 -> 100755,
  dropped by `78216bc`'s Python write).
- `e52bd2f` -- gitignore `perf.data*`.
- `905bc70` `20261102-ata-rep-pio` -- the ATA fix.
- `fdbcbc7` `20261102-console-cursor-defer` -- independent
  improvement.
- `5db42f2` `20261102-exec-churn-ns-timing` -- test-only.
- `6df4fef` `20261102-console-timer` -- test-only.

(`f902af6` is `20261102-handoff-rewrite`; the six above are the
work on top of it.)

---

## One-line summary

**`v0.6.18` is the last pushed release; nothing is pushed.** The
tree is at `d69c2ab` plus the uncommitted docs edits, no stashes,
ahead of `origin/dev` by 100 before the edits. The **busybox `tar`
bug is fixed in two parts**: `a0664a8` (`f_open` flushes the
staged directory entry after `dir_register`) and `d69c2ab`
(`f_write` flushes the FAT window before a direct data write).
Both are stale-`fs->win` bugs. `canary --full` 28/28, `test.sh`
57/57, a full `tar` create/list/extract round trip (regular files
**and directory members**) silent with `rc=0` and matching
contents, and the six-line redirect/tar matrix are green on a
clean rebuild with the probes reverted. **This session is the
`v0.6.19` documentation pass; the bump and tag happen when the
user says so, and nothing is pushed until the user says so.**
NEXT, if the user wants OS work, is item 10 (SSE), item 9
(privilege model, with the "is this worth a session" note), or the
two small syscall-completion fixes (`umask` 95, `mkdir`/`EEXIST`);
otherwise treat any user-reported bug as a new task. Read
`docs/contract.md` and this file's NEXT before any code.
