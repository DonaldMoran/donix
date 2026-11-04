# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-04, at `e5081e1`.
**`v0.6.19` is the last pushed release.** `dev` = `e5081e1`;
`origin/dev` = `ba56b3d`. Ahead **8**, clean tree, one stash
(`stash@{0}`, "vfork 3b ... not a complete fix"). No push, no tag this
session; banner still `v0.6.19`. `main` = `7e4748b` (the `--no-ff` merge
of `v0.6.19`). Scratch tag `20261104-gettimeofday-time` is on `5a3b65e`.

**WHERE WE ARE.** The 2026-11-04 session did four things and stopped at a
fifth.

1. **`gettimeofday` (96) and `time` (201)** -- implemented, gated, tagged
   `20261104-gettimeofday-time` (`5a3b65e`). Both read the existing 100 Hz
   `g_ticks`; `tv_usec` is `(g_ticks%100)*10000` (10 ms, the PIT
   resolution, stated in the comment). `mktemp` (musl `__randname`'s
   `gettimeofday` fallback) works; `date` and `uptime` work after the
   busybox config commit (`dca2eff` enabled `CONFIG_DATE`,
   `CONFIG_UPTIME`, `CONFIG_TIME`).
2. **`borrows_cr3` guard** (`ea0986e`) -- a `pcb_t` field appended after
   `shebang_depth`; `process_cleanup_elf_pages` and the `process_reclaim`
   / `process_destroy` clone-free sites skip their frees when set. Inert
   until vfork sets it.
3. **`vmm_create_address_space`** (`6ca941b` + `c652c8f`) -- **fixes the
   triple fault**. The first version built a fresh PML4 with an empty low
   half and dropped the 2 MB identity map `vmm_init` installs at PML4
   index 0; the machine triple-faulted and reset on the first `time true`.
   The fix is **clone-then-unmap**: `vmm_clone_page_table(src_cr3)` keeps
   every kernel mapping, then the four user ranges have their PTEs zeroed
   in the new cr3. The parent's pages are not freed.
4. **`sys_vfork` (58) block** (`1aaca0d`) -- real shared-mm: the child
   gets `parent->cr3`, `borrows_cr3 = 1`, an empty `elf_page_list`, no
   eager copy; the parent blocks on `BLOCK_KIND_VFORK`. The tick does not
   wake it (`process_wake_all_blocked` skips any `block_kind != NONE`);
   the child's exec/exit is the only waker. **The wake half was never
   committed** -- it is in `stash@{0}`.

5. **NOT DONE: `time true` is unreliable.** The vfork child intermittently
   faults in musl/busybox startup. Sometimes it succeeds
   (`real 0m 0.18s`); often it faults at a **real busybox RIP** with a
   **callee-saved register holding 0**:
   `0x4512E8 mov %r13,0x8(%r12)` with `%r12 = 0`; `0x422300
   testb $0x1,0x21(%rbx)` with `%rbx = 0xCE`. Leading hypothesis: a
   callee-saved register is intermittently wrong on the syscall-return
   path (C-halt / pipe7e family). **Full record: `docs/open-issues.md`,
   heading "vfork child faults in musl startup", line ~412.** Read it
   before touching vfork.

**Nine kernel values along the exec path were measured correct** and are
listed in that open-issues entry (cr3 clone, exec frame slots, sysret
target, argv stack, `.data`/relocations, ELF entry, kernel-stack slot
allocator, brk unmap, `vmm_map_page_in_cr3`/`vmm_unmap_page_in_cr3`
bodies). The values are not the bug.

**STASHED, NOT COMMITTED:** `stash@{0}` holds the exec-side vfork work --
`process.c`'s `process_wake_parent_if_waiting` vfork arm (accepts
`BLOCK_KIND_VFORK` with `wait_pid == child->pid`), and `user_syscall.c`'s
`vmm_create_address_space` call + exec wake -- **plus** the session's
diagnostics (`DIAG-ENTRY`, `DIAG-DATAPROBE`, `DIAG-PTAUDIT`,
`DIAG-MAPAUDIT`, `DIAG-CLONEAUDIT`, the `EXPERIMENT-BRK` `#if 0` in
`vmm.c`). `git stash show -p` to recover; strip the `DIAG-*` and the
`#if 0` when doing so. **Do not commit the stash as-is.**

