# handoff.md

Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-11-04, at `325e85b`.
**`v0.6.19` is the last pushed release.** `dev` = `325e85b`;
`origin/dev` = `ba56b3d`. Ahead **17**, clean tree, **no stash**.
No push, no tag this session; banner still `v0.6.19`.
`main` = `7e4748b` (the `--no-ff` merge of `v0.6.19`). Scratch tag
`20261104-gettimeofday-time` is on `5a3b65e` (still preserved).

**WHERE WE ARE.** The 2026-11-04 vfork session is complete through
the parent-reap path and has narrowed the remaining fault to the
applet-dispatch layer of busybox's startup. Seven commits were
kept, two were reverted, one revert commit landed. `time true`
now **wakes the parent, reaps the child in `wait4`, and prints a
wall time** -- the vfork parent-reap path works end to end. The
child's startup still faults at **`RIP 0` / `CR2 0` / `error
0x15` / `rax 0`** with the identity map's VA-0 page, and the
fault is narrowed to **`lbb_prepare` (`0x4139F6`) or
`run_applet_and_exit` (`0x413A93`)** in the exec'd busybox. **The
next session's first read is those two disassemblies.** Nothing
in the fault is vfork.

**WHAT LANDED (the seven keepers on `dev`).**

1. **`a885888`** `sys_vfork`: clear `parent->block_kind` and
   `parent->wait_pid` on the parent's resume. Without it the
   parent left `BLOCK_KIND_VFORK` set and was permanently
   skipped by `process_wake_all_blocked` for every later block.
2. **`3f81fb1`** `sys_vfork`: save the nine caller-saved GPRs
   (`rax..r11`) into the parent's PCB before `process_yield`,
   mirroring `console_read` / pipe read / pipe write.
   `context_switch.asm`'s ring-0 save restores only six
   callee-saved registers from its own prologue; a block site
   that did not save the other nine resumed with them zeroed.
3. **`001a756`** `sys_execve`: if `self->borrows_cr3` (a vfork
   child), build a fresh cr3 with `vmm_create_address_space`
   and switch `self->cr3` to it before `exec_free_and_unmap_
   user_pages`; clear `borrows_cr3`. Also calls
   `process_wake_parent_if_waiting(self)` after
   `self->entry_point = entry` -- the exec half of the vfork
   wake.
4. **`0a9ca16`** `process.c` `process_wake_parent_if_waiting`:
   accept `BLOCK_KIND_VFORK` when `wait_pid == child->pid`.
   This is what makes the exec-side wake and the child-exit
   wake actually wake a vfork parent.
5. **`a10e585`** `sys_execve`: write the auxv on the child's
   stack. musl's `__init_libc` walks past the `envp[]` NULL for
   it; without it `AT_PHDR`/`AT_PHNUM`/`AT_ENTRY`/`AT_RANDOM`
   are 0. Placement: `auxv_base = envp_array_base + envp_bytes`
   (immediately after the envp pointer array's NULL), `AT_RANDOM`
   above it, strings above that. `kmalloc`'d, not stack arrays
   (see the `argv_scratch` hazard below).
6. **`325e85b`** the revert: removes the twelve `_vf[...] = 0`
   register-zeroing assignments that V3 (`c1bd883`) and V3b
   (`f57299b`) had added to `sys_vfork`'s child frame.
7. (the reverts themselves, `c1bd883` and `f57299b`, are still
   in history -- see below.)

**WHAT WAS REVERTED, AND WHY.**

- **`c1bd883` (V3)** zeroed the vfork child's six callee-saved
  register slots. **`f57299b` (V3b)** extended it to the six
  caller-saved slots. **Both are reverted by `325e85b`.**
- The revert is the **Linux vfork contract**: a vfork child
  receives the parent's register file, and musl / busybox are
  written against it. Zeroing `r12` and `r10` broke the child of
  busybox `time`'s own `vfork`; `run_command`'s child resumed with
  `r12 = 0` and called through it (the `0x453DC5` fault).
- **The zeroing was masking `static_init_tls`, not fixing it.**
  With V3/V3b the fault moved to `0x453DC5`; with the revert it
  is back at the original site (`RIP 0` after
  `__init_tls`/`set_tid_address`). The underlying fault was
  never the register inheritance.

**WHAT THE AUXV PROVED.** With `a10e585` on top of the revert,
the stack dump shows the auxv on the child's stack in the right
place with the right values: `AT_PHDR = 0x400040`,
`AT_PHENT = 0x38`, `AT_ENTRY = 0x413882`, `AT_RANDOM` set,
`AT_NULL` terminator. **`__init_libc` reads it and proceeds.**
The `time true` fault is **not** a missing auxv, and the auxv
does not change the fault. The auxv commit is a real fix on its
own (musl reads a well-formed auxv instead of walking into the
argv strings) and it is gate-green; it is kept.

**WHAT WAS REFUTED THIS SESSION -- do NOT re-walk.**

- **"V3/V3b register zeroing is the fault."** Refuted by the
  revert: with the zeroing the fault was at `0x453DC5`; without
  it the fault is back at `RIP 0` / `__init_tls`. The zeroing
  was masking, not fixing.
- **"The missing auxv is the `time true` fault."** Refuted by
  the stack dump: the auxv is present and correct, and the
  fault is unchanged with it.
- **"`sys_set_tid_address` is the fault."** Refuted by the
  disassembly: `__init_tls` (`0x40FA40`) calls
  `set_tid_address(&0x46AA20)` at `0x40FBB4`, returns to
  `0x40FBC0`'s `ret`, `__init_libc` (`0x40B060`) takes the
  "not setuid" path (`AT_UID == AT_EUID`, `AT_GID == AT_EGID`,
  `AT_SECURE == 0`) and returns at `0x40B215`, and
  `libc_start_main_stage2` (`0x44860B`) calls `main` through
  `%r12 = 0x413DA6`. **None of that is a call through 0.**
  `sys_set_tid_address`'s `tidptr` gap is real and worth fixing
  on its own merits (open-issues entry), but it is not the
  `time true` fault.
- **"The fault is a `call *%rax` in `static_init_tls` at
  `0x40FBC0`."** Refuted: `0x40FA40` is **`__init_tls`**, not
  `static_init_tls`, and `0x40FBC0` is the instruction after
  the `set_tid_address` `syscall` -- `__init_tls`'s `ret`, not
  an indirect call.
- **"`kmain.c`'s self-tests map over the ELF text page."**
  Refuted: `kmain.c:550` maps `test_virt = 0x40000000` and
  unmaps it immediately; `kmain.c:707` maps a kernel VA at
  `0xFFFFFFFF82000000`. Neither touches `0x40FBC0`.

**NEXT -- highest-value work, in order.**

1. **`lbb_prepare` (`0x4139F6`) and `run_applet_and_exit`
   (`0x413A93`) disassembly.** The `call *%rax` with `rax = 0`
   is in one of them. `main` (`0x413DA6`) does
   `lbb_prepare` -> `bb_basename(argv[0])` ->
   `run_applet_and_exit(name, argv)`. If the applet table lookup
   returns 0, `main` calls through 0. The disassembly names
   whether the failure is the applet-name match (the exec'd
   `argv[0]` is wrong) or the table (the `.rodata` applet table
   is not mapped / is 0). That is the next commit.
2. **`sys_set_tid_address` `tidptr`.** Store the `tidptr` in
   the `pcb_t` (appended after `borrows_cr3`), and clear
   `*tidptr` in `process_exit` when non-zero. Small,
   self-contained, a real correctness fix on its own merits;
   **not** claimed to fix `time true`. The `g_syscall_stack_top
   - 120` frame base and the `_Static_assert` block in
   `process.c` are the offset constraints for the new field.
3. **`sys_execve`'s `argv_scratch[64][256]` = 16 KB on a 16 KB
   `PROC_STACK_SIZE` stack.** Pre-existing hazard. The first
   auxv attempt used `_auxv[30]`/`_rnd[2]` as stack arrays on
   top of it and produced a kernel `#GP error 0x2BE0` during
   `test.sh` (the iretq frame's SS slot got a stack address).
   The `kmalloc`'d auxv (`a10e585`) avoids it for the auxv, but
   `argv_scratch` is still the 16 KB landmine. **Move
   `argv_scratch` to `kmalloc` as its own commit**, with the
   same `kfree` discipline. Do **not** bundle it with anything
   else.
4. **`umask` (95) unimplemented.** `mkdir -p` fails with
   `Unknown syscall: 95`; `mkdir` without `-p` works. Small,
   self-contained, unblocks scripts.
5. **`mkdir` on an existing directory returns `EPERM` not
   `EEXIST`.** Busybox `tar -xvf` prints `can't create directory
   ... Operation not permitted` even though the tree extracts.
   Pair with item 4.
6. **`docs/open-issues.md` item 10 -- SSE save/restore.**
   Unchanged from the previous session.
7. **`docs/open-issues.md` item 9 -- the privilege model.**
   **Before starting, ask what it unlocks.** If "nothing," skip.

**NOT DONE, NOT THIS SESSION'S WORK.** The C halt
(`context_switch_resume`'s unknown-CS branch) has not been
observed since the 2026-11-02 disk-path fixes. No mechanism was
named. Do not chase it without new evidence.

---

## Working style -- how this project gets changed

Read this before proposing any command block. It is the same as
the previous session's, with the additions this session earned.

**One change at a time.** A "change" is a commit, verified by
**RUNNING** before the next starts -- build, run, see the
behavior, THEN commit. A build passing is not a test passing.

**The state check comes before the command block.** Every command
block starts with `git status -sb` (or `git log`).

**A command block is one paste -- write it to a file first.**
`git add`, `git status`, `git commit`, and `git tag` belong in
**one** block, with the message inline via heredoc. **Do not
paste a block with `cd ... || exit 1` into an interactive
shell** -- the `exit` closes the terminal. Write to
`/tmp/<name>.sh` with `cat > ... <<'SCRIPT_EOF'`, then run it as
a **separate** paste. Never paste a multi-line block with
trailing backslashes into an interactive shell. **`cd
"$HOME/..."` or a literal path in the script, never `cd
"$(dirname "$0")"` when the script is pasted.**

**A block that writes a file must actually be run as its own
paste.** Two steps, both explicit.

**Never put a destructive git command inside a block that also
runs diagnostics.** A `git checkout -- <file>` or `git restore`
in a block whose other lines are `git status`/`git log`/`grep`
is a trap. If a revert is wanted, name it on its own line, say
what it discards, and run it as its own paste.

**Python edits with `count != 1` abort checks are the preferred
edit style.** `set -e` at the top of the *script*. **Idempotent
edit scripts.** **Do the count check IN PYTHON (`str.count`),
not with `grep`.** `grep -Fc` does not do multi-line matches;
`grep -Fxc` requires a whole-line match; `str.count` is the only
honest multi-line count.

**BEFORE ASSERTING A COUNT, count the occurrences in the file
and write the expected number from that count, not from memory.**
**This session burned three turns on wrong check strings**
(`process_wake_parent_if_waiting(self)` counted 1 when 2 were
legitimate; `envp_data[EXEC_MAX_ENVC + 1];` counted without
`uint64_t`; the same in the revert). The pattern is the same
every time: the check string is retyped from memory, and a
prefix word or the indentation differs. **The rule: the
post-write check string is copied from the `new` literal, or
counted from `cat -A` of the actual file -- never retyped.**

**ANCHOR ON STRUCTURE UNIQUE TO THE TARGET FUNCTION -- AND ON A
STATEMENT BOUNDARY, NOT A FRAGMENT INSIDE A STRING LITERAL.**
Grep the file, confirm the count is 1 **and** the match is in
the intended function. Anchor from the file's current bytes
(`cat -A`), open `rb`/`wb` to preserve line endings. If an
anchor aborts, **ask for the current bytes** -- do not guess a
second anchor. The 2026-11-04 session avoided the previous
session's two anchor failures (the string-literal match, the
`o64 sysret` slice) by anchoring on full statements and by
appending rather than slicing.

**A post-write check string must be copied from the `new`
literal, not retyped.** (Same rule, stated twice because the
session earned it twice.)

**The build is TWO steps.** `make -C 04_kernel_64bit
FAT_CONFIG=single` builds the kernel; `make -C 05_boot_kernel64
hdd-single.img` builds the image. Running only the second copies
a STALE `kernel.bin`. Always run both, and verify the mtime
chain `source` < `.o` < `kernel.elf` < `kernel.bin` <
`hdd-single.img`. **After a session of probe edits and reverts,
run a full `make clean` before the next behavioral test.**

**The patch-script write changes the mode of the file it
writes.** A Python `open(..., "wb")` on the **same path** (not
`os.replace`) preserves the inode and the mode bits; `os.replace`
/ `rename` does not. If a patched file was executable,
`chmod +x` it back, or commit the mode change as its own
explicit commit. **Verify `stat -c '%a'` before and after every
patch script.**

**`nm` output moves between builds.** Re-run `nm` before using
any address. **And the symbol at an address may be the *next*
symbol, not the function the address is in** -- this session
read `0x40FA40` as `static_init_tls` when it is `__init_tls`,
because `nm -n` reported `static_init_tls` first. **The first
symbol `<=` the address is the enclosing function; the first
`>=` is the *next* function.** Verify both.

**A diagnostic on the SUCCESS path is noise. A diagnostic on the
FAILURE path is signal.**

**A probe that fires nothing is a result.** A silent probe and
a loud sibling together are the evidence. Do not treat a loud
sibling's numbers as corruption without the silent one.

**Test harness.** `./run` (no arg) = clean rebuild + fresh
`hdd-single.img` + `-enable-kvm -cpu host -display sdl`, no
freeze, gdb stub on `:1234`, `capture.txt` written (overwritten
each run -- **copy it aside after any failure or pass you want
to keep**; this session kept only the last few). `./run debug`
adds `-S` (frozen at reset). **`./run tcg` / `./run tcg-debug`**
= the same flow in TCG (`-cpu max`, no `-enable-kvm`). The
Makefile run targets also use `-cpu max` + TCG -- **state which
launcher was used.** Default cwd `/root`.

**In-guest gate.** `cd /`, then `canary --full` (28/28) and
`/root/scripts/test.sh` (57/57). **Use multiple `^Z`/`fg`
cycles.** One is not always enough to reach an intermittent
fault.

**Perf kvm stat, GDB raw offsets, and the Fedora 44 perf
`double free`** are unchanged from the previous session's notes.

---

## Releases

**Before a release:** the scratch `2026MMDD-*` tags are annotated
and are the source for the version summary. **The scratch tags
are PRESERVED until the user decides to push to GitHub, and are
dropped ONLY after the summary is written and reviewed by the
user.** A session does NOT drop them on its own initiative.

**The release procedure** (on `dev`, pre-release):

1. **Patch the docs first.** Update this file's release
   paragraph and (optionally) `docs/version-history.md`. Commit.
2. **Bump the version string in `kmain.c` manually** (the
   banner). Commit.
3. **Tag** `vX.Y.Z` annotated, on `dev`, summary from the
   scratch tags' annotations and `vPREV..HEAD`:

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
unpushed `dev` commit is intentional work. Run `git log --oneline
origin/dev..dev | wc -l` for the current count.

**Current release state.** **`v0.6.19` is the last pushed
release.** `dev` = `325e85b`, ahead **17**, clean, no stash.
`origin/dev` = `ba56b3d`. Scratch tag
`20261104-gettimeofday-time` on `5a3b65e`. Work is now
pre-`v0.6.20`. **There is no release gate recorded for
`v0.6.20`; if a gate is wanted it is set in this file before
that release session, not during it. The user has said not to
move to `v0.6.20` until they say so.**

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static
musl-linked binaries on the Linux x86_64 ABI, natively. Newlib
is gone. Userland is a tracked source tree at
`userland/musl/`. Phase B (busybox) is well underway: the shell
is interactive, pipelines and redirection work, job control
works (`^C`, `^Z`, `jobs`, `fg`), and the applet set includes
`bc`, `dc`, `diff`, `du`, `awk`, `date`, `uptime`, `time` in
addition to the file/text applets. **`tar` creates, lists and
extracts correct archives, including directory members.**
`date`/`uptime` report seconds since boot as a 1970-epoch date
(no RTC -- the honest base). **`time` builds, its vfork
parent-reap path works (parent wakes, `wait4` reaps, a wall
time prints), and its child's startup still faults at the
applet-dispatch layer -- see the open-issues entry.**

Full strategy: `docs/strategy.md`. Direction: `ROADMAP.md`.
Concurrency rules: `docs/contract.md`. Open issues:
`docs/open-issues.md`.

---

## Where we are

### Current baseline

- `test.sh` **57/57**, `canary --full` **28/28** at every gate
  point this session, on a fresh boot, after every commit and
  the revert. **Re-run both on a fresh boot before trusting
  them.**
- `date` prints `Thu Jan 1 00:00:30 UTC 1970` (30 s uptime),
  `uptime` prints `up 0 min`, load `0.00` -- correct for donix.
- `mktemp` works (both `/tmp/tmp.XXXXXX` forms).
- `^Z` on a foreground child -> `[1]+ Stopped`; `jobs` lists it;
  `fg` resumes. `^C` -> SIGINT reaches the foreground pgrp; the
  console survives.
- **`time true` wakes the parent, reaps the child, prints a
  wall time, and the shell survives** -- the vfork parent-reap
  path is complete. The child still faults at `RIP 0`
  (`lbb_prepare` / `run_applet_and_exit`); the shell sees
  `Command terminated by signal 11` and returns to the prompt.
- The C is not observed since the 2026-11-02 disk-path fixes;
  still open -- evidence, not a close.

### Next -- the highest-value work

1. `lbb_prepare` (`0x4139F6`) and `run_applet_and_exit`
   (`0x413A93`) disassembly -- the `call *%rax` with `rax = 0`.
2. `sys_set_tid_address` `tidptr` store + clear.
3. `sys_execve`'s `argv_scratch[64][256]` = 16 KB stack hazard
   -> `kmalloc`, as its own commit.
4. `umask` (95), `mkdir` EEXIST.
5. Item 10 SSE.
6. Item 9 privilege model -- **with the "is this worth a
   session" note.**

### Refuted -- do NOT re-walk

- **The `efc8e08` rsp/irq0_rsp split, and `frame_kind`.**
- **The four uncommitted scheduler guards.**
- **The `EXIT-REFUSE frame-CS` guard in `process_exit`.**
- **The canary `.text` `#PF` probe stack.**
- **The physical-address gates on `.text` PTE zeroing.**
- **The `f91d31a` "scheduler guards fix canary/pipe7e/ctrl-Z"
  claim.**
- **`exec_churn`'s KVM slowness is the console / the serial
  poll loop / the VGA cursor CRTC writes.**
- **`ata_poll_drq` spinning.**
- **`exec_churn` can measure the `sys_fork` eager-copy cost.**
- **The `perf kvm stat record` `double free` is a donix issue.**
- **The 2026-11-04 PTAUDIT "22 anomalies."**
- **The 2026-11-04 empty-low-half `vmm_create_address_space`.**
- **NEW this session: V3/V3b register zeroing is the `time
  true` fault** (refuted by the revert -- it was masking
  `__init_tls`).
- **NEW: the missing auxv is the `time true` fault** (refuted
  by the stack dump -- the auxv is present and correct, and the
  fault is unchanged with it).
- **NEW: `sys_set_tid_address` is the `time true` fault**
  (refuted by the disassembly -- `__init_tls` returns cleanly
  after it, `__init_libc` takes the not-setuid path, and
  `libc_start_main_stage2` calls `main` through `%r12 =
  0x413DA6`).
- **NEW: the fault is a `call *%rax` in `static_init_tls` at
  `0x40FBC0`** (refuted: `0x40FA40` is `__init_tls`, and
  `0x40FBC0` is the instruction after the `set_tid_address`
  `syscall` -- `__init_tls`'s `ret`).
- **NEW: `kmain.c`'s self-tests map over the ELF text page**
  (refuted: `kmain.c:550` maps `0x40000000` and unmaps it;
  `kmain.c:707` maps `0xFFFFFFFF82000000`).

### Open issues

The full list is `docs/open-issues.md`. The numbered items are 9
(no privilege model) and 10 (no SSE save/restore). The
un-numbered items (7a, 7b, 7e, the `0x413962` `#PF`, the
`.bss`-adjacent faults, the small VFS/proc/pipe entries) are
real and stay in that file. **The 2026-11-04 session added a
new heading: "busybox applet dispatch faults after exec (open,
next session's first work)", with the `lbb_prepare` /
`run_applet_and_exit` candidate. The old "vfork child faults in
musl startup" heading is now "closed for the parent-reap path"
with a pointer to the new heading.**

---

## State on disk

Kernel sources: `04_kernel_64bit/`. `capture.txt` is gitignored,
overwritten each `./run` -- **copy it aside after any failure or
pass you want to keep.** This session kept the V3b, auxv, and
revert captures; the earlier ones are gone.

**After a fresh image, one manual step:** the image stages
`/bin/busybox` alone. To get the applet symlinks (including
`/bin/sh`, which shebang needs), boot and run
`/bin/busybox --install -s /bin`. The absolute path is
required: busybox reads `/proc/self/exe` to find itself, donix
has no `/proc/self/exe`, and it falls back to `argv[0]` --
which must start with `/`. This is a manual step, not part of
the build.

**`third_party/`** is **gitignored** -- do not commit edits to
it. `third_party/busybox/` has `busybox` (stripped) and
`busybox_unstripped`; the `.map` file is **not** present, so
`nm -n` + `objdump -d` are the symbol tools, and `readelf -lW`
gives the program headers. `third_party/musl-install/` is
gitignored and is needed for the build. If it's missing after
a `git clean -xdf`, copy it back from a working tree, or
rebuild it with `toolchain/install_musl.sh`.

**`04_kernel_64bit/fatfs/ff.c`** is tracked and has two
`sync_window` fixes; any further edit there must re-run the full
gate.

**`~/code/saved`** -- not a baseline. Run
`git -C ~/code/saved status --short` before any comparison.

**The reset baseline.** `dev` was reset to `873be27`, tag
`reset-baseline-873be27`. `efc8e08` is not on `dev`; it survives
as a commit object. Treat its guards as **refuted**.

**The 2026-11-02 performance and correctness commits** (six, on
top of `f902af6`): `78216bc`, `ea4dc2e`, `e52bd2f`, `905bc70`,
`fdbcbc7`, `5db42f2`, `6df4fef`.

**The 2026-11-03 release commits:** `c0126d4`, `28272f9`,
`b0e9cce`, `9a36e76`, `ef090f4` (docs), and `ba56b3d` (version
banner `v0.6.19`, tagged).

**The 2026-11-04 commits (17, on top of `9b62951`):**

- `5a3b65e` -- syscalls 96/201, tagged
  `20261104-gettimeofday-time`.
- `ea0986e` -- `borrows_cr3` guard.
- `dca2eff` -- busybox config: `DATE`, `UPTIME`, `TIME`.
- `1aaca0d` -- `sys_vfork` (58), block only.
- `6ca941b` -- `vmm_create_address_space` (first version,
  superseded).
- `c652c8f` -- `vmm_create_address_space` clone-then-unmap.
- `e5081e1` -- docs: record the open vfork-child fault.
- `8514fca` -- docs: the previous handoff rewrite.
- `a885888` -- `sys_vfork`: clear `block_kind`/`wait_pid` on
  resume.
- `3f81fb1` -- `sys_vfork`: save the nine caller-saved GPRs
  before the parent blocks.
- `001a756` -- `sys_execve`: fresh cr3 for the vfork child +
  exec wake.
- `c1bd883` -- `sys_vfork`: zero the child's callee-saved GPRs.
  **Reverted by `325e85b`.**
- `0a9ca16` -- `process.c`: accept `BLOCK_KIND_VFORK`.
- `f57299b` -- `sys_vfork`: zero the child's caller-saved GPRs.
  **Reverted by `325e85b`.**
- `a10e585` -- `sys_execve`: write the auxv.
- `325e85b` -- revert of `c1bd883` + `f57299b`.

**No stash.** The `stash@{0}` from the previous session was
consumed by the commits above; there is nothing stashed.

---

## One-line summary

**`v0.6.19` is released; `dev` is at `325e85b`, ahead 17, clean,
no stash, nothing pushed.** The 2026-11-04 session shipped the
**vfork parent-reap path end to end** (seven keepers: V2 clear,
V1 save, exec cr3 swap, wake arm, auxv) and **reverted the
V3/V3b register zeroing** because Linux's vfork gives the child
the parent's registers and the zeroing was masking
`__init_tls`, not fixing it. **`time true` now wakes the
parent, reaps the child, prints a wall time, and the shell
survives; the child still faults at `RIP 0` after the exec'd
busybox's `_start`.** The auxv is present and correct in the
stack dump and does not change the fault. Five hypotheses were
refuted this session and are listed above. **NEXT: the
`lbb_prepare` (`0x4139F6`) and `run_applet_and_exit`
(`0x413A93`) disassembly -- the `call *%rax` with `rax = 0` is
there.** Then `sys_set_tid_address` `tidptr`, then
`argv_scratch` -> `kmalloc`, then `umask`, `mkdir` EEXIST, SSE,
privilege model. Read `docs/contract.md` and this file's NEXT
before any code. **Do not move to `v0.6.20` until the user says
so.**
