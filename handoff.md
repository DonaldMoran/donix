# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-04 (evening), at the handoff commit itself.
**`v0.6.19` is the last pushed release.** `dev` is the commit that
added this file (`git log -1 --format=%h` to see it); its parent is
`1bf2ca2`. `origin/dev` = `ba56b3d`. Ahead **21**, clean tree,
**stash present** (`stash@{0}`: vfork 3b diagnostics; preserved, do
not drop). No push, no tag this session; banner still `v0.6.19`.
`main` = `7e4748b` (the `--no-ff` merge of `v0.6.19`). Scratch tag
`20261104-gettimeofday-time` is on `5a3b65e` (still preserved).

**WHERE WE ARE.** The 2026-11-04 vfork parent-reap path is complete
(seven keepers landed earlier in the day). The evening session found
that **the `#PF` / `#GP` diagnostic frame was lying**: the
`exception_frame_t` struct began at `error_code` while the stubs push
15 GPRs first, so every dump printed `r14`/`r13`/`r11` as
`rip`/`cs`/`rsp`. That is fixed (`fb188f0`). A second real bug was
found and fixed (`1bf2ca2`): `context_switch_resume` never restored
`fs_base`, so a per-process TLS base set by `arch_prctl(ARCH_SET_FS)`
was clobbered by the next process to run; musl's `errno` load in
`__syscall_ret` then faulted at `CR2=0x34`.

**With an honest instrument, `time true` has ONE fault signature
left**, and it is not any of the previously named causes: `RIP 0`,
`CR2 0`, `error 0x15`, `CS 0x33`, `last syscall num 218`
(`set_tid_address`), `last sysret rcx 0x40FBC0`, fault
`RSP = g_user_rsp_save + 0x10`. The `__init_tls` `ret` at `0x40FBF0`
pops 0 from `0x80000FBE28`; the `__set_thread_area` call frame just
below it (`rsp-0x18 = 0x40FBA0`) is intact. **The next session's
first work is a gdb probe, not a code change -- see NEXT.**

**WHAT LANDED THIS EVENING (two commits plus this handoff).**

1. **`fb188f0`** `interrupts`: `exception_frame_t` now matches the
   20-slot stub frame (15 GPRs, then `error_code`/`rip`/`cs`/`rflags`/
   `rsp`/`ss`, packed). The old layout began at `error_code`, so
   `frame->rip` read `r14`, `frame->cs` read `r13`, `frame->rsp` read
   `r11`. Every dump this session printed GPRs as control fields,
   which is why every earlier "fault site" looked like an ELF entry
   point. Gate: 28/28 + 57/57.
2. **`1bf2ca2`** `context_switch`: restore `fs_base` on resume from
   `next->fs_base` (`pcb_t` offset `0x378`, now pinned by
   `_Static_assert` in `process.c`). `wrmsr(0xC0000100, ...)` clobbers
   `eax`/`edx`/`ecx`, so it runs after `mov cr3, rax` and before
   `POP_ALL_GPRS`. This removed the `CR2=0x34` / `RIP=0x448C6A`
   (`__errno_location`) fault. Gate: 28/28 + 57/57.
3. **This commit** rewrites the handoff for the next session.
4. **The seven earlier keepers** (`a885888`, `3f81fb1`, `001a756`,
   `0a9ca16`, `a10e585`, `325e85b`, and the revert) are unchanged.

**WHAT WAS REFUTED THIS EVENING -- do NOT re-walk.**

- **The `cli` window** in `user_syscall_entry.asm` (moving `cli`
  above `add rsp,8`). Tried, five faults, identical signature.
- **Signal delivery** as the zeroer. `SIGDELIVER` only ever fired for
  pid 3 / `num=231` / `frame=0x1`, never for a faulting child.
- **`memset`** as the zeroer. The watchpoint hit was `__init_libc`
  re-using the slot after `__init_tls` returned -- normal.
- **The sysret slot arithmetic / the user-RSP load.** `last sysret
  rsp = g_user_rsp_save` (correct); `mov [rsp-72], %rsp` reads the
  right slot.
- **"The slot flips `0x40B163`→0 in one process."** The gdb printfs
  had no per-process marker, and `%rsp=0x80000fbe28` is the same VA
  in every busybox process (own physical page each). The two lines
  were very likely different processes printed adjacently. **Every
  fix built on this premise failed.**
- **The three `exec_path` forms** (`bin/true`, `/bin/true`,
  `0:/bin/true`). `/bin/true` does not exist -- only `busybox` and
  `sh` are staged in `/bin`. The path was never the bug for that
  case; `strip_dot_prefix` is back as it was.
- **The `.bss`-not-zeroed hypothesis.** `elf_load_into_process`
  already zeroes `memsz > filesz` tails (elf.c:214-220).