**NEXT -- highest-value work, in order.**

1. **The open vfork fault.** Read `docs/open-issues.md` line ~412.
   Recover `stash@{0}`, strip its diagnostics, and run the one probe the
   record proposes: in `user_syscall_entry`, immediately before
   `o64 sysret`, if `%r12` (or `%rbx`) is 0 or `< 0x1000`, print it and
   the syscall number. That is the first probe aimed at the actual
   faulting value (`%r12 = 0`) rather than at pages or frames. Then:
   `canary --full` 28/28, `test.sh` 57/57, and **`time true` several
   times** -- one success is not a fix.
2. **`umask` (syscall 95) unimplemented.** `mkdir -p` fails with
   `Unknown syscall: 95` and then mis-creates; `mkdir` without `-p`
   works. Small, self-contained, unblocks scripts. Never started.
3. **`mkdir` on an existing directory returns `EPERM` not `EEXIST`.**
   Symptom: `tar -xvf` of a directory-member archive prints `can't create
   directory ... Operation not permitted` even though the tree extracts
   (`rc=0`). POSIX: `-EEXIST` when the target is a directory,
   `-ENOTDIR` when it is a file. Read `sys_mkdir` before patching. Pair
   with item 2 -- both small syscall-completion fixes, neither touches
   FatFs.
4. **`docs/open-issues.md` item 10 -- SSE save/restore.**
   `CR4.OSFXSR` is set but the kernel saves no SSE state; userland is
   pinned to `-mno-sse*`. `fxsave`/`fxrstor` in `context_switch.asm` and
   the interrupt path, then drop `-mno-sse*` from the **userland** build
   (kernel flags are a separate decision, separate commit). Risky part:
   save-region alignment and latent kernel SSE once `-mno-sse` leaves
   userland CFLAGS.
5. **`docs/open-issues.md` item 9 -- the privilege model.** uid/gid are
   0 for every process. **Read the note in open-issues first:** there is
   no current user-facing feature it unlocks (every binary is built here,
   no network, no multi-user login), and the cost is real. **Before
   starting it, ask what it unlocks that will be used soon.** If
   "nothing," prefer the visible-payoff items: FatFs timestamps,
   `gettimeofday`/`time` follow-ons, or SSE.

**THE C HALT -- OPEN, NOT THIS SESSION'S FOCUS.** The C is
`context_switch_resume`'s unknown-CS branch. Not observed since the
2026-11-02 disk-path fixes. No mechanism was ever named; the fault is
intermittent. No C work is planned. The refuted paths below are still
refuted.

---

## Working style -- how this project gets changed

Read this before proposing any command block.

**One change at a time.** A "change" is a commit, verified by
**RUNNING** before the next starts -- build, run, see the behavior,
THEN commit. A build passing is not a test passing.

**The state check comes before the command block.** Every command block
starts with `git status -sb` (or `git log`).

**A command block is one paste -- write it to a file first.**
`git add`, `git status`, `git commit`, and `git tag` belong in **one**
block, with the message inline via heredoc. **Do not paste a block with
`cd ... || exit 1` into an interactive shell** -- the `exit` closes the
terminal. Write to `/tmp/<name>.sh` with `cat > ... <<'SCRIPT_EOF'`,
then run it as a **separate** paste. Never paste a multi-line block with
trailing backslashes into an interactive shell. **`cd "$HOME/..."` or a
literal path in the script, never `cd "$(dirname "$0")"` when the script
is pasted -- `$0` is the interactive shell, and the `cd` fails.** A
`set -e` at the top of the *script* is fine; a `set -e` pasted into the
interactive shell is a terminal-killer.

