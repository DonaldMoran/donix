Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-09 (the 7e family is
a RACE, proven by a tick-rate curve; the `testme` migration landed
on `donix`; five race windows closed; the session-61 kernel fixes
cherry-picked and corrected.  **No bump.**  The tree is clean and
unpushed, and the next session works on `donix` directly.

**The next act is the 7e RACE.**  Not a bump, not item 12 as the
lead.  The instrument is the tick-rate sweep plus the P13/P12/P11
probes.  See "Where we are" and "NEXT SESSION" below.)

**Repo state -- run these; do not write it here.**  A header that
names a commit or a tag count is wrong the moment the same commit
lands, so this file does not carry one:

    git log --oneline -1            # HEAD
    git status -sb                  # branch, dirty?, ahead/behind
    git tag --list 'v*' | tail -1   # last milestone
    git tag --list '2026*'          # live scratch tags

> **The rule this file carries forward, from session 52: an item
> here that quotes an expected value is a claim.**  Session 51
> filed item 7c as "`sha512sum` computes a WRONG digest for
> `ABC`" with a value that turned out to be the SHA-512 of
> lowercase `abc`.  Session 52 spent its time reading correct
> code.  Sessions 53 through 58 falsified mechanism after
> mechanism, each a plausible story read off a trace, each killed
> by reading the source.  **Session 59's method was the
> counter-example that paid off: every claim was checked against
> the source, and the mechanism was found by *reading the
> disassembly*, not by reasoning from a trace.**  The greps cost
> seconds; the wrong readings cost sessions.  Read the walk, read
> the disassembly, read the function -- not just its output.

**The version history, in one line each:** `v0.6.6` pipes; `v0.6.7`
a real shell and a framebuffer console; `v0.6.8` the `*at()` family;
`v0.6.9` envp, the `/usr/bin` layout, the `execve` shim removal;
`v0.6.10` a tail on `v0.6.9`; `v0.6.11` the pathname dispatch seam,
`/proc` per-pid and `ps`, and the PMM zone-scan fix; **`v0.6.12` a
correctness milestone** -- item 7 closed, `process_create`'s failure
exits fixed, the exit-path page-table leak closed.  **No new
subsystem in `v0.6.12`.**

**The last `v*` tag is read from git, not from this file.**  The
work after the tag is on `dev`, unpushed: the applet batches and the harness; item
7c retracted; the zombie leak and the NX / non-canonical family; the
fork-path sentinel leak; the `isr14` phys-mask fix; the vector-6
`#UD` handler; the `exit_group` termination fix; `mmap_nx` and
`sigsegv_probe`; the ELF loader's `PF_X` and page-0 fixes; and
**session 59's headline -- the ash-side 7e mechanism found by
reading and reproduced, and the `signal_maybe_redirect` re-entry
guard.**  The bump is a whole procedure; see below.  **It is a
decision, not a default.**

Commits are named by tag only, never by SHA.  **Working tags
(`YYYYMMDD-*`) are local scratch restore points** -- they exist while
a milestone is being developed and are **dropped before the
milestone is pushed**.  Only `v*` tags go to the remote and are
permanent.  The commit record is `docs/session-log.md`; the commit
message carries the narrative.  Once a scratch tag is dropped it
resolves to nothing; do not cite one as if it were a stable
reference.

**A plain commit can exist with no tag.**  Session 45's `run:` fix
is one.  Later docs commits are the same.  Such
commits ride along on `dev` until the next `v*` bump.  Do not invent
a tag for one.

**Note on commit messages, and on this file too:** a commit message
is a claim, not a fact.  Read the diff, not the subject.  **This
file's item text is the same kind of claim.**  Session 52 exists
because session 51's item 7c was believed; sessions 53 through 58
falsified a dozen mechanisms that were plausible stories read off a
trace.  **Session 59 is the one where reading source first found the
mechanism** -- `evaltree`'s missing restore was found in the
disassembly, not in a trace.  **Session 59 also produced two wrong
calls of the opposite kind, by this file's own rules: a section
title and a commit subject were read instead of the body, and both
were wrong.**  See "Gotchas worth reading," below.

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
  The marker is load-bearing: before it, the assistant is
  discussing; after it, the content is applyable.

  **A whole-file return for a large file with non-ASCII content
  re-encodes every non-ASCII character and shows up as a large
  deletion count.**  For an **insertion**, check `git diff --stat`:
  it should show **zero deletions**.  A full **replacement** of a
  section correctly shows both.  **A third shape: a Python edit
  with `count != 1` abort checks, which refuses to write if the
  anchor doesn't match exactly once.**  That shape caught a real
  ambiguity (an anchor matching twice) and wrote nothing.  Use it
  for edits into large files.

