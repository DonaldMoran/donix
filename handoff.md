Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-09 (the 7e control-transfer fault is an
**UNSOLVED RACE** -- not memory corruption.  It reproduces
byte-identically at 100 Hz, and the kernel's syscall return path is
CLEARED.  **No bump.**  The tree is dirty with this session's
diagnostic, one scratch `pit_init` change, and three docs edits; see
"Repo state" below.)

**THE NEXT SESSION WORKS ON THE RACE, NOT ON MEMORY CORRUPTION.**
A race presents as a wrong value in a slot, and there is **no store
to find** -- the wrong value is the correct value for a different
interleaving.  **Do not hunt the writer.  Instrument the
ordering.**  The instrument is named under "NEXT SESSION".

**Repo state -- run these; do not write it here.**  A header that
names a commit or a tag count is wrong the moment the same commit
lands, so this file does not carry one:

    git log --oneline -1            # HEAD
    git status -sb                  # branch, dirty?, ahead/behind
    git tag --list 'v*' | tail -1   # last milestone
    git tag --list '2026*'          # live scratch tags

**This session's uncommitted tree changes:**

- `04_kernel_64bit/interrupts.c` -- `g_user_rsp_save` printed in
  both `bad_target` blocks; user-stack window widened from
  `-0x40..+0x40` to `-0x200..+0x200`.  **This is the instrument;
  commit it.**
- `04_kernel_64bit/user_syscall_entry.asm` -- `global
  g_user_rsp_save`.  **Same change; commit with the above.**
- `04_kernel_64bit/kmain.c` -- `pit_init(100)` -> `pit_init(10)`,
  a scratch change for an end-of-session watch run.  **Not part of
  the instrument.  Revert before committing the diagnostic, or set
  it deliberately.**  The 100 Hz build is the one the fault
  reproduces on.
- `docs/open-issues.md`, `docs/gotchas.md`, `docs/session-log.md`
  -- this session's documentation.  Commit with or after the
  instrument.

> **The rule this file carries forward, from session 52: an item
> here that quotes an expected value is a claim.**  A claim read
> off a trace or a comment has cost this project sessions.  **Read
> the walk, read the disassembly, read the function -- not just
> its output.**  Session 62 added a new form of the same rule: **a
> session number, a comment, and an address pairing are claims too
> -- count it, read the body, and quote the bytes.**  This session
> wrote "Session 62" before verifying the count, paired two `ret`
> addresses as one function when they are two, and wrote two
> anchors from memory that failed to match.

**The version history, in one line each:** `v0.6.6` pipes; `v0.6.7`
a real shell and a framebuffer console; `v0.6.8` the `*at()` family;
`v0.6.9` envp, the `/usr/bin` layout, the `execve` shim removal;
`v0.6.10` a tail on `v0.6.9`; `v0.6.11` the pathname dispatch seam,
`/proc` per-pid and `ps`, and the PMM zone-scan fix; **`v0.6.12` a
correctness milestone** -- item 7 closed, `process_create`'s failure
exits fixed, the exit-path page-table leak closed.  **No new
subsystem in `v0.6.12`.**

**The last `v*` tag is read from git, not from this file.**  The
work after the tag is on `dev`, unpushed: the applet batches and the
harness; item 7c retracted; the zombie leak and the NX /
non-canonical family; the fork-path sentinel leak; the `isr14`
phys-mask fix; the vector-6 `#UD` handler; the `exit_group`
termination fix; `mmap_nx` and `sigsegv_probe`; the ELF loader's
`PF_X` and page-0 fixes; session 59's ash-side 7e mechanism found by
reading disassembly and the `signal_maybe_redirect` re-entry guard;
the session-61 kernel fixes; and **session 62's `g_user_rsp_save`
instrument and wide user-stack window.**  The bump is a whole
procedure; see below.  **It is a decision, not a default.**

Commits are named by tag only, never by SHA.  **Working tags
(`YYYYMMDD-*`) are local scratch restore points** -- they exist while
a milestone is being developed and are **dropped before the
milestone is pushed**.  Only `v*` tags go to the remote and are
permanent.  The commit record is `docs/session-log.md`; the commit
message carries the narrative.  Once a scratch tag is dropped it
resolves to nothing; do not cite one as if it were a stable
reference.