**A block that writes a file must actually be run as its own paste.**
The 2026-11-04 session lost several turns to "I described the script but
never wrote it" -- `gate_*.sh`, `append_vfork_issue.sh`,
`commit_vfork_record.sh` all had to be re-emitted. If a script is
needed, it is a `cat > /tmp/x.sh` paste, then a `/tmp/x.sh` paste. Two
steps, both explicit.

**Never put a destructive git command inside a block that also runs
diagnostics.** A `git checkout -- <file>` or `git restore` in a block
whose other lines are `git status`/`git log`/`grep` is a trap. If a
revert is wanted, name it on its own line, say what it discards, and run
it as its own paste.

**Python edits with `count != 1` abort checks are the preferred edit
style.** **`set -e` at the top.** **Idempotent edit scripts.**
**Do the count check IN PYTHON (`str.count`), not with `grep`.**
`grep -Fc` does not do multi-line matches; `grep -Fxc` requires a
whole-line match. `str.count` is the only honest multi-line count.

**BEFORE ASSERTING A COUNT, count the occurrences in the file and write
the expected number from that count, not from memory.**

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION -- AND ON A
STATEMENT BOUNDARY, NOT A FRAGMENT INSIDE A STRING LITERAL.** Grep the
file, confirm the count is 1 **and** the match is in the intended
function. Anchor from the file's current bytes (`cat -A`), open
`rb`/`wb` to preserve line endings. If an anchor aborts, **ask for the
current bytes** -- do not guess a second anchor. The 2026-11-04 session
broke `interrupts.c` by anchoring on `"  FAULTWALK rsp_phys=0x"`, which
matched *inside* a `serial_print("...")` literal and split the string;
and truncated `user_syscall_entry.asm` by stripping to `o64 sysret`
without keeping the label blocks that follow. Both were avoided on later
patches by anchoring on full statements (`serial_unlock();`) and by
appending rather than slicing.

**A post-write check string must be copied from the `new` literal, not
retyped.**

**Ask for source you do not have.** Before patching a file it has not
seen **in this session**, ask for it. **Before writing an anchor, read
the file with `cat -A` and quote the bytes.**

**The build is TWO steps.** `make -C 04_kernel_64bit
FAT_CONFIG=single` builds the kernel; `make -C 05_boot_kernel64
hdd-single.img` builds the image. Running only the second copies a
STALE `kernel.bin`. Always run both, and verify the mtime chain
`source` < `.o` < `kernel.elf` < `kernel.bin` < `hdd-single.img`.
**After a session of probe edits and reverts, run a full `make clean`
before the next behavioral test** -- per-file `rm` is not enough when
multiple files were patched. The 2026-11-04 session ran many probes
through `interrupts.c`, `vmm.c`, `user_syscall.c`; all were reverted or
stashed, but a clean rebuild before the next test is the rule.

**The patch-script write changes the mode of the file it writes.** A
Python `open(..., "wb")` or `os.replace` drops the `+x` bit that the
file had. The `78216bc` follow-up `ea4dc2e` restored `user_syscall.c`
from 100644 to 100755 for exactly this reason. If a patched file was
executable, `chmod +x` it back, or commit the mode change as its own
explicit commit -- do not leave a mode change hidden inside a content
commit.

**`nm` output moves between builds.** Re-run `nm` before using any
address.

**A diagnostic on the SUCCESS path is noise. A diagnostic on the
FAILURE path is signal.** `SW2-*`, `HEAP_TRACE_MERGE`, the `HEAP:
corrupt` prints, and `heap_validate`'s check prints are
success-or-transient-path; they are gated. The refuse lines
(`SWITCH-DEAD`, `YIELD-REFUSE`, `SWITCH-REFUSE`, `EXIT-REFUSE`) and the
`C` report are failure-path (kept).