**Ask for source you do not have.**  The assistant does not have
direct file access.  Before patching a file it has not seen **in
this session**, it must ask for that file.  Never guess.

**When editing a large file, quote the bytes.**  For an edit inside
a big file, the instruction must quote the exact text being replaced
and the exact text that replaces it, both copied from output the
other side just pasted.  Read files with `cat -A` before writing
anchors into them.

**A redirection binds to the last command in an `&&` chain.**

**A capture file is one run.  Truncate, do not append.**

**A build flag change does not trigger a rebuild unless the
Makefile is a prerequisite.**

**Do not edit `third_party/`.**  The vendored sources there are
gitignored and rebuilt by the toolchain.  **But read them freely** --
session 59's mechanism was found by disassembling
`third_party/busybox/busybox_unstripped` and grepping
`busybox_unstripped.map`.  Read-only is the point.

**Hand-written `@@` hunk headers are a trap.**  Edit the file and
let `git diff` produce the patch; always `git apply --check` before
`git apply`.

**A diagnostic that runs on the fault path is part of the system.**
A fault-path diagnostic reads memory and prints serial output inside
an exception handler, and the extra time can widen a race's window.
Gate a fault-path diagnostic to the addresses it is for; do not
assume it is inert.

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
correctness milestone.**  **`v0.6.13` is a candidate** -- a
mechanism found, reproduced, and a real kernel defect fixed -- and
the work since `v0.6.12` is 60+ commits across applets, the zombie
leak, the NX family, and the probes.  The bump is a **decision**,
not a default.

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

## Where we are -- after the last tag

**The 7e family is a RACE, and the tick-rate curve is the proof.**
Same kernel, same image, same `pipe7e`, only `pit_init` varies:

| `pit_init` | result |
|---|---|
| 10 | `PIPE7E-ALL-PASS`, 50000 iterations |
| 20 | progresses, `iter 1000` within minutes |
| 30 | progresses |
| 40 | **begins to fail** |
| 100 | faults, at `iter 4` to `iter 232` across runs |

**A monotone failure curve with a threshold between 30 and 40 Hz
is the signature of a race.**  A deterministic defect produces the
same outcome at every rate.  This one does not.

**The precondition is ash's stale `jmp_buf`; the trigger is the
race.**  `evaltree` stores a pointer to its own stack frame's
`jmp_buf` into `globals+0x38` and does not restore it on the normal
return; `raise_exception` `_longjmp`s to it with no liveness check.
That is session 59's finding and it is a *deterministic* defect --
it explains why a stale buffer exists, not why it is only *reached*
at the higher rate.  **Both are required.**

### The fault's shape

Every 7e fault produces a small integer at a return-address slot,
where a `ret` popped it: `0x1`, `0x4`, `0x32`, `0x0`, `0xFFFFFFFF`.
`last sysret rcx` is `__syscall_cp` (`0x400CF2`, `0x4041F8`), the
musl cancellation wrapper on the stdio/lock path.  The value is
`_longjmp(buf, 1)`'s return value.  `%fs` is a text address at every
fault and all three records agree -- **the `fs_base` path is
correct and is not the mechanism.**

### What is fixed on `dev` (the migration and the kernel fixes)

The `testme` migration (13 commits, tag
`20261009-donix-migrated`) plus the session-61 kernel fixes
(tag `20261009-session61-fixes-merged`).  The fixes that closed
race windows, each found by a probe and verified:

1. **Pipe lifetime.**  `put_pipe_ref` / `put_file_slot` gate the
   free on `readers_open == 0 && writers_open == 0`.
2. **Stranded `READY`.**  `process_yield` / `process_exit` re-add a
   `READY` process off the queue before idling.
3. **The sysret tail.**  `cli` across the whole return tail.
4. **The `hlt`-return state.**  Reader resets `state = RUNNING`.
5. **The invalid-`end` guard** on pipe slots.
6. **The stale caller-saved register file** on block-and-resume
   (session-61: the ring-0 save path reads 15 pcb slots, writes 6).
7. **`pcb->rsp` / `pcb->user_rsp` split** (session-61), corrected
   by `c8d6255` -- the user-mode save must write BOTH fields.

### What is NOT fixed

**The 7e race itself.**  The fixes above closed windows; the family
still fires at 100 Hz.  **The residual window is not yet found.**
That is the next session's job.

### What was falsified, so it is not re-walked

- **Memory corruption** as the cause.  Five sessions chased it; the
  rate curve says race.  The wrong values are *symptoms* of windows.