**A plain commit can exist with no tag.**  Such commits ride along
on `dev` until the next `v*` bump.  Do not invent a tag for one.

**Note on commit messages, and on this file too:** a commit message
is a claim, not a fact.  Read the diff, not the subject.  **This
file's item text is the same kind of claim.**

---

## Working style -- how this project gets changed

Read this before proposing any command block.

**One change at a time.**  A "change" is a commit, verified before
the next one starts.

**The state check comes before the command block.**  Every commit
block starts with `git status` (or `git log`).  This is a rule
because it was learned: a `git commit` was once written against an
assumed state and landed wrong.

**Check the directory, too.**  Start every command block with
`cd /home/noneya/code/donix || exit 1` and a `pwd`.

**A command block is one paste.**  `git add`, `git status`,
`git commit`, and `git tag` belong in **one** block, with the commit
message inline via heredoc.  Do not split a single operation.

**Two file-return styles.**

- **Small files:** return the complete file, four-backtick fenced.
- **Large files:** return a unified-diff hunk or a copy-block
  insertion, preceded by a bold **`WORK BEGINS HERE.`** marker.
  The marker is load-bearing.

  **A whole-file return for a large file with non-ASCII content
  re-encodes every non-ASCII character and shows up as a large
  deletion count.**  For an **insertion**, check `git diff --stat`:
  it should show **zero deletions**.  A full **replacement** of a
  section correctly shows both.  **A third shape: a Python edit
  with `count != 1` abort checks, which refuses to write if the
  anchor doesn't match exactly once.**  Session 62 used it six
  times and it caught two bad anchors and wrote nothing both times.

**Ask for source you do not have.**  The assistant does not have
direct file access.  Before patching a file it has not seen **in
this session**, it must ask for that file.  Never guess.  **Session
62's corollary: before writing an anchor, read the file with
`cat -A` and quote the bytes.**  Two anchors failed because they
were written from memory of a paste.

**The channel is the human.**  A session asks for a file by name
and the human pastes it; a session asks for a command's output the
same way -- a read-only block, one paste, and the state check
first.

**When editing a large file, quote the bytes.**  For an edit inside
a big file, the instruction must quote the exact text being replaced
and the exact text that replaces it, both copied from output the
other side just pasted.

**A redirection binds to the last command in an `&&` chain.**

**A capture file is one run.  Truncate, do not append.**

**A build flag change does not trigger a rebuild unless the
Makefile is a prerequisite.**

**Do not edit `third_party/`.**  The vendored sources there are
gitignored and rebuilt by the toolchain.  **But read them freely** --
session 59's mechanism was found by disassembling
`third_party/busybox/busybox_unstripped`; session 62 read
`userland/musl/build/pipe7e.elf`'s disassembly the same way.
Read-only is the point.

**Hand-written `@@` hunk headers are a trap.**  Edit the file and
let `git diff` produce the patch.

**A diagnostic that runs on the fault path is part of the system.**
Session 58's gotcha, still true.  Session 62's complement: a
diagnostic inside the `bad_target` guard prints only for the shapes
it is for, which is the right scope.

**The push / merge / tag sequence** (used for every `v*` bump):

    git tag -a vX.Y.Z -F - <<'EOF'
    <annotation>
    EOF

    git push origin dev && \
    git push origin vX.Y.Z && \
    git checkout main && \
    git merge --no-ff dev -m "Merge dev into main for vX.Y.Z

    <merge narrative>" && \
    git push origin main && \
    git checkout dev && \
    git status && \
    git log --oneline -3

**Before this:** the banner bump is the **last** code commit; the
session's `YYYYMMDD-*` scratch tags are **dropped**; and the
session-log and handoff are written.  The tag is **annotated**; the
merge is `--no-ff`.

**A version is not necessarily a milestone.**  **`v0.6.12` is a
correctness milestone.**  **`v0.6.13` is a candidate.**  The bump is
a **decision**, not a default.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 syscall ABI, natively. Newlib is gone.
Userland is a tracked source tree at `userland/musl/`. Phase B
(busybox) is well underway.