**A probe that fires nothing is a result.** The 2026-11-04 session's
`DIAG-MAPAUDIT` never fired; read correctly, that meant "no caller passes
a bad phys." Instead the session spent several turns treating the
`PTAUDIT` "22 anomalies" as corruption. **`0x8000_..._07D6_xxxx` in a
donix PTE phys field is the NORMAL encoding** (the HHDM address is stored
there; `pte_phys()`/`phys_to_virt()` account for it). Do not re-chase it.
When a probe is silent and a sibling probe is loud, the pair is the
evidence.

**Test harness.** `./run` (no arg) = clean rebuild + fresh
`hdd-single.img` + `-enable-kvm -cpu host -display sdl`, no freeze, gdb
stub on `:1234`, `capture.txt` written (overwritten each run -- **copy
it aside after any failure or pass you want to keep**). `./run debug`
adds `-S` (frozen at reset). **`./run tcg` / `./run tcg-debug`** = the
same flow in TCG (`-cpu max`, no `-enable-kvm`). The Makefile run
targets also use `-cpu max` + TCG -- **state which launcher was used.**
Default cwd `/root` (prompt `~`).

**In-guest gate.** `cd /`, then `canary --full` (28/28) and
`/root/scripts/test.sh` (57/57). **Use multiple `^Z`/`fg` cycles.** One
is not always enough to reach an intermittent fault; several reliably
do.

**`perf kvm stat` is the way to count VM-Exits.** `sudo dnf install -y
perf` on the Fedora host. Two terminals: guest runs the test; host runs
`sudo perf kvm stat record -a sleep 15`, then `sudo perf kvm stat
report` (`q` to quit the pager), then `rm -f perf.data.guest`. The
report names `io`, `npf`, `interrupt`, `vintr`, `idle-halt`. **An
`io`-dominated report does not mean "port I/O is slow"; it means "the
guest is doing port I/O on a hot path."** Read the guest source before
reading the exit reason.

**On Fedora 44, `perf kvm stat record` aborts with `double free or
corruption (!prev)` in cleanup, after writing `perf.data.guest`.** The
capture is complete and `perf kvm stat report` on it works. The abort is
in `kvm_events_record()` (it `strdup`s `rec_argv` entries and frees them
after `parse_options()` reorders `argv`), not in the guest and not in
the kernel. Do not chase it as a donix issue. If `report` also aborts on
the same host, use `sudo perf kvm stat live` as the workaround. The
`perf.data*` files are in `.gitignore` (`e52bd2f`).

**GDB over `-s` has no types** (`kernel.elf` is built WITHOUT `-g`).
Use raw offsets: `pid` 0x000, `name` 0x008, `state` 0x028, `cr3` 0x030,
`entry_point` 0x038, `kernel_stack_virt` 0x048, `kernel_stack_top` 0x050,
`rsp` 0x110, `rip` 0x118, `next` 0x120, `prev` 0x128, `block_kind` 0x158.
`sizeof(pcb_t)` = 0x618. **CONSIDER BUILDING WITH `-g`.**

**gdb write watchpoint is safe for the canary `.text` `#PF`.** The "do
not use gdb" applies to timing-sensitive repro (gdb-at-fault masks the
race). A **write watchpoint** on the physical or virtual PTE does not
mask it.

**Do not** `git reset --hard` without stashing; **do not** edit
`third_party/`; **do not** commit `kmain.c`'s `pit_init` knob -- the
committed value is 100; **do not** call a race closed on one clean run.

**Do not** assume a commit fixed something because its message says it
did. `f91d31a` claims to fix canary/pipe7e/ctrl-Z; it does not.
`7cb26fb` claims to fix `^C`/`^Z`/nesting and DOES -- and it also
introduces the pipe7e `#GP` (`0196d66`, `162a1d3` fixed that). **Test
every commit before trusting its message -- and note that one commit can
be both a fix and a regression at once.**

**A handoff claim is not evidence.** The old handoff said `d1ae213` was
green; it was not. **Re-run every repro on a fresh boot and read the
bytes before acting.**

---

## Releases

**Before a release:** the scratch `2026MMDD-*` tags are annotated and are
the source for the version summary. **The scratch tags are PRESERVED
until the user decides to push to GitHub, and are dropped ONLY after the
summary is written and reviewed by the user.** A session does NOT drop
them on its own initiative.