- **`0x448C6A` / `CR2=0x34` as the `time true` root cause.** It was a
  real, separate bug (`fs_base`), fixed by `1bf2ca2`. The `time true`
  fault survived it.
- **`lbb_prepare` / `run_applet_and_exit` / `applet_main[rbx]`.** The
  child never reaches them; the fault is at `RIP 0` before `main`.
- **The auxv, `set_tid_address`'s return, `mmap`, V3/V3b register
  inheritance** -- all refuted earlier in the day; unchanged.

**NEXT -- the highest-value work, in order.**

1. **The `0x40FBC0` probe, with the instrument now honest.** Boot
   `./run debug`, attach gdb, and set **one** `hbreak *0x40fbc0` with
   `commands` printing `slot=%lx rsp=%lx fs=%lx` for
   `*(unsigned long*)0x80000fbe28`, `$rsp`, `$fs_base`, then
   `continue`. Run `time true` until it faults, and read the **last**
   line before the fault:
   - `slot=0x40B163` at `0x40FBC0` → the `call __init_tls` return
     slot is correct at sysret; something between `0x40FBC0` and
     `0x40FBF0` zeroes it. Then a **watchpoint** on
     `0x80000fbe28` with `info registers rdi rsi rdx` + `x/8gx $rsp`
     at the hit names the writer (and `$rip` names the instruction).
   - `slot=0` at `0x40FBC0` → the `call`'s push never wrote it; the
     bug is before the sysret, in `__init_libc`/`_start`/`rsp_init`.
   - `fs` differs from `0x46A138` → the `wrmsr` restore is not
     reaching this path; re-check `context_switch_resume` is the
     resume path used for the exec'd child.
2. **`sys_set_tid_address` `tidptr`.** Store the `tidptr` in the
   `pcb_t` and clear `*tidptr` in `process_exit` when non-zero.
   Small, self-contained, a real correctness fix; **not** claimed to
   fix `time true`.
3. **`sys_execve`'s `argv_scratch[64][256]` = 16 KB on a 16 KB
   `PROC_STACK_SIZE` stack.** Pre-existing hazard. Move it to
   `kmalloc` as its own commit. Do **not** bundle it.
4. **`umask` (95) unimplemented.** `mkdir -p` fails with
   `Unknown syscall: 95`; `mkdir` without `-p` works. Small.
5. **`mkdir` on an existing directory returns `EPERM` not `EEXIST`.**
   `tar -xvf` prints `Operation not permitted`; pair with item 4.
6. **`docs/open-issues.md` item 10 -- SSE save/restore.** Unchanged.
7. **`docs/open-issues.md` item 9 -- the privilege model.**
   **Before starting, ask what it unlocks.** If "nothing," skip.

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
style.** `set -e` at the top of the *script*. **Idempotent edit
scripts.** **Do the count check IN PYTHON (`str.count`), not with
`grep`.** `grep -Fc` does not do multi-line matches; `grep -Fxc`
requires a whole-line match; `str.count` is the only honest
multi-line count.

**BEFORE ASSERTING A COUNT, count the occurrences in the file and
write the expected number from that count, not from memory.** The
2026-11-04 session burned turns on wrong check strings and on a
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
cross-process and the inference is invalid. This session's central
mistake.