Full strategy: `docs/strategy.md`.  Direction: `ROADMAP.md`.

---

## Tree on disk

One working copy.

- **Real project:** `/home/noneya/code/donix/`.  Tracked repository,
  source of truth, where the `v*` tags live.

The kernel C sources live in `04_kernel_64bit/`.  Verify by file
name before editing.  `05_boot_kernel64/` holds the boot chain
assembly and the image builder.

Scratch workspaces are **transient**.  Do not port from one without
a build and test in the real tree.

**Run command blocks from the repository root.**

---

## Where we are -- after session 62

**The 7e control-transfer fault is an UNSOLVED RACE.  The
tick-rate curve is the proof and it has been confirmed live:** at
`pit_init(10)` the same kernel and image run past 3000 `pipe7e`
iterations clean; at `pit_init(100)` the same binary faults within
the first iterations.  A deterministic defect produces the same
outcome at every rate.  This one does not.

**A race is not memory corruption, and this is the session's
headline.**  A race presents as a wrong value in a slot, and there
is **no store to find** -- the wrong value is the correct value for
a different interleaving.  Six mechanisms this session were writer
hypotheses and **all six were refuted by reading the source.**
**Instrument the ordering, not the store.**

### What session 62 established, from bytes

**1. The kernel's syscall return path is CORRECT.**  Three runs,
`RSP - g_user_rsp_save == 8` -- exactly the relation for a `ret`
immediately after a `syscall`.  The kernel returns to the correct
user RIP (`last sysret rcx = 0x4041F8`, `__syscall_cp_c`'s `ret`)
and the correct user RSP.  **The corruption is in the user-stack
CONTENTS, present before the `ret` executes.**  This was unknown
before this session; the `g_user_rsp_save` line produced it.

**2. The faulting frame sits in the STARTUP region of the stack.**
The wide user-stack window shows `"Pe7epip"` / `"WD=/root"` (argv/
envp strings) at `rsp+0x70`/`0x78`, `main`'s entry `0x400220` at
`rsp-0x18`, and a **doubled saved frame** at `rsp-0x20` and
`rsp-0x1C0`.  A frame in the startup region, with a function entry
as a return address and the same values saved twice, is the shape
of a frame built by a path that **sets up an initial frame** -- the
`fork` child setup.

**3. `pipe7e.elf` links NO `longjmp`/`setjmp`.**  The disassembly
shows no such call.  **The handoff's long-carried `_longjmp(buf,
1)` mechanism is not this binary's.**  The `jmp_buf` shape belongs
to the ash faults; `pipe7e` has none.

**4. `0x400CF2` and `0x4041F8` are two DIFFERENT thunks' post-
syscall `ret`s.**  `0x400CF2` is `__restore_sigs`'s `ret` after
`rt_sigprocmask` (syscall 14); `0x4041F8` is `__syscall_cp_c`'s
`ret`.  The item text paired them as one function; corrected.

**5. `__syscall_cp_c` is a bare syscall thunk** -- no cancellation
branch.  `user_syscall_entry.asm`'s return-tail arithmetic traces
correct (`rsp`, `rip`, `rflags` all read from the right slots).

### The fault's shape

Every 100 Hz fault: `CR2 = RIP = 0xFFFFFFFF`, error `0x14` (user,
instruction fetch), `RSP = 0x80000FBF90`,
`g_user_rsp_save = 0x80000FBF88` (difference `0x8`), CR3
`0x32D000`, pid 4 `pipe7e`.  **Three runs, byte-identical.**

### Refuted -- do NOT re-walk

Six mechanisms, each a writer hypothesis, each killed by reading
the source:

- **`longjmp`/`setjmp`** -- not in `pipe7e.elf` (disassembly).
- **Cancellation unwind** -- `__syscall_cp_c` is a bare thunk.
- **Signal frame** -- signals are not delivered
  (`user_syscall.c:5387,5392,6512,7604`).
- **The `wait4` re-scan window** -- the re-scan runs **inside** the
  `cli`; the comment at `user_syscall.c:5210` saying it is
  "preemptible" is **stale**.
- **`dup2` refcount undercount** -- `sys_dup2` **does** bump
  `old_slot->refcount`.
- **Exit-path fd leak / pipe use-after-free** -- `close_all_files`
  uses `put_file_slot`; `put_pipe_ref` is gated on
  `readers_open`/`writers_open`, and the comment says the stamp
  probe proved that gate necessary.

Also do not re-open: item 7 (closed), item 7c (retracted), item 11
(closed), the session-48 zone scan, the `fs_base` path (correct at
every fault, three values agree at `0x408698`).

### The instrument now in the tree

- `g_user_rsp_save` printed in both `bad_target` blocks
  (`interrupts.c`).
- `g_user_rsp_save` declared `global` in `user_syscall_entry.asm`.
- The user-stack window is `-0x200 .. +0x200`.
- The P13/P12/P11 probes, the P1/P8 probes, `RACE_PROBE_*` gates.

---

## NEXT SESSION

**The lead is the RACE.  The method is: instrument the ordering.**

1. **The `fork` stack-copy ordering.**  Print `parent->rsp`,
   `parent->user_stack_virt`, `parent->user_stack_top` at fork time
   and compare against `child->rsp`.  The copy is
   `EAGER_COPY_REGION(parent->user_stack_virt, parent->user_stack_top)`
   at `user_syscall.c:7940`; the two bounds are set once
   (`process.c:311/366`, `user_syscall.c:4407/4455`) and are **not
   updated as the stack grows**.  If the parent's `rsp` at fork
   time falls outside the copied range, the child has no mapping
   where the parent's `rsp` points, and its first `ret` after a
   syscall reads `0xFFFFFFFF`.  **Two numbers, not a store hunt.**

2. **The same-point rate test.**  Run 10 Hz to 50000 iterations.
   Fault at the same point as 100 Hz -> deterministic; pass -> the
   race stands.  Either result is a finding.

3. **The open question, named not resolved:** why does the race
   reproduce byte-identically?  A race can do that when one
   ordering wins nearly every time.  The ordering instrument
   answers it if the losing order is always the same.

### The candidates, in order

1. **The residual 7e race.**  The lead.  Ordering, not stores.
2. **Item 12 -- signal delivery.**  Largest; unblocks `kill`, ash
   job control, the Wayland `SIGBUS` path.  A workstream.
3. **`sys_gettimeofday` (96).**  Small, patterned, independent.
   Unlocks `ps -l`.  **Note: 96, not 99.**

### Do not

**Do not chase memory corruption** -- it is a race.  **Do not point
at musl or busybox** as the culprit binary.  **Do not re-derive the
`fs_base` path** -- it is correct.  Do not re-open item 7, item 11,
or the session-48 zone scan.  Do not re-run the two `setjmp`
probes.  Do not trust a page walk's verdict without reading the
walk.  Do not drop the older non-`2026*` tags.  One change at a
time.  Do not edit `third_party/` -- but do read it.

---

## Open issues (top 6; full list in `docs/open-issues.md`)

1. **`open-issues.md` item 7e: the control-transfer face.**  An
   **UNSOLVED RACE**, proven by the tick-rate curve, reproduced
   byte-identically at 100 Hz.  **The kernel return path is
   cleared; the corruption is user-stack contents.**  **The lead.**
2. **`open-issues.md` item 12: signal delivery is a stub.**  The
   largest single target.
3. **`open-issues.md` item 10: `CR4.OSFXSR` without an XMM save.**
4. **`open-issues.md` item 13: `CONFIG_ASH_JOB_CONTROL` gap.**
5. **`open-issues.md` item 9: no privilege model.**
6. **`sys_gettimeofday` (96)** so `ps -l` works.  Small.

**Item 7 closed; item 7c retracted; item 7d fixed; item 11 closed.**
**Item 7e: a race, residual window open.**

## State on disk

**Real project root:** `/home/noneya/code/donix/`.

**The FAT layout:**

    /            HELLO-WORLD.TXT and other data
    /bin         busybox
    /usr/bin     the donix-native ELFs, staged BARE
    /tmp         empty
    /etc         passwd, group
    /root        scripts/test.sh, scripts/pipe_wake_probe.sh
    /home        empty
    /dev         empty (a real FAT directory)
    /var         empty

- `configs/busybox.config` -- tracked canonical config.
- `userland/musl/` -- tracked musl userland.  Session 59 added
  `tests/pipe7e_stdio_sjlj.c`, `tests/pipe7e_stdio_heapjmp.c`,
  `tests/pipe7e_sjlj_stale.c`.
- `userland/scripts/` -- `test.sh`, `pipe_wake_probe.sh`.
- `third_party/{busybox,musl-src,musl-install}/` -- gitignored;
  **read freely, do not write.**
- `toolchain/{install_musl.sh,musl-gcc.sh}` -- tracked.
- `capture.txt` -- gitignored, truncated per run.  Captures
  preserved outside the tree survive a rebuild.
- `run` -- tracked; the build-and-capture fix.

Kernel sources: `04_kernel_64bit/`.

---

## Where things live

Not needed to start a session; ask for a file when the task needs
it.

- `docs/strategy.md`, `docs/gotchas.md`, `docs/session-log.md`,
  `docs/open-issues.md`.
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md`.
- `ROADMAP.md`, `README.md` -- review at the next `v*` bump.
- `run`.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
The userland is a tracked source tree at `userland/musl/`.  `v0.6.13`
is the last tag; no bump.  The `testme` migration and the session-61
kernel fixes are on `donix`'s `dev`, unpushed.  Session 62's finding:
**the 7e control-transfer fault is an UNSOLVED RACE** -- 10 Hz clean
past 3000 iterations, 100 Hz faults within the first, three runs
byte-identical.  **The kernel's syscall return path is CLEARED**
(`RSP - g_user_rsp_save == 8`); the corruption is user-stack
contents.  **A race is not memory corruption -- there is no store to
find.  Do not hunt the writer; instrument the ordering.**  The next
instrument is the `fork` stack-copy range: two numbers, not a store
hunt.  One change at a time.**

