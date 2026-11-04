# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-04 (second session), at `ae97ae9`.
**`v0.6.19` is the last pushed release.** `dev` is at `ae97ae9`; its
parent is `4312aab`. `origin/dev` = `ba56b3d`. Ahead **25**, clean
tree (plus an uncommitted `docs/open-issues.md` patch from this
session -- see below), **stash present** (`stash@{0}`: vfork 3b
diagnostics; preserved, do not drop). No push, no tag this session;
banner still `v0.6.19`. `main` = `7e4748b` (the `--no-ff` merge of
`v0.6.19`). Scratch tag `20261104-gettimeofday-time` is on `5a3b65e`
(still preserved).

**WHERE WE ARE.** The 2026-11-04 evening session landed two real
kernel fixes and made the fault instrument honest: `fb188f0` made
`exception_frame_t` match the 20-slot stub frame (every earlier
`#PF`/`#GP` dump had printed GPRs as `rip`/`cs`/`rsp`), and
`1bf2ca2` restored `fs_base` on context switch (the
`CR2=0x34`/`__errno_location` fault is gone). The second session
(today) then landed `4312aab` (`set_tid_address` `clear_child_tid`)
and `ae97ae9` (`umask` 95, `mkdir` `-EEXIST` on an existing name,
and the `mkdir("/")` fix that **unblocks `mkdir -p`**).

**`time true` has TWO fault signatures, both live in ONE boot.**
Both carry `RIP 0`, `CR2 0`, `error 0x15`, `CS 0x33`,
`last syscall num 218` (`set_tid_address`), `last sysret rcx
0x40FBC0`. They differ in fault RSP and in which `ret` popped 0:

- **true-side** (`pcb pid/name: N true`): `RSP =
  g_user_rsp_save + 0x10` (= `0x80000FBE30`). The `__init_tls`
  `ret` at `0x40FBF0` pops 0 from `0x80000FBE28`; the
  `__set_thread_area` call frame below it (`rsp-0x18 = 0x40FBA0`)
  is intact. **This is the signature the previous handoff
  described.**
- **busybox-side** (`pcb pid/name: N busybox`): `RSP =
  g_user_rsp_save + 0x180` (= `0x80000FBFA0`). The user-stack
  window at fault shows `rsp-0x18 = 0x40FBA0`, `rsp-0x20 =
  0x4659FE`, `rsp-0x28 = 0x80000FBFE8`, `rsp-0x30 =
  0x80000FBFF8` -- **the `__init_tls` frame is intact below the
  fault.** The `ret` that popped 0 is **above** `__init_tls`'s
  return, in `__init_libc`'s call chain. **A breakpoint at
  `0x40FBC0` is one frame too low for this one**: it fires, shows
  the slot correct, and the fault happens later.

**The next session's first work is a gdb probe, not a code change
-- see NEXT item 1, which is scoped to BOTH signatures and says
which probe answers which.**

**WHAT LANDED THIS SESSION (two code commits plus a docs patch).**

1. **`4312aab`** `syscalls`: `set_tid_address` `clear_child_tid`
   (218). `sys_set_tid_address` keeps its return value (the current
   pid, which musl reads in `__libc_start_main`) and stores the user
   VA in a new `pcb_t.clear_child_tid` (appended after
   `borrows_cr3`, no asserted offset moves). `process_exit`
   (scheduler.c) writes `0` to `*clear_child_tid` after
   `close_all_files` and before the state change, while the exiting
   process's own CR3 is still loaded. The store rejects `0` and
   non-canonical-kernel VAs but does **not** walk the page table --
   a canonical-but-unmapped VA would fault there; no current caller
   does that. **Not claimed to fix `time true`.** Gate: 28/28 +
   57/57.