- **The ready-queue lock** (P8: 6028 checks, all clean).
- **The `fs_base` path** (three values agree at `0x408698`).
- **The wake-kind inversion, the lost-wakeup instrument, the PMM
  double-handout** -- all zero on the probes.
- **The two `setjmp` probes** pass; those mechanisms are falsified.

---

## NEXT SESSION

**The lead is the 7e RACE.**  Not item 12 as the lead; item 12
remains the fix that lets ash *survive* the fault and is the largest
single target, but it is not the way to find the residual window.

**The method: find the window, not the corruption.**

1. **Run `pipe7e` at 10 Hz and at 100 Hz, with the P13/P12/P11
   probes on, and compare the counters.**  The difference is the
   race.  The probes are in `04_kernel_64bit/include/race_probe.h`,
   gated by `RACE_PROBE_*`.
2. **The fault-path read:** when it faults, read `CR2`, `RIP`,
   `last sysret rcx`, and the three `fs_base` values.  The shape
   names the window.
3. **The tick rate is the instrument.**  `pit_init(10)` widens the
   window; `pit_init(100)` fires it.  A window that moves between
   the rates is real; one that does not is not the one.

**Candidate windows the probes already point at:** the block sites
that still do `sti; hlt` without a register save (`sys_wait4`'s
yield path is the named gap -- it sets `BLOCKED` then calls
`process_yield`, reaching `context_switch` by a different route);
the `irq0_stub` resume path; the timer's `current->rsp` /
`user_rsp` decision.  **Read the bytes, add one probe, run once.**

### The candidates, in order

1. **The residual 7e race.**  The lead.  The tick-rate sweep and
   the probes; do not chase memory corruption.
2. **Item 12 -- signal delivery.**  Largest; lets ash survive the
   fault; unblocks `kill`, ash job control, the Wayland `SIGBUS`
   path.  A workstream, not a commit -- scope it first.
3. **`sys_gettimeofday` (96).**  Small, patterned, independent.
   Unlocks `ps -l`.  **Note: 96, not 99.**
4. **Documenting the ash mechanism** for a third-party patch.

### Do not

**Do not chase memory corruption** -- the rate curve says race.
**Do not point at musl or busybox** as the culprit binary; they are
read-only and the trigger is a kernel-side window.  **Do not
re-derive the `fs_base` path** -- it is correct.  Do not re-open
item 7 (closed), item 11 (closed), or the session-48 zone scan.
Do not re-litigate the five 7e mechanisms falsified in sessions
56-57.  Do not re-run the two `setjmp` probes to "check".  Do not
trust a page walk's verdict without reading the walk.  Do not drop
the older non-`2026*` tags.  One change at a time.  Do not edit
`third_party/` -- but do read it.

---

## Open issues (top 6; full list in `docs/open-issues.md`)

1. **`open-issues.md` item 7e: the control-transfer face.**  **It
   is a RACE** (rate curve).  Precondition: ash's stale
   `jmp_buf`.  Trigger: a timing-dependent kernel window, not yet
   found.  **The lead.**
2. **`open-issues.md` item 12: signal delivery is a stub.**  The
   largest single target; lets ash survive the fault.
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
  `tests/pipe7e_sjlj_stale.c`; the Makefile unchanged.
- `userland/scripts/` -- `test.sh`, `pipe_wake_probe.sh` (4000
  iterations).
- `third_party/{busybox,musl-src,musl-install}/`
  -- gitignored; **read freely, do not write.**
- `toolchain/{install_musl.sh,musl-gcc.sh}` -- tracked.
- `capture.txt` -- gitignored, truncated per run.  Captures
  preserved outside the tree survive a rebuild and do not dirty
  the tracked tree.
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
kernel fixes are on `donix`'s `dev`, unpushed.  Session 60's finding:
**the 7e family is a RACE, proven by the tick-rate curve** (10 Hz
clean, 40 Hz the threshold, 100 Hz faults).  The precondition is
ash's stale `jmp_buf`; the trigger is a timing-dependent kernel
window that has not been found.  Five race windows are closed and
committed; the residual race is the next session's lead.  **Do not
chase memory corruption -- the rate curve says race.  Read the
bytes, add one probe, run once.**  One change at a time.**

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
writes nothing -- use it for edits into large files; a handler on
the fault path with a corrupt `rsp` cannot run, so a redirect
without a re-entry guard loops forever -- guard it; and reading the
disassembly found the mechanism a dozen trace-readings had
missed.**  **And session 59's meta-gotcha: a section title is a
claim about its body, and a commit subject is a claim about its
diff -- read the body, read the diff, not the title and not the
subject.  Session 59 made that mistake twice.**  **This file's item
text is the same kind of claim.**
