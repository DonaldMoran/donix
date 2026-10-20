# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-20.  **THE VFS IS DONE.  `v0.6.16` is the
VFS release.**  Every path syscall reaches its filesystem through the
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
`sys_gettimeofday` (99); signals (contract's async-writer design);
shebang + `/bin/sh`.  `tar` is **disabled** in busybox: its creation
path writes an empty archive and exits 0, so it was turned off rather
than shipped broken.

**THE SCRATCH TAGS** are development restore points, not release
artifacts: `20261020-proc-path-fold`, `20261020-proc-entries`,
`20261020-proc-realpath`, `20261020-vfs-mount-table`,
`20261020-vfs-dev-dir`, `20261020-vfs-dev-driver`,
`20261020-vfs-dev-zero-urandom`.  Do not drop them until the `v0.6.16`
tag and merge summary exist.

**WHERE WE ARE.**  `v0.6.15` is released.  `v0.6.16` (this VFS release)
is tagged and merging.  The working tree is `dev`.

**NEXT.**  Signals (open-issues 12; design against `docs/contract.md`
first -- they are the first asynchronous state writer), then
`sys_gettimeofday` (99, so `ps -l` works), then shebang + `/bin/sh`,
then file-backed `mmap`, then `mount(2)`.

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

**Current release state.**  **`v0.6.15` is released; `v0.6.16` (the VFS) is tagged and merging.**  Work is now
pre-**`v0.6.15`**.  The scratch tags `20261019-*` (7b investigation,
busybox batch 1, the five applets, README) are the source for the
`v0.6.15` summary; the bump is the maintainer's decision.  Do not
delete them until the `v0.6.15` tag and merge summary exist.

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

1. **The VFS.**  The pathname-dispatch seam exists (`/proc` is a real
   directory, `/dev` is not); the VFS makes the seam a real layer:
   a mount table, inode operations, FatFs as one driver, and one
   `vfs_resolve(path, follow_final)` that every path-taking syscall
   calls.  It unblocks `/dev` as a directory, symlinks (item 8),
   file-backed `mmap` (a Wayland prerequisite), and the display
   stack.  Read `docs/contract.md`'s VFS section first.  Scope it as
   small, verified commits: mount table, FatFs driver, one syscall
   through `vfs_resolve`, then `/dev`, then `/proc`, then symlinks.
2. **Signals (open-issues item 12).**  The first asynchronous state
   writer; design against `docs/contract.md` before code.
3. **Shebang + `/bin/sh`.**  `sys_execve` `#!` parsing with a
   recursion guard, and an interpreter on the image.
4. **`sys_gettimeofday` (99)** so `ps -l` works.
5. The open-issues list (top 6, below).

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

1. **`open-issues.md` item 12: signal delivery is a stub.**
2. **`open-issues.md` item 10: `CR4.OSFXSR` without an XMM save.**
3. **`open-issues.md` item 13: `CONFIG_ASH_JOB_CONTROL` gap.**
4. **`sys_gettimeofday` (99)** so `ps -l` works.
5. **`process_dump_all`'s `DETACHED` state** -- may mask a stranded
   process bug.
6. **Item 7a/7b** -- the boot-time `#PF` and the busybox `#GP`.

Plus: **the VFS** (see Next), **shebang + `/bin/sh`**, and the
open-issues list.

---

## State on disk

Kernel sources: `04_kernel_64bit/`.  `capture.txt` is gitignored.
Scratch tags for the `v0.6.15` summary (drop only after the tag and
merge summary exist): `20261019-7b-investigated`,
`20261019-busybox-batch1`, `20261019-busybox-applets`,
`20261019-readme-current`.

---

## One-line summary

**The `C` is FIXED, the heap cycle hang is CONTAINED, and busybox
batch 1 + five applets are in (57/57; a >3.5-hour 100 Hz soak is
clean).  `docs/contract.md` is the durable concurrency rule.  Next:
the VFS -- read the contract's VFS section, then scope it as small,
verified commits.**