**The release procedure** (on `dev`, pre-release):

1. **Patch the docs first.** Update this file's release paragraph and
   (optionally) `docs/version-history.md`. Commit.
2. **Bump the version string in `kmain.c` manually** (the banner).
   Commit.
3. **Tag** `vX.Y.Z` annotated, on `dev`, summary from the scratch tags'
   annotations and `vPREV..HEAD`:

       git tag -a vX.Y.Z -m "<version summary>"

4. **Drop the scratch tags locally** (the tag and summary now exist):

       for t in $(git tag --list '2026*'); do git tag -d "$t"; done

5. **Push `dev`, then the tag:**

       git push origin dev
       git push origin vX.Y.Z

6. **Merge to `main` with `--no-ff`** (keeps the merge point in `main`'s
   history), push, return to `dev`:

       git checkout main
       git merge --no-ff dev -m "<version summary>"
       git push origin main
       git checkout dev

Do not use `git push --tags` -- it would push the scratch tags. Push
`vX.Y.Z` by name.

**The GitHub push policy.** **Nothing is pushed until `vX.Y.Z` is ready
and verified.** `origin/dev` is not force-pushed; every unpushed `dev`
commit is intentional work. Run `git log --oneline origin/dev..dev | wc
-l` for the current count.

**Current release state.** **`v0.6.19` is the last pushed release.**
`dev` = `e5081e1`, ahead **8**, clean, one stash. `origin/dev` =
`ba56b3d`. Scratch tag `20261104-gettimeofday-time` on `5a3b65e`. Work
is now pre-`v0.6.20`. **There is no release gate recorded for
`v0.6.20`; if a gate is wanted it is set in this file before that
release session, not during it. The user has said not to move to
`v0.6.20` until they say so.**

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 ABI, natively. Newlib is gone. Userland is a
tracked source tree at `userland/musl/`. Phase B (busybox) is well
underway: the shell is interactive, pipelines and redirection work, job
control works (`^C`, `^Z`, `jobs`, `fg`), and the applet set includes
`bc`, `dc`, `diff`, `du`, `awk`, `date`, `uptime`, `time` in addition to
the file/text applets. **`tar` creates, lists and extracts correct
archives, including directory members.** `date`/`uptime` report seconds
since boot as a 1970-epoch date (no RTC -- the honest base). **`time`
builds but is unreliable: its vfork child intermittently faults in musl
startup (see the open-issues entry).**

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`. Open issues:
`docs/open-issues.md`.

---

## Where we are

### Current baseline

- `test.sh` **57/57**, `canary --full` **28/28** at the session's gate
  points (last full gate before the vfork diagnostics: `canary` 28/28,
  `test.sh` 57/57). **Re-run both on a fresh boot before trusting them.**
- `date` prints `Thu Jan 1 00:00:30 UTC 1970` (30 s uptime), `uptime`
  prints `up 0 min`, load `0.00` -- correct for donix.
- `mktemp` works (both `/tmp/tmp.XXXXXX` forms).
- `^Z` on a foreground child -> `[1]+ Stopped`; `jobs` lists it; `fg`
  resumes. `^C` -> SIGINT reaches the foreground pgrp; the console
  survives.
- **`time true` is unreliable** -- sometimes `real 0m 0.18s`, often a
  `#PF` in the child. See the open-issues entry.
- The C is not observed since the 2026-11-02 disk-path fixes; still
  open -- evidence, not a close.

### Next -- the highest-value work

1. The open vfork fault (`docs/open-issues.md` line ~412; recover
   `stash@{0}`, strip diagnostics, probe `%r12`/`%rbx` before
   `o64 sysret`).
2. `umask` (95).
3. `mkdir` EEXIST/ENOTDIR.
4. Item 10 SSE.
5. Item 9 privilege model -- **with the "is this worth a session" note.**

### Refuted -- do NOT re-walk