2. **`ae97ae9`** `syscalls`: `umask(95)`; `mkdir("/")` and existing
   names return `EEXIST`. Four pieces, verified by behavior:
   - `sys_umask` stores the low nine bits in `pcb_t.umask` and
     returns the previous mask, defaulting to `022` on first use
     (`process_initialize_pcb` zeroes the field). FatFs has no mode
     store, so the mask is not persisted; `sys_mkdir` applies it to
     the mode it hands the driver. Verified: `umask` -> `0022`,
     `umask 077` then `umask` -> `0077`, `umask 022` -> `0022`.
   - `fat_mkdir` translates `FR_EXIST` to `-(long)17` (`EEXIST`)
     **locally**, before falling through to `fatfs_errno`, which
     still maps `FR_EXIST` to `-EPERM_` for every other FatFs
     caller. Verified: a second `mkdir /a` prints `File exists`.
   - `sys_mkdir` returns `-EEXIST` for a resolved path of `"/"`
     **before `strip_dot_prefix` runs.** `strip_dot_prefix("/")`
     leaves `"."`, not `""`, and `f_mkdir(".")` returns
     `FR_INVALID_NAME`, which `fatfs_errno` maps to `-ENOENT`. The
     first attempt checked `local_path[0] == '\0'` *after* the
     strip and was dead code; a one-run `serial_print` named it
     (`path='/' rr=0(+) resolved='/' user_first=47` then
     `post-strip local_path='.' user_first=47`).
   - **`mkdir -p` now works.** Verified: `mkdir -p /b/c/d`
     silent, `ls /b/c/d` empty, `mkdir ""` still `-ENOENT`,
     `mkdir /b` prints `File exists`, `rm -r /b` silent.
   Gate: 28/28 + 57/57.
3. **A `docs/open-issues.md` patch is uncommitted in the working
   tree.** It updates `Last updated:`, rewrites the `umask`,
   `mkdir -EEXIST`, and `set_tid_address` entries as implemented,
   adds the `mkdir("/")`-before-`strip_dot_prefix` hazard, adds the
   `FR_EXIST`-translation-scope hazard, records the live
   `wake_no_waiter == writers_nonzero` observation (111 == 111) as a
   standing question and not a mechanism, corrects the
   `argv_scratch` claim, adds a `-ENOTDIR` open item, and adds two
   new headings: *"Two measurement corrections from the 2026-11-04
   second session"* and the two-signature description under the
   existing *"busybox applet dispatch faults after exec"* heading.
   **Commit it before the next code change.**

**WHAT WAS REFUTED -- do NOT re-walk.**

*This session's additions first.*

- **`argv_scratch` as a 16 KB stack hazard.** Measured:
  `objdump -d kernel.elf` shows `sys_execve`'s frame is
  `sub $0x1a68,%rsp` = **6760 bytes** of `PROC_STACK_SIZE = 16384`.
  `argv_scratch` is `16 * 256 = 4096` bytes, not 16 KB and not
  `[64][256]`. The 16 KB array is `envp_scratch`, **already
  `kmalloc`'d** (`user_syscall.c:5845-5849`). No 16 KB stack
  overflow in `sys_execve` exists. The handoff's former NEXT item 3
  was mis-stated.
- **`mkdir("/")` handled by an empty-path check after
  `strip_dot_prefix`.** `strip_dot_prefix("/")` leaves `"."`, not
  `""`; the check never fired. See `ae97ae9` above.
- **`EEXIST_` as an errno name.** The tree has `EPERM_`, `ENOENT_`,
  `EFAULT_`, but no `EEXIST_` and no `EEXIST`. The `mkdir`
  translation uses the literal `-(long)17`, as the source's own
  comment prescribed.
- **A two-argument `fat_mkdir` / a two-argument driver `mkdir`.**
  The forward declaration at `user_syscall.c:1603` is
  `static long fat_mkdir(const char* local_path);` and the driver's
  `mkdir` member takes one argument. Threading `mode` through
  breaks the build.
- **`set -e` in a runner that can abort.** It closes the terminal on
  the abort; the abort message is the signal and must stay visible.
  The `python3 ... ; git diff --stat` form (semicolon, no `set -e`)
  does not close the terminal.