## How to use this file

At session start, paste this file and say "Continue from here and
request any files you need."  Run the four `git` commands -- they
are the state; this file is the narrative.

At session end, **rewrite the narrative.**  Do **not** write repo
state.  New gotchas go to `docs/gotchas.md`; new commit rows go to
`docs/session-log.md`; new open issues go to `docs/open-issues.md`.
This file never grows.  Name commits by tag only.

**When a session's findings change an earlier numbered step, edit
the step in place.**

**Before proposing any command block, read the "Working style"
section at the top.**

**Gotchas worth reading before the next change.**  "A fix with no
test is indistinguishable from an unfixed defect."  "A test can
encode an earlier version's behavior."  "A consumer inferred from
behavior is not a consumer."  Session 49's: **a function that has
never run is correct by inspection only.**  Session 51's two: **a
whole-file return for a large non-ASCII file re-encodes it**; and
**print the row marker before the row runs.**  Session 52's: **a
test's expected value is a claim.**  Session 53's: **a diagnostic
declared but never wired is not a diagnostic.**  Session 54's two:
**a halt in a fault handler is a diagnostic that never runs**; and
**put cleanup in the teardown, not each entry point.**  Session
55's two: **a fork that overwrites a child's file table leaks per
fork**; and **a captured fault is not a diagnosed one.**  Session
57's two: **hand-written `@@` hunk headers are a trap**; and **a
type's named fields are not what the pointer points at.**  Session
58's two: **a page walk that descends through a supervisor level
prints a confident wrong verdict**; and **a diagnostic on the fault
path is part of the system.**  Session 59's three: **a Python edit
with a `count != 1` abort check catches an ambiguous anchor and
writes nothing; a handler on the fault path with a corrupt `rsp`
cannot run, so a redirect without a re-entry guard loops forever;
and reading the disassembly found the mechanism a dozen
trace-readings had missed.**  **Session 59's meta-gotcha: a section
title is a claim about its body, and a commit subject is a claim
about its diff -- read the body, read the diff.**  **Session 62's
three: a race is not memory corruption -- there is no store to
find, so do not hunt the writer; a session number, a comment, and
an address pairing are claims too -- count it, read the body, and
quote the bytes; and before writing an edit anchor, read the file
with `cat -A` and quote the bytes.**  **This file's item text is
the same kind of claim.**