These were discarded in earlier sessions or this one. If any turns out
to be a different root cause, that is a **new** hypothesis, not a revival.

- **The `efc8e08` rsp/irq0_rsp split, and `frame_kind`.** Discarded with
  the 2026-11-02 reset; not on `dev`.
- **The four uncommitted scheduler guards** (`process_yield` FRAME_IRQ0
  skip, `scheduler_ready_queue_add` non-PCB guard,
  `scheduler_switch_to` non-PCB guard, `scheduler_switch_to` frame-CS
  validate). Discarded with the reset and the stash drop.
- **The `EXIT-REFUSE frame-CS` guard in `process_exit`.** Discarded.
- **The canary `.text` `#PF` probe stack** (`[PT-CANARY]`, `[PT-WP]`,
  `[PHDR]`/`[EHDR]`, `[UNMAP_IN_CR3]`, `[FREE_CLONE]`, `[PCLAIM]`).
  Dropped with `stash@{1}`.
- **The physical-address gates on `.text` PTE zeroing.** Boot dependent;
  do not reapply without new evidence.
- **The `f91d31a` "scheduler guards fix canary/pipe7e/ctrl-Z" claim.**
  Disproved by running.
- **`exec_churn`'s KVM slowness is the console / the serial poll loop /
  the VGA cursor CRTC writes.** Refuted 2026-11-02: the cost was the ATA
  data port in `ata.c`, fixed by `rep insw`/`rep outsw`.
- **`ata_poll_drq` spinning.** Instrumented 2026-11-02: 2,113 calls, 128
  total spins. Not the cost.
- **`exec_churn` can measure the `sys_fork` eager-copy cost.** Refuted;
  its floor is the 10 ms PIT tick. A sub-tick fork benchmark is what a
  future session needs if this is ever to be measured.
- **The `perf kvm stat record` `double free` is a donix issue.**
  Refuted: it is a Fedora 44 `perf` bug in cleanup; the capture writes
  and the report reads correctly.
- **The 2026-11-04 PTAUDIT "22 anomalies at 0x455000..0x45C000" are page
  corruption.** **FALSE POSITIVE.** `0x8000_..._07D6_xxxx` in a donix PTE
  phys field is the normal encoding (HHDM stored in the PTE;
  `pte_phys()`/`phys_to_virt()` account for it). `DIAG-MAPAUDIT` never
  fired -- together that means "ordinary PTEs." The pages are busybox's
  own image. Do not re-chase.
- **The 2026-11-04 empty-low-half `vmm_create_address_space`.** Refuted
  by the triple fault: it dropped the 2 MB identity map at PML4 index 0.
  The committed fix is clone-then-unmap (`c652c8f`). Do not rebuild a
  fresh PML4.

### Open issues

The full list is `docs/open-issues.md`. The numbered items are 9 (no
privilege model) and 10 (no SSE save/restore). The un-numbered items
(7a, 7b, 7e, the `0x413962` `#PF`, the `.bss`-adjacent faults, the small
VFS/proc/pipe entries) are real and stay in that file. **The 2026-11-04
vfork fault is recorded there as its own heading (line ~412).** Read
`docs/open-issues.md` before touching the VFS, fork, exec, vfork, or the
signal paths.

---

## State on disk

Kernel sources: `04_kernel_64bit/`. `capture.txt` is gitignored,
overwritten each `./run` -- **copy it aside after any failure or pass you
want to keep.** The 2026-11-04 session did not keep most captures; that
is a reason to copy them going forward, not to re-derive.

**After a fresh image, one manual step:** the image stages
`/bin/busybox` alone. To get the applet symlinks (including `/bin/sh`,
which shebang needs), boot and run `/bin/busybox --install -s /bin`. The
absolute path is required: busybox reads `/proc/self/exe` to find itself,
donix has no `/proc/self/exe`, and it falls back to `argv[0]` -- which
must start with `/`. This is a manual step, not part of the build.