**Test harness.** `./run` (no arg) = clean rebuild + fresh
`hdd-single.img` + `-enable-kvm -cpu host -display sdl`, no freeze,
gdb stub on `:1234`, `capture.txt` written (overwritten each run --
**copy it aside after any failure or pass you want to keep**).
`./run debug` adds `-S` (frozen at reset). **`./run tcg` /
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
`dev` is the commit that added this file; its parent is `1bf2ca2`.
Ahead **21**, clean, **stash present**. `origin/dev` = `ba56b3d`.
Scratch tag `20261104-gettimeofday-time` on `5a3b65e`. Work is now
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
`date`/`uptime` report seconds since boot as a 1970-epoch date (no RTC
-- the honest base). **`time` builds, its vfork parent-reap path works
(parent wakes, `wait4` reaps, a wall time prints), and its child's
startup still faults at `RIP 0` after `set_tid_address` -- the one
open signature; see NEXT.**

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`. Open issues:
`docs/open-issues.md`.

---

## Where we are

### Current baseline

- `test.sh` **57/57**, `canary --full` **28/28** at every gate point
  this session, on a fresh boot, after both code commits together.
  **Re-run both on a fresh boot before trusting them.**
- `date` prints `Thu Jan 1 00:00:44 UTC 1970`, `uptime` prints
  `up 0 min`, load `0.00` -- correct for donix.
- `mktemp` works (both `/tmp/tmp.XXXXXX` forms).
- `^Z` on a foreground child -> `[1]+ Stopped`; `jobs` lists it; `fg`
  resumes. `^C` -> SIGINT reaches the foreground pgrp; the console
  survives.
- **`time true` faults at `RIP 0`** with `CR2 0`, `error 0x15`,
  `last syscall num 218`, `last sysret rcx 0x40FBC0`. The shell sees
  `Command terminated by signal 11` and returns to the prompt.
  **No `CR2=0x34` fault since `1bf2ca2`.**
- The C is not observed since the 2026-11-02 disk-path fixes; still
  open -- evidence, not a close.

### Next -- the highest-value work

1. The `0x40FBC0` probe (`hbreak`, `slot`/`rsp`/`fs`), then a
   watchpoint on `0x80000fbe28` with `$rip` if the slot is correct at
   sysret. See NEXT item 1 above for the decision tree.
2. `sys_set_tid_address` `tidptr` store + clear.
3. `sys_execve`'s `argv_scratch` 16 KB stack hazard -> `kmalloc`.
4. `umask` (95), `mkdir` EEXIST.
5. Item 10 SSE.
6. Item 9 privilege model -- **with the "is this worth a session"
   note.**

### Refuted -- do NOT re-walk

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
  instructions past it).
- **A `call *%rax` in `static_init_tls` at `0x40FBC0`**
  (`0x40FBC0` is the instruction after the `syscall`; `__init_tls`'s
  `ret` is `0x40FBF0`).
- **`kmain.c`'s self-tests map over the ELF text page.**
- **NEW 2026-11-04 evening:** the `cli` window; signal delivery;
  `memset`; the sysret slot arithmetic / user-RSP load; the "same
  process slot flips" premise; the three `exec_path` forms;
  `.bss`-not-zeroed; `0x448C6A`/`CR2=0x34` as the `time true` root
  cause; `lbb_prepare`/`run_applet_and_exit`/`applet_main[rbx]`.

### Open issues

The full list is `docs/open-issues.md`. The numbered items are 9 (no
privilege model) and 10 (no SSE save/restore). The un-numbered items
(7a, 7b, 7e, the `0x413962` `#PF`, the `.bss`-adjacent faults, the
small VFS/proc/pipe entries) are real and stay in that file. **The
2026-11-04 evening session added a new heading: "the child startup
fault is `RIP 0` after `set_tid_address`; the fault dumps were
misleading until `fb188f0`; `fs_base` was a separate bug fixed by
`1bf2ca2`."** The old "vfork child faults in musl startup" heading is
"closed for the parent-reap path" with a pointer to the new heading.

---

## State on disk

Kernel sources: `04_kernel_64bit/`. `capture.txt` is gitignored,
overwritten each `./run` -- **copy it aside after any failure or pass
you want to keep.** This session kept the last few; the earlier ones
are gone.

**After a fresh image, one manual step:** the image stages
`/bin/busybox` alone. To get the applet symlinks (including `/bin/sh`,
which shebang needs), boot and run `/bin/busybox --install -s /bin`.
The absolute path is required: busybox reads `/proc/self/exe` to find
itself, donix has no `/proc/self/exe`, and it falls back to `argv[0]`
-- which must start with `/`. This is a manual step, not part of the
build.

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

**The 2026-11-04 commits (21, on top of `9b62951`):**

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
- **the commit that added this file** -- docs: the evening handoff
  rewrite.

**Stash.** `stash@{0}`: "vfork 3b (wake arm + sys_vfork) +
diagnostics; time true intermittent, not a complete fix." This is a
preserved diagnostic trail. **Do not drop, do not pop.**

---

## One-line summary

**`v0.6.19` is released; `dev` is the commit that added this file
(parent `1bf2ca2`), ahead 21, clean, stash present, nothing pushed.**
The 2026-11-04 evening session landed two real kernel fixes:
**`fb188f0`** made `exception_frame_t` match the 20-slot stub frame
(every earlier `#PF`/`#GP` dump had printed GPRs as `rip`/`cs`/`rsp`,
which is why every earlier "fault site" was wrong), and **`1bf2ca2`**
restored `fs_base` on context switch (the `CR2=0x34`/
`__errno_location` fault is gone). **With an honest instrument,
`time true` has one signature left:** `RIP 0` / `CR2 0` /
`error 0x15` / `last syscall num 218` / `last sysret rcx 0x40FBC0`,
the `__init_tls` `ret` popping 0 from `0x80000FBE28`. **NEXT: the
`hbreak *0x40fbc0` probe printing `slot`/`rsp`/`$fs_base` -- read the
last line before the fault, then a watchpoint on `0x80000fbe28` with
`$rip` if the slot is correct at sysret.** Every earlier hypothesis is
in the Refuted list. Read `docs/contract.md` and this file's NEXT
before any code. **Do not move to `v0.6.20` until the user says so.**