- **The one gdb run where `time true` succeeded.** Never
  reproduced. Cause unknown. **Not** evidence of a race and **not**
  a fix.

*Earlier, unchanged.*

- **The `cli` window** in `user_syscall_entry.asm` (moving `cli`
  above `add rsp,8`). Tried, five faults, identical signature.
- **Signal delivery** as the zeroer. `SIGDELIVER` only ever fired for
  pid 3 / `num=231` / `frame=0x1`, never for a faulting child.
- **`memset`** as the zeroer. The watchpoint hit was `__init_libc`
  re-using the slot after `__init_tls` returned -- normal.
- **The sysret slot arithmetic / the user-RSP load.**
- **"The slot flips `0x40B163`→0 in one process."** The gdb printfs
  had no per-process marker, and `%rsp=0x80000fbe28` is the same VA
  in every busybox process (own physical page each). **Every fix
  built on this premise failed.**
- **The three `exec_path` forms** (`bin/true`, `/bin/true`,
  `0:/bin/true`). `/bin/true` does not exist. `strip_dot_prefix` is
  back as it was.
- **The `.bss`-not-zeroed hypothesis.** `elf_load_into_process`
  already zeroes `memsz > filesz` tails (elf.c:214-220).
- **`0x448C6A` / `CR2=0x34` as the `time true` root cause.** Real,
  separate bug (`fs_base`), fixed by `1bf2ca2`. The `time true`
  fault survived it.
- **`lbb_prepare` / `run_applet_and_exit` / `applet_main[rbx]`.**
  The child never reaches them; the fault is at `RIP 0` before
  `main`.
- **The auxv, `set_tid_address`'s return, `mmap`, V3/V3b register
  inheritance** -- all refuted earlier; unchanged.
- **`time /bin/true` as a working control, and "no fault is an
  improvement."** There is no `/bin/true`; `f_open` returned
  `FR_NO_FILE` and the shell exited 127 **before a child existed**.
  Valid real-file controls are `/usr/bin/HELLO`, `/usr/bin/ECHO`,
  `/usr/bin/CAT`; all three fault (`0x400221`, `0x4002EC`,
  `0x40039D`).
- **The `efc8e08` rsp/irq0_rsp split, and `frame_kind`.**
- **The four uncommitted scheduler guards.**
- **The `EXIT-REFUSE frame-CS` guard in `process_exit`.**
- **The canary `.text` `#PF` probe stack.**
- **The physical-address gates on `.text` PTE zeroing.**
- **The `f91d31a` "scheduler guards fix canary/pipe7e/ctrl-Z"
  claim.**
- **`exec_churn`'s KVM slowness is the console / the serial poll loop
  / the VGA cursor CRTC writes.**