**`third_party/`** is **gitignored** -- do not commit edits to it.
`third_party/busybox/` has `busybox` (stripped) and
`busybox_unstripped` + `.map` if a userspace symbol is ever needed.
**`third_party/musl-install/` is gitignored and is needed for the
build.** If it's missing after a `git clean -xdf`, copy it back from a
working tree, or rebuild it with `toolchain/install_musl.sh`.
`04_kernel_64bit/fatfs/ff.c` is **tracked** (not `third_party/`), built
by the cross-GCC, and contains **two** `sync_window` fixes (`a0664a8` in
`f_open`, `d69c2ab` in `f_write`). Any further edit there must re-run the
full gate (`canary --full` + `test.sh` + the tar round trip + the
redirect/tar matrix), not just a build.

**`~/code/saved`** -- not a baseline. Run
`git -C ~/code/saved status --short` before any comparison; treat "not a
baseline" as the default. It has been dirty twice.

**The reset baseline.** `dev` was reset to `873be27`, tag
`reset-baseline-873be27`. `efc8e08` is not on `dev`; it survives as a
commit object. If a future session needs the discarded guards, they are
not in the tree -- read `git log --all --oneline` for `efc8e08`, but
treat them as **refuted**.

**The 2026-11-02 performance and correctness commits** (six, on top of
`f902af6`): `78216bc` fork `rep movsb`; `ea4dc2e` mode restore;
`e52bd2f` gitignore perf; `905bc70` ATA rep-pio; `fdbcbc7`
console-cursor-defer; `5db42f2` exec-churn-ns-timing; `6df4fef`
console-timer.

**The 2026-11-03 release commits** (on top of `a0664a8`/`d69c2ab`):
`c0126d4`, `28272f9`, `b0e9cce`, `9a36e76`, `ef090f4` (docs), and
`ba56b3d` (version banner `v0.6.19`, tagged).

**The 2026-11-04 commits** (8, on top of `9b62951`):
- `5a3b65e` `20261104-gettimeofday-time` -- syscalls 96/201.
  **Tagged `20261104-gettimeofday-time`.**
- `ea0986e` -- `borrows_cr3` guard.
- `dca2eff` -- busybox config: `DATE`, `UPTIME`, `TIME`.
- `1aaca0d` -- `sys_vfork` (58), block only.
- `6ca941b` -- `vmm_create_address_space` (kernel mappings only; first
  version, superseded).
- `c652c8f` -- `vmm_create_address_space` clone-then-unmap. **Fixes the
  triple fault.**
- `e5081e1` -- docs: record the open vfork-child startup fault.

**`stash@{0}`**: "vfork 3b (wake arm + sys_vfork) + diagnostics; time
true intermittent, not a complete fix" -- the exec-side vfork work plus
the session's diagnostics. Recover with `git stash show -p`; strip the
`DIAG-*` and the `#if 0` before using.

---

## One-line summary

**`v0.6.19` is released; `dev` is at `e5081e1`, ahead 8, clean, one
stash, nothing pushed.** The 2026-11-04 session shipped
**`gettimeofday`/`time`** (tagged), the **busybox date/uptime/time
config**, the **`borrows_cr3` guard**, and the **`vmm_create_address_space`
clone-then-unmap fix for the triple fault**; committed the **vfork block**
and recorded the **open vfork-child startup fault** (intermittent `#PF`
in musl startup, callee-saved register holding 0, `%r12=0` at
`0x4512E8`; leading hypothesis: syscall-return path). Nine kernel values
along the exec path were measured correct; the **PTAUDIT "22 anomalies"
were a false positive** and are refuted above. The exec-side vfork work
is in **`stash@{0}`**, not committed. **`time true` is unreliable and no
commit claims otherwise.** NEXT: the open vfork fault (`docs/open-issues.md`
line ~412; recover the stash, probe `%r12`/`%rbx` before `o64 sysret`),
then `umask` (95), `mkdir` EEXIST, item 10 SSE, item 9 with its "is it
worth a session" note. Read `docs/contract.md` and this file's NEXT
before any code. **Do not move to `v0.6.20` until the user says so.**