- **`ata_poll_drq` spinning.**
- **`exec_churn` can measure the `sys_fork` eager-copy cost.**
- **The `perf kvm stat record` `double free` is a donix issue.**
- **The 2026-11-04 PTAUDIT "22 anomalies."**
- **The 2026-11-04 empty-low-half `vmm_create_address_space`.**
- **V3/V3b register zeroing** (reverted; Linux gives the child the
  parent's registers).
- **The missing auxv** (present and correct).
- **`sys_set_tid_address`** as the fault (the `ret` is two
  instructions past it; the `tidptr` gap is now closed by
  `4312aab`, and the fault is unchanged).
- **A `call *%rax` in `static_init_tls` at `0x40FBC0`**
  (`0x40FBC0` is the instruction after the `syscall`; `__init_tls`'s
  `ret` is `0x40FBF0`).
- **`kmain.c`'s self-tests map over the ELF text page.**

**NEXT -- the highest-value work, in order.**

1. **The `0x40FBC0` probe, scoped to BOTH `time true` signatures.**
   Boot `./run debug`, attach gdb, and set **one**
   `hbreak *0x40fbc0` with `commands` printing
   `slot=%lx rsp=%lx fs=%lx` for `*(unsigned long*)0x80000fbe28`,
   `$rsp`, `$fs_base`, then `continue`. **Every line must carry a
   per-process marker (`$fs_base` or pid) or the lines are
   cross-process and the inference is invalid.** Run `time true`
   until it faults, copy `capture.txt` aside, and read the **last**
   `PROBE` line before the fault together with the fault dump's
   `pcb pid/name` and `RSP`:

   - **If the fault is true-side** (`RSP = 0x80000FBE30`,
     `+0x10`): `0x40FBC0` is the right probe.
     - `slot=0x40B163` at `0x40FBC0` -> the `call __init_tls`
       return slot is correct at sysret; something between
       `0x40FBC0` and `0x40FBF0` zeroes it. A **watchpoint** on
       `0x80000fbe28` with `$rip` and `rdi/rsi/rdx` + `x/8gx $rsp`
       at the hit names the writer.
     - `slot=0` at `0x40FBC0` -> the `call`'s push never wrote it;
       the bug is before the sysret, in
       `__init_libc`/`_start`/`rsp_init`.
     - `fs` differs from `0x46A138` -> the `wrmsr` restore is not
       reaching this path; re-check `context_switch_resume` is the
       resume path used for the exec'd child.
   - **If the fault is busybox-side** (`RSP = 0x80000FBFA0`,
     `+0x180`): `0x40FBC0` is **one frame too low**; the `ret`
     that popped 0 is in `__init_libc`'s call chain above
     `__init_tls`. The fault dump's user-stack window is the
     evidence: `rsp-0x18 = 0x40FBA0`, `rsp-0x20 = 0x4659FE`,
     `rsp-0x28 = 0x80000FBFE8`, `rsp-0x30 = 0x80000FBFF8`. The
     next probe is a breakpoint on the `ret` at the faulting
     frame, which the dump names: read the word at
     `fault_rsp - 0x10` (the zero) and the word at
     `fault_rsp - 0x18` (the `0x40FBA0` return address) and
     disassemble the busybox code around `0x40FBA0` -- the `ret`
     that returned `0x40FBA0` is the one whose pop was 0. A
     watchpoint on the slot at `fault_rsp - 0x10` (once the
     faulting process's page table is loaded) names the writer.

   **A silent breakpoint is itself a result.** If `0x40FBC0`
   never fires before a fault, the writer is before the sysret.
2. **`sys_mkdir` `-ENOTDIR` for the file case.** POSIX wants
   `-ENOTDIR` when the target exists as a file; `-EEXIST` when it
   exists as a directory. `f_mkdir` returns `FR_EXIST` for both and
   does not distinguish. Small; `sys_mkdir`-local (a `f_stat`
   pre-check or a driver-level distinction); no current caller
   depends on the difference.
3. **`docs/open-issues.md` item 10 -- SSE save/restore.**
   Unchanged. The test is in the item.
4. **`docs/open-issues.md` item 9 -- the privilege model.**
   **Before starting, ask what it unlocks.** If "nothing," skip.
5. **The live `wake_no_waiter == writers_nonzero` observation
   (111 == 111).** Recorded in `docs/open-issues.md`. Do **not**
   chase it on this evidence; it is a standing question, not a
   mechanism.

**NOT DONE, NOT THIS SESSION'S WORK.** The C halt
(`context_switch_resume`'s unknown-CS branch) has not been observed
since the 2026-11-02 disk-path fixes. No mechanism was named. Do not
chase it without new evidence.

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
Two steps, both explicit.

**Never put a destructive git command inside a block that also runs
diagnostics.** A `git checkout -- <file>` or `git restore` in a block
whose other lines are `git status`/`git log`/`grep` is a trap. If a
revert is wanted, name it on its own line, say what it discards, and
run it as its own paste.

**Python edits with `count != 1` abort checks are the preferred edit
style.** **Idempotent edit scripts.** **Do the count check IN PYTHON
(`str.count`), not with `grep`.** `grep -Fc` does not do multi-line
matches; `grep -Fxc` requires a whole-line match; `str.count` is the
only honest multi-line count.

**NO `set -e` in a runner that can abort or that runs a patch script
that may abort.** It closes the terminal on the non-zero exit and
hides the abort message, which is the signal. Use
`python3 /tmp/patch.py ; git diff --stat` (semicolon) and read the
`ABORT` line. The patch script's own `sys.exit(1)` is enough to
signal failure; `set -e` on top of it is a trap. `set -e` **inside**
a written script is fine and is the preferred style for the script
itself; it is the *runner* that must not have it.

**A one-run diagnostic on the FAILURE path beats three guesses.**
The `mkdir("/")` fix took three guessed patches and one
`serial_print` run; the run named it (`path='/' rr=0 resolved='/'
post-strip "."`). When a branch or an address is in doubt, add the
print, build, run once, read it, and revert -- do not guess twice.

**BEFORE ASSERTING A COUNT, count the occurrences in the file and
write the expected number from that count, not from memory.** The
2026-11-04 sessions burned turns on wrong check strings and on a
clang `_Static_assert` diagnostic that prints the **requirement**, not
the comparison (`0x1A0` vs `416` vs the real `0x378`). **When a
`_Static_assert` fails, read the number in `evaluates to 'N == M'` --
`N` is the actual value.**

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION -- AND ON A
STATEMENT BOUNDARY, NOT A FRAGMENT INSIDE A STRING LITERAL.** Grep the
file, confirm the count is 1 **and** the match is in the intended
function. Anchor from the file's current bytes (`cat -A`), open
`rb`/`wb` to preserve line endings. If an anchor aborts, **ask for the
current bytes** -- do not guess a second anchor.

**A post-write check string must be copied from the `new` literal,
not retyped.**

**A post-write check is only valid if `new` is unique in the file.**
For a removal edit (`new` is a short, non-unique replacement such as
`    int backend;`), skip the post-check; the `count(old) == 1`
pre-check plus a single `s.replace(old, new, 1)` is sufficient.

**The build is TWO steps.** `make -C 04_kernel_64bit
FAT_CONFIG=single` builds the kernel; `make -C 05_boot_kernel64
hdd-single.img` builds the image. Running only the second copies a
STALE `kernel.bin`. Always run both, and verify the mtime chain
`source` < `.o` < `kernel.elf` < `kernel.bin` < `hdd-single.img`.
**`make: Nothing to be done` after editing a header means the object
was not rebuilt -- run `make -C 04_kernel_64bit clean` first.**
**After a session of probe edits and reverts, run a full `make clean`
before the next behavioral test.**

**The patch-script write changes the mode of the file it writes.** A
Python `open(..., "wb")` on the **same path** (not `os.replace`)
preserves the inode and the mode bits. **Verify `stat -c '%a'` before
and after every patch script.**

**`nm` output moves between builds.** Re-run `nm` before using any
address. **And the symbol at an address may be the *next* symbol, not
the function the address is in** -- the first symbol `<=` the address
is the enclosing function; the first `>=` is the *next* function.
Verify both.

**A diagnostic on the SUCCESS path is noise. A diagnostic on the
FAILURE path is signal.**

**A probe that fires nothing is a result.** A silent probe and a loud
sibling together are the evidence.

**gdb printf with no per-process marker is not evidence about a
process.** `%rsp` is the same VA in every process; the physical page
differs. Print `$fs_base` or a pid at every hit, or the lines are
cross-process and the inference is invalid. This is the single most
expensive mistake this project has made.

**Test harness.** `./run` (no arg) = clean rebuild + fresh
`hdd-single.img` + `-enable-kvm -cpu host -display sdl`, no freeze,
gdb stub on `:1234`, `capture.txt` written (overwritten each run --
**copy it aside after any failure or pass you want to keep**).
`./run debug` adds `-S` (frozen at reset; **the guest does nothing
until gdb attaches and `continue`s -- if it looks frozen at a black
screen, that is the `-S`, not a fault**). **`./run tcg` /
`./run tcg-debug`** = the same flow in TCG (`-cpu max`, no
`-enable-kvm`). The Makefile run targets also use `-cpu max` + TCG --
**state which launcher was used.** Default cwd `/root`.

**In-guest gate.** `cd /`, then `canary --full` (28/28) and
`/root/scripts/test.sh` (57/57). **Use multiple `^Z`/`fg` cycles.**
One is not always enough to reach an intermittent fault.

**Perf kvm stat, GDB raw offsets, and the Fedora 44 perf
`double free`** are unchanged from the previous session's notes.

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
ready and verified.** `origin/dev` is not force-pushed; every unpushed
`dev` commit is intentional work. Run `git log --oneline
origin/dev..dev | wc -l` for the current count.

**Current release state.** **`v0.6.19` is the last pushed release.**
`dev` is at `ae97ae9`; its parent is `4312aab`. Ahead **25**, clean,
**stash present**. `origin/dev` = `ba56b3d`. Scratch tag
`20261104-gettimeofday-time` on `5a3b65e`. Work is now
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
work, job control works (`^C`, `^Z`, `jobs`, `fg`), and the applet set
includes `bc`, `dc`, `diff`, `du`, `awk`, `date`, `uptime`, `time` in
addition to the file/text applets. **`tar` creates, lists and extracts
correct archive members, including directory members.**
**`mkdir -p` works** (as of `ae97ae9`).
`date`/`uptime` report seconds since boot as a 1970-epoch date (no RTC
-- the honest base). **`time` builds, its vfork parent-reap path works
(parent wakes, `wait4` reaps, a wall time prints), and its child's
startup still faults at `RIP 0` after `set_tid_address` -- in two
signatures, see WHERE WE ARE and NEXT.**

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`. Open issues:
`docs/open-issues.md`.

---

## Where we are

### Current baseline

- `test.sh` **57/57**, `canary --full` **28/28** at every gate point
  this session, on a fresh boot, after both code commits. **Re-run
  both on a fresh boot before trusting them.**
- `date` prints `Thu Jan 1 00:00:44 UTC 1970`, `uptime` prints
  `up 0 min`, load `0.00` -- correct for donix.
- `mktemp` works (both `/tmp/tmp.XXXXXX` forms).
- `^Z` on a foreground child -> `[1]+ Stopped`; `jobs` lists it; `fg`
  resumes. `^C` -> SIGINT reaches the foreground pgrp; the console
  survives.
- **`mkdir -p /b/c/d` succeeds silently**, `ls /b/c/d` empty,
  `mkdir /b` prints `File exists`, `mkdir ""` prints `No such file
  or directory`. `umask` returns `0022` and round-trips through
  `077` and `022`.
- **`time true` faults at `RIP 0`** in **two signatures** (see
  WHERE WE ARE). The shell sees `Command terminated by signal 11`
  and returns to the prompt. **No `CR2=0x34` fault since
  `1bf2ca2`.**
- The C is not observed since the 2026-11-02 disk-path fixes; still
  open -- evidence, not a close.

### Next -- the highest-value work

1. The `0x40FBC0` probe, **scoped to both signatures**; then a
   watchpoint on the zeroed slot with `$rip` + `rdi/rsi/rdx`. See
   NEXT item 1 above for the two-branch decision tree.
2. `sys_mkdir` `-ENOTDIR` vs `-EEXIST` for the file case.
3. Item 10 SSE.
4. Item 9 privilege model -- **with the "is this worth a session"
   note.**
5. The `111 == 111` pipe equality -- recorded, not chased.

### Refuted -- do NOT re-walk

See the **WHAT WAS REFUTED** section above. It is the single list;
this section is a pointer, not a duplicate. The additions this
session are at the top: `argv_scratch` 16 KB, the
`mkdir("/")`-after-strip branch, `EEXIST_`, two-argument
`fat_mkdir`/driver `mkdir`, `set -e` in a runner, and the one
unreproduced gdb success.

### Open issues

The full list is `docs/open-issues.md`. The numbered items are 9 (no
privilege model) and 10 (no SSE save/restore). The un-numbered items
(7a, 7b, 7e, the `0x413962` `#PF`, the small VFS/proc/pipe entries)
are real and stay in that file. **The 2026-11-04 second session
patched that file** (uncommitted at the time this handoff was
written): the `umask`, `mkdir` `-EEXIST`, `mkdir("/")`, and
`set_tid_address` entries are now written as implemented; the
`mkdir("/")`-before-`strip_dot_prefix` and `FR_EXIST`-scope hazards
are in the hazards section; the `wake_no_waiter == writers_nonzero`
111 == 111 observation is recorded; the `argv_scratch` claim is
corrected; and a new *"Two measurement corrections from the
2026-11-04 second session"* heading carries the two lessons. The
two-signature `time true` description is under the existing
*"busybox applet dispatch faults after exec"* heading.

---

## State on disk

Kernel sources: `04_kernel_64bit/`. `capture.txt` is gitignored,
overwritten each `./run` -- **copy it aside after any failure or pass
you want to keep.** The second session kept several; the
`44D6A3`-signature and the two `time true` signature captures are
worth keeping if a next session needs them.

**After a fresh image, one manual step:** the image stages
`/bin/busybox` alone. To get the applet symlinks (including `/bin/sh`,
which shebang needs), boot and run `/bin/busybox --install -s /bin`.
The absolute path is required: busybox reads `/proc/self/exe` to find
itself, donix has no `/proc/self/exe`, and it falls back to `argv[0]`
-- which must start with `/`. This is a manual step, not part of the
build. **Note:** on this image, `busybox --install -s /bin` prints
`Operation not permitted` for `/bin/busybox` and `/bin/sh` (the
`FR_EXIST` -> `-EPERM_` mapping for those two, which are already
there) and creates the rest. That is expected; `ls /bin` is the
check.

**`/bin` contains only `busybox` and `sh`.** There is no `/bin/true`,
`/bin/echo`, `/bin/cat`. `time true` works because `true` is a
busybox applet. `time /bin/true` fails at `f_open` with `FR_NO_FILE`
-- the file does not exist. Use `/usr/bin/HELLO`, `/usr/bin/ECHO`,
`/usr/bin/CAT` for real-file control tests.

**`third_party/`** is **gitignored** -- do not commit edits to it.
`third_party/busybox/` has `busybox` (stripped) and
`busybox_unstripped`; the `.map` file is **not** present, so `nm -n` +
`objdump -d` are the symbol tools, and `readelf -lW` gives the program
headers. `third_party/musl-install/` is gitignored and is needed for
the build. If it's missing after a `git clean -xdf`, copy it back from
a working tree, or rebuild it with `toolchain/install_musl.sh`.

**`04_kernel_64bit/fatfs/ff.c`** is tracked and has two `sync_window`
fixes; any further edit there must re-run the full gate.

**`~/code/saved`** -- not a baseline. Run
`git -C ~/code/saved status --short` before any comparison.

**The reset baseline.** `dev` was reset to `873be27`, tag
`reset-baseline-873be27`. `efc8e08` is not on `dev`; it survives as a
commit object. Treat its guards as **refuted**.

**The 2026-11-02 performance and correctness commits** (six, on top of
`f902af6`): `78216bc`, `ea4dc2e`, `e52bd2f`, `905bc70`, `fdbcbc7`,
`5db42f2`, `6df4fef`.

**The 2026-11-03 release commits:** `c0126d4`, `28272f9`, `b0e9cce`,
`9a36e76`, `ef090f4` (docs), and `ba56b3d` (version banner `v0.6.19`,
tagged).

**The 2026-11-04 commits (23, on top of `9b62951`):**

- `5a3b65e` -- syscalls 96/201, tagged
  `20261104-gettimeofday-time`.
- `ea0986e` -- `borrows_cr3` guard.
- `dca2eff` -- busybox config: `DATE`, `UPTIME`, `TIME`.
- `1aaca0d` -- `sys_vfork` (58), block only.
- `6ca941b` -- `vmm_create_address_space` (first version, superseded).
- `c652c8f` -- `vmm_create_address_space` clone-then-unmap.
- `e5081e1` -- docs: record the open vfork-child fault.
- `8514fca` -- docs: the previous handoff rewrite.
- `a885888` -- `sys_vfork`: clear `block_kind`/`wait_pid` on resume.
- `3f81fb1` -- `sys_vfork`: save the nine caller-saved GPRs before the
  parent blocks.
- `001a756` -- `sys_execve`: fresh cr3 for the vfork child + exec
  wake.
- `c1bd883` -- `sys_vfork`: zero the child's callee-saved GPRs.
  **Reverted by `325e85b`.**
- `0a9ca16` -- `process.c`: accept `BLOCK_KIND_VFORK`.
- `f57299b` -- `sys_vfork`: zero the child's caller-saved GPRs.
  **Reverted by `325e85b`.**
- `a10e585` -- `sys_execve`: write the auxv.
- `325e85b` -- revert of `c1bd883` + `f57299b`.
- `32b3417` -- docs: the morning handoff rewrite.
- `fb188f0` -- **`interrupts`: `exception_frame_t` matches the
  20-slot stub frame.**
- `1bf2ca2` -- **`context_switch`: restore `fs_base` on resume from
  `next->fs_base` (`0x378`, asserted).**
- `4704b8a` -- docs: the evening handoff rewrite.
- `9cf7380` -- docs: replace the refuted
  `lbb_prepare`/`run_applet_and_exit` NEXT with the `0x40FBC0`
  probe.
- `ddd73ba` -- docs: two post-close corrections.
- `4312aab` -- **`syscalls`: `set_tid_address` `clear_child_tid`
  (218).**
- `ae97ae9` -- **`syscalls`: `umask(95)`; `mkdir("/")` and existing
  names return `EEXIST`; `mkdir -p` unblocked.**
- (uncommitted) `docs/open-issues.md` patch from this session --
  **commit it before the next code change.**

**Stash.** `stash@{0}`: "vfork 3b (wake arm + sys_vfork) +
diagnostics; time true intermittent, not a complete fix." This is a
preserved diagnostic trail. **Do not drop, do not pop.**

---

## One-line summary

**`v0.6.19` is released; `dev` is at `ae97ae9` (parent `4312aab`),
ahead 25, clean except the uncommitted `docs/open-issues.md`
patch, stash present, nothing pushed.** The 2026-11-04 second
session landed **`4312aab`** (`set_tid_address`
`clear_child_tid`) and **`ae97ae9`** (`umask(95)`, `mkdir`
`-EEXIST` on an existing name, and the `mkdir("/")` fix that
**unblocks `mkdir -p`**), and corrected two handoff items by
measurement: `argv_scratch` is 4 KB and the 16 KB array is
`envp_scratch`, already `kmalloc`'d; and the `mkdir("/")` check
must run **before** `strip_dot_prefix`, because
`strip_dot_prefix("/")` leaves `"."`, not `""`. **`time true` has
TWO signatures, both live in one boot:** true-side
`RSP = g_user_rsp_save + 0x10`, busybox-side
`+0x180` with the `__init_tls` frame intact below the fault. Both
carry `num 218` / `rcx 0x40FBC0`. **NEXT: the
`hbreak *0x40fbc0` probe scoped to both signatures -- it is the
right probe for the true-side fault and one frame too low for the
busybox-side one; read the last `PROBE` line with a per-process
marker, then a watchpoint on the zeroed slot with `$rip` if the
slot is correct at sysret.** Every earlier hypothesis is in the
Refuted list. Read `docs/contract.md` and this file's NEXT before
any code. **Do not move to `v0.6.20` until the user says so.**
