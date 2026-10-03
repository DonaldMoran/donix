Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-10-03 (session 50: the fault-injection test
for `process_create`'s failure exits landed; three of the four exits
now run.  **Next: the boot-time `#PF` at `0x400000` came back on the
first boot after that commit and is the recommended next session.**)

**Repo state — run these; do not write it here.**  A header that
names a commit or a tag count is wrong the moment the same commit
lands, so this file does not carry one:

    git log --oneline -1            # HEAD
    git status -sb                  # branch, dirty?, ahead/behind
    git tag --list 'v*' | tail -1   # last milestone
    git tag --list '2026*'          # live scratch tags

> **The fault-injection test landed; one exit is still by
> inspection, and the `#PF` is back.**  Session 49's
> `process_create` cleanup was "correct by inspection;
> UNEXERCISED."  Session 50 added two kernel-side fault-injection
> hooks and a `test_create_fail` selftest row: **exits 1, 2, and 4
> now run** (`selftest` 18/18, was 17), `pmm_get_free_pages()` is
> unchanged across each, no double-free.  **Exit 3
> (`vmm_map_page_in_cr3` returning -1 in the stack loop) is still
> not tested** and the commit says so — reaching it needs a
> page-table allocation to fail after the clone succeeded but
> before the stack loop's map call, and the clone's own tables are
> the same type, so a type-filtered hook cannot separate them.
> **Separately, the first boot after that commit reproduced the
> boot-time `#PF` at `CR2 = RIP = 0x400000`** during the `musl_sh`
> ELF load — layout-dependent, gone on the next boot, unrelated to
> the hooks.  It is `open-issues.md` **item 7a**, a sibling of the
> session-45 virtual-1 fault, and it is the recommended next
> session.  See `docs/session-log.md`, session 50, and
> `gotchas.md`, "A function that has never run is correct by
> inspection only."

**The version history, in one line each:** `v0.6.6` pipes; `v0.6.7`
a real shell and a framebuffer console; `v0.6.8` the `*at()` family;
`v0.6.9` envp, the `/usr/bin` layout, the `execve` shim removal;
`v0.6.10` a tail on `v0.6.9` — `realpath`, the `readlink` errno,
and `/dev/null`; `v0.6.11` the pathname dispatch seam, `/proc`
per-pid and `ps`, and the PMM zone-scan fix; **`v0.6.12` a
correctness milestone** — item 7 closed, `process_create`'s failure
exits fixed, the exit-path page-table leak closed, and one dead
`f_stat_with_retry` block deleted.  **No new subsystem in
`v0.6.12`.**

**Session 50 is untagged work on `dev`, after `v0.6.12`.**  One
kernel commit (`20261003-fail-inject`), then two docs commits.  No
`v*` bump; the banner still reads `v0.6.12`.

Commits are named by tag only, never by SHA.  **Working tags
(`YYYYMMDD-*`) are local scratch restore points** — they exist while
a milestone is being developed and are **dropped before the
milestone is pushed**.  Only `v*` tags go to the remote and are
permanent.  The commit record is `docs/session-log.md`; the commit
message carries the narrative.  Once a scratch tag is dropped it
resolves to nothing; do not cite one as if it were a stable
reference.

**Scratch tags are annotated.**  The annotation is the fuller
per-commit summary; at a milestone bump the annotations seed the
final milestone narrative.  This is why each step gets a tag even
when no `v*` tag is imminent.

**Scratch tags are also state.**  Which ones are live is
`git tag --list '2026*'`; this file does not list them.

**A plain commit can exist with no tag.**  Session 45's `run:` fix
is one: it is not a milestone, so it gets no `v*`; it is not a
scratch restore point for a milestone in development, so it gets
no `YYYYMMDD-*` either.  Session 50's two docs commits are the
same — docs-only, no tag.  Such commits ride along on `dev` until
the next `v*` bump.  Do not invent a tag for one, and do not be
surprised by a commit with no tag.

**Note on commit messages:** a commit message is a claim, not a
fact.  Four instances to remember.  `95c6337 handoff: rewrite fresh
for the v0.6.9 bump` did not rewrite the handoff body.  Session
45's Fix B (`157627c`, later reverted) claimed to close the
boot-time `#PF`; it did not — the fault reproduces with a different
presentation.  Session 49's commit
`20261003-process-create-cleanup` says "correct by inspection;
UNEXERCISED" — which was honest when written, and is now
**superseded for three of its four exits** by session 50's
`20261003-fail-inject`.  And that session-50 commit itself says
exit 3 is not tested — which is still true.  Read the diff, not the
subject.

---

## Working style — how this project gets changed

Read this before proposing any command block.  It is how the
project has been run for many sessions, and it is what the next
session should preserve.

**One change at a time.**  A "change" is a commit.  The handoff's
work, the docs' rules, and the session-log's pattern all assume a
single coherent change per commit, verified before the next one
starts.

**The state check comes before the command block.**  Every commit
block starts with `git status` (or `git log`) so the working tree
is known *before* a command runs.  This is a rule because it was
learned: a `git commit` was once written against an assumed state
and landed wrong.  Ask for the status, then give the block.

**A command block is one paste.**  `git add`, `git status`,
`git commit`, and `git tag` belong in **one** block, with the
commit message inline via heredoc (`git commit -F - <<'EOF' … EOF`),
not spread across several turns.  The same for the push/merge
sequence below.  Do not split a single operation across messages;
that is how a commit and its tag get separated.

**Two file-return styles.**

- **Small files:** return the complete file, four-backtick fenced.
- **Large files:** return unified-diff hunks, each located by
  enough surrounding context to be unambiguous, preceded by a bold
  **`WORK BEGINS HERE.`** marker on its own line so it is never
  confused with thinking-out-loud.

  The marker is load-bearing: before it, the assistant is
  discussing; after it, the content is applyable.  Never mix the
  two — do not post file parts while reasoning.

**Ask for source you do not have.**  The assistant does not have
direct file access.  Before patching a file whose current contents
it has not seen in this session, it must **ask for that file**.
Never guess at a file's contents, never patch from memory of an
earlier version, never assume a file is unchanged.  This is how
stale patches and reverted work have been avoided.

**When editing a large file, quote the bytes.**  Session 44 added
this the hard way.  For an edit inside a big file, the instruction
must **quote the exact text being replaced and the exact text that
replaces it**, both copied from output the other side just pasted.
Do not describe an edit as "the block above" or "after line N" when
N has not been seen.  Four build failures in session 44 came from
edits described from memory rather than quoted from the file — the
fix each time was to read the current bytes and quote them.  A
whole-half or whole-file return is the safe form; a one-line
insertion into a 6500-line file is the form that breaks.

**A redirection binds to the last command in an `&&` chain.**
Session 45 added this.  `a && b && c && d > file` sends only `d`'s
output to `file`; `a`, `b`, and `c` write to the terminal.  The
`run` script's build line used this form, so every capture it
produced was missing the front of the build — including whether the
kernel was rebuilt at all.  Half a session was spent comparing
binaries whose build log had been silently discarded.  When the
whole chain's output matters, wrap it:
`{ a && b && c && d ; } 2>&1 | tee file`.  See `docs/gotchas.md`.

**Do not edit `third_party/`.**  Session 47 added this.  The
vendored sources there are gitignored and rebuilt by the toolchain,
so an edit is invisible to the repo and vanishes on the next build.
When a diagnostic needs to see inside a third-party applet — a
`printf` in busybox, say — the right instrument is a **first-party
test that reproduces the applet's sequence** (e.g. `proc_walk` for
`procps_scan`), or a **trace in our own kernel**, not a patch to
`third_party/`.  Both are committable; the patch is not.

**The push / merge / tag sequence** (the project's own order,
used for every `v*` bump):

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

Before this: the banner bump is the **last** code commit of the
version; the session's `YYYYMMDD-*` scratch tags are **dropped**
(`git tag -d`); and the session-log and handoff are written.  The
banner commit is named `kmain: bump the shell banner to vX.Y.Z`.

The tag is **annotated** (`-a -F -`) — the annotation is the
milestone narrative, and it is what a future reader sees first.
The merge is `--no-ff`, so `main` keeps a real merge commit for
each version (`Merge dev into main for v0.6.12`, etc.).

**A version is not necessarily a milestone.**  `v0.6.10` is a tail
on `v0.6.9` with no new subsystem.  When that happens, the tag
annotation and the session-log row should *say so*, so a future
reader does not hunt for a milestone narrative that is not there.
**`v0.6.12` is a correctness milestone, not a feature one** — the
annotation should say so plainly.  Session 50 may be the same
shape: one kernel commit, two docs commits, no `v*` yet.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 syscall ABI, natively. Newlib is gone.
Userland is a tracked source tree at `userland/musl/`. Phase B
(busybox) is well underway.

Full strategy, rules, and the one-change-at-a-time discipline:
`docs/strategy.md`.  Direction of travel beyond the current
milestone: `ROADMAP.md`.

---

## Tree on disk

One working copy.

- **Real project:** `/home/noneya/code/donix/`.  This is the tracked
  repository, the source of truth, and where the `v*` tags live.

The kernel C sources live in `04_kernel_64bit/`.  Verify by file
name before editing — do not trust the number alone.
`05_boot_kernel64/` holds the boot chain assembly and the image
builder, not the kernel C sources.

Scratch workspaces (e.g. a copy at `/home/noneya/code/testme/`)
are **transient**: never the source of truth, never where a `v*`
tag lives, and never referenced by this file.  If one exists, it is
safe to reset or delete.  Do not port from a scratch workspace into
the real tree without a build and test in the real tree.

---

## Where we are — session 50, after `v0.6.12`

**One kernel commit and two docs commits on `dev`, untagged.**

### The fault-injection test (`20261003-fail-inject`)

Session 49's `20261003-process-create-cleanup` added real cleanup to
`process_create`'s four failure exits and called it "correct by
inspection; UNEXERCISED" — a healthy boot does not fail an
allocation.  Session 50 makes three of the four run.

**Two hooks, not the one the previous handoff sketched.**  Reading
the source showed a single `static int` in `pmm.c` cannot reach
more than exit 1: the clone makes several allocations, so a bare
flag armed before `process_create` trips inside
`vmm_clone_page_table`.  And exit 4 is pool exhaustion, not an
allocation, so no `pmm` hook can reach it.  The commit adds:

- `pmm_debug_fail_next_of_type(page_type_t)` — **type-filtered**,
  not a count, so `PAGE_PAGE_TABLE` (the clone) and
  `PAGE_USER_DATA` (the user-stack page) fail independently.
- `process_debug_fail_next_stack_slot(void)` — for exit 4.

Both are one branch, self-disarming, kernel-side only, no syscall
door.

**What each case does, and does not.**

| Exit | Trigger | What it exercises |
|---|---|---|
| 1 | clone returns 0, armed `PAGE_PAGE_TABLE` | the **undo-PCB-slot** path; `process_free_clone` does **not** run (nothing allocated) |
| 2 | `pmm_alloc_page_for_elf` 0, armed `PAGE_USER_DATA` | **`process_free_clone` on a fully-built clone** — the case the gotcha is about |
| 3 | `vmm_map_page_in_cr3` -1 in the stack loop | **NOT TESTED** — see below |
| 4 | `kernel_stack_slot_alloc` exhausts | most cleanup: 16 tracked pages + clone + slot |

Exit 3 is unreachable deterministically with a type-filtered hook:
the map call's failing allocation is the same type the clone uses,
and a countdown would be brittle against any change to the clone's
shape.  It remains correct by inspection, and the commit says so.
The "UNEXERCISED" claim is superseded for **1, 2, and 4**.

**Verification:** `selftest` **18 passed, 0 failed** (was 17);
`canary` 15/15; `canary --full` 28/28.  `pmm_get_free_pages()`
unchanged across each failed `process_create`; no `PMM: WARNING -
Double free`.  The three `PROCESS:` lines in the output are the
**production** failure-path diagnostics — the hooks drove the real
exits, not a copy.

### The `#PF`, which is the next session

**The first boot after the commit reproduced the boot-time `#PF`
at `CR2 = RIP = 0x400000`** during the `musl_sh` ELF load.  It
did **not** reproduce on the next two boots.  It is **not** the
hooks (no test ran; both are `static` and unarmed), **not** the
closed item 7 (the returns are plumbed), and **not** session 48's
PMM zone scan (that wraps now).  It is `open-issues.md` **item 7a**
— a sibling of the session-45 virtual-1 fault, same family
(allocation failure during the shell's ELF load), different
presentation.

**What is new is the reporting.**  Session 45's version was silent:
the mapping failed, `vmm_map_page_in_cr3` returned without the
caller knowing, and the process faulted later in user mode.  This
boot printed `ELF: COPY-FAIL phys=0 at vaddr=0x400000` and
panicked at the ELF load — session 49's item-7 plumbing working.
A silent failure became a reported one; the allocation still
failed.

**Two candidate causes, not distinguished by the capture:** a
genuine `pmm_alloc_page` exhaustion in a shape the session-48 wrap
does not cover, or a half-built clone missing the PDPT/PD for
`0x400000` so the map's own table allocation failed.  The dump
walks the **parent's** cr3, not the child's clone.  See NEXT
SESSION.

### The two older milestones, condensed

**The pathname dispatch seam (`v0.6.11`, session 44).**
`resolve_at` returns a backend tag from a path's first component;
FAT / DEV / PROC are selected by it.  No VFS.  Its consumers:
`/dev/null`, `/proc/self/status`, `/dev/console` +
`/proc/self/fd/N`.  Full narrative in `ROADMAP.md`.

**`/proc` per-pid and `ps` (`v0.6.11`, session 47).**  `/proc` is a
listable directory; `ps` and `pstree` work.  The bug worth
remembering: `procps_scan` stats `/proc/<pid>/` **with a trailing
slash** and skips the entry when that fails.  Full narrative in
`ROADMAP.md`; the lesson is in `gotchas.md`.

**The PMM zone-scan fix (`v0.6.11`, session 48).**  The
intermittent boot-time `#PF` at `0x400000` was `pmm_alloc_page`'s
one-directional scan, not item 7.  `pmm_scan_zone` now wraps.  Full
narrative in `ROADMAP.md`.

### Session 45 — the one thing it left

Session 45 attempted the item-7 fix three ways and reverted all
three.  Its **fault is the surviving artifact**: a user-mode `#PF`
at virtual 1 (`CR2 = RIP = 0x1`, error `0x15`, `pte = 0x3` present
/ write / **no user**, phys 1, every page of the walk `PAGE_TABLE`),
observed with the signature change and **not seen since** — including
through session 49's full signature change.  Session 50's `#PF` at
`0x400000` is its **sibling**, not the same capture.

**Disposition: instrumented, not fixed.**  `isr14_handler` prints
the four page types of every `#PF` walk, permanently.  If it
recurs, the dump is in the serial log at the moment it happens.  Its
one kept commit is the `run` script's build-capture fix (`6cfb0e6`),
which rides on `dev` untagged.

---

## NEXT SESSION — the boot-time `#PF` at `0x400000` (open-issues item 7a)

**This is the recommended target.**  It returned on session 50's
first boot, and session 45's rule governs: **an intermittent fault
that stops reproducing is not fixed — it is unobserved.**  It has
now appeared in three sessions with two presentations.

### What is known, exactly

- The fault is a user-mode `#PF` at `CR2 = RIP = 0x400000`, error
  `0x15` (present, write, user, **fetch**), during `musl_sh`'s ELF
  load, **before any user instruction ran**.
- `ELF: COPY-FAIL phys=0 at vaddr=0x400000` — `elf_load_into_process`
  got a failed mapping from `vmm_map_page_in_cr3`.
- The dump walks the **parent's** cr3 (`0x30D000`): PDPT and PD
  present, PDE `0x400083` — the bootloader's **user** 2 MB huge page
  (present, write, user; the session-45 dump's huge page was
  supervisor, which is a difference, not the same capture).
- Layout-dependent: gone on the next boot; returns after an image
  change.

### What is *not* known, and the instrument that would say

Which allocation in `vmm_map_page_in_cr3` returned 0.  The
candidates:

- **(a)** `pmm_alloc_page` genuinely returned 0 — free-page
  exhaustion in a shape the session-48 wrap does not cover.
- **(b)** The **child's clone** was missing the PDPT or PD for
  `0x400000`'s region, so `vmm_map_page_in_cr3`'s own table
  allocation failed.  The clone copies the low half; if the source
  PML4 entry or PDPT entry was absent at clone time, nothing was
  copied and the map has to allocate it.

**Instrument, do not guess.**  The right first change is a
diagnostic, not a fix:

1. In `vmm_map_page_in_cr3`'s three `if (!new_*) return -1;` sites,
   print **which** one failed (PDPT / PD / split-PT / final-PT),
   the `virt`, the `cr3`, and `pmm_get_free_pages()`.
2. Boot the trigger image; the failing boot's serial log names the
   site.  If it does not reproduce in N boots, that is the
   session-48 result — record it and keep the diagnostic in place.
3. Only then choose a fix: (a) is an allocator change; (b) is a
   clone-correctness change.

**Read first:** `vmm.c`'s `vmm_map_page_in_cr3` and
`vmm_clone_page_table`, `pmm.c`'s `pmm_alloc_page` and
`pmm_scan_zone`, and `docs/open-issues.md` item 7a.

**Do not** re-open item 7 (closed, shipped in `v0.6.12`) or the
session-48 zone scan (fixed, shipped in `v0.6.11`).  This is a
third fault in the same family, and the first two being closed is
what makes it legible.

### If you would rather do something small

**Exit 3 of `process_create`.**  The one untested failure exit.  A
hook that fails the *nth* `PAGE_PAGE_TABLE` allocation after a mark
— or a `pmm_debug_fail_after(n, type)` — would reach it
deterministically.  Slightly larger facility than the type-filtered
one; the commit message must say what it adds and what it still
does not reach.

**`sys_gettimeofday` (99)** — a few lines from `g_ticks`, like the
existing `sys_clock_gettime` (228).  It unlocks busybox `ps -l` /
`ps -e` (`PS_LONG`, `PS_TIME`), which currently hit
`Unknown syscall: 99`.  Small, patterned, independent.

### Candidates after that, none blocking

- **`open("/proc/<pid>", O_DIRECTORY)`** — the "complete" half of
  session 47's fix.  Same two paths in `open_resolved`, producing a
  `FILE_KIND_DIR` slot with `PROC_DIR_SENTINEL`, which exists.
- **`/dev` as a listable directory** — `ls /dev` fails.  Same
  directory shape `/proc` got.  Prerequisite for `/dev/tty` and
  `/dev/urandom`.
- **`musl_sh` as a child of `idle`** — so `pstree` shows a
  Unix-like tree.  A `kmain`/`process_create` change.
- **`/etc/passwd`** — makes `ps`'s USER column show names.
- **`kill` / `pidof`** — `pidof` needs the per-pid entries (now
  present) and a lookup; `kill` additionally needs signal delivery.
- **The `proc_walk` assertion test** — turn the diagnostic into a
  test that asserts.
- **Symlinks** (`open-issues.md` item 8) — larger; design recorded,
  the seam exists to hide the encoding.

### Do not

Do not reopen the PMM fix (session 48) or item 7 (session 49) —
both are committed, shipped, and verified.  Do not re-litigate the
fault-injection design — it landed, it works, and exit 3's
untested status is documented on purpose.  One change at a time.
Do not edit `third_party/`.

---

## Busybox enablement — state of play

**`ps`, `pstree`, `stty`, and `tty` are enabled.**  `ps` lists
processes; `pstree` shows `idle`; `tty` prints `/dev/console`;
`stty` prints the terminal state.

### The rule

**Enable an applet only when the syscalls it actually calls are
implemented — read the applet's source, do not guess from its
name.**  When something is missing, the question is "how big is
it?" — a table entry and a directory shape is a session's work; a
signal-delivery subsystem is not.  A syscall that exists but
cannot do its job (a `chmod` on a filesystem with no permissions)
is worse than a missing one: it makes the applet lie.

**A `/proc` consumer can stat a path it never opens.**  Session
47's lesson: `procps_scan` stats `/proc/<pid>/` (trailing slash)
under `PSSCAN_UIDGID` and skips the entry when it fails, before
reading any file.  Every per-pid *file* can exist and the applet
still prints nothing.  See `gotchas.md`, "A /proc consumer can
stat a path it never opens."

**Do not edit `third_party/`.**  When a diagnostic needs to see
inside an applet, write a first-party test that reproduces its
sequence, or trace our own kernel.  Both are committable.

### A note on `stty`

`stty` runs and prints a plausible state (speed, control
characters, flags).  **It cannot change the terminal:** the kernel
console has no termios, so `stty -icanon`, `stty erase X`, and the
like do nothing.  The applet reads and reports; it cannot write.
Line editing in `ash` is busybox's own, independent of the kernel.
Not a defect — a limit.

### What each still-off applet needs

Each row is a **cost estimate, not a prohibition**.  Sometimes the
missing piece is small and we write it — that is how `tty` and `ps`
were enabled.  Sometimes it is a whole subsystem and we do not
write it just to turn on one applet.  The rule is **know what you
are signing up for** before you enable.

| Config | Applet | Needs |
|---|---|---|
| `CONFIG_DIFF` | `diff` | `mmap` of files (non-anonymous `mmap`); deliberate |
| `CONFIG_CHMOD` | `chmod` | `chmod`/`fchmodat`; FAT has no permissions |
| `CONFIG_CHOWN` | `chown` | `chown`/`fchownat`; FAT has no ownership |
| `CONFIG_LN` | `ln` | `link`/`symlink`; FAT has no links |
| `CONFIG_LINK` | `link` | same |
| `CONFIG_MOUNT`/`UMOUNT` | `mount`/`umount` | `mount` (165); no VFS |
| `CONFIG_HALT`/`POWEROFF`/`REBOOT` | `halt`/`poweroff`/`reboot` | signal delivery (item 5); no init, no ACPI |
| `CONFIG_TAR`/`UNZIP`/`CPIO`/`GZIP`/`BZIP2`/`XZ` | archives | `mkdirat`, `symlinkat`, `utimensat` storage, file-backed `mmap`, decompression |
| `CONFIG_AWK` | `awk` | large; needs `FEATURE_AWK_LIBM` |
| `CONFIG_LESS`/`MORE` | pagers | raw-mode terminal control; `/dev/tty` does not exist |
| `CONFIG_TOP` | `top` | `ps -l`-class fields (`gettimeofday`), a redraw loop |
| `CONFIG_KILL` | `kill` | signal delivery; `procps/kill.c` |
| `CONFIG_PIDOF` | `pidof` | per-pid entries (now present) and a lookup |
| `CONFIG_NETWORKING` (all) | `ping`, `wget`, etc. | no network stack |
| `CONFIG_ASH_JOB_CONTROL` | ash job control | signal delivery |
| `CONFIG_FEATURE_TAB_COMPLETION` | ash completion | needs `stat` on many paths; probably works, test it |

**Enabled and working, for the record:** `ps`, `pstree`, `stty`,
`tty` (prints `/dev/console`), plus the applets listed under
"State on disk."

---

## Canary state

**Green as of session 50** — `canary` 15/15 and `canary --full`
28/28, plus `exec_churn` and `mmap_stress` pass.  The kernel
self-test runs at boot and reports **18/18** (was 17; session 50
added `create_fail`).

`exec_churn` has two uses: it exercises the process-exit page-table
teardown (`process_free_clone` via `process_reclaim` /
`process_destroy`), and it is a 24-round ELF-load stress — run it
when changing those, or when changing the ELF load path.

**The canary is a program: `canary`.**  `tests/canary.c` runs every
non-interactive canary row, checks exit status and output
substrings, and reports pass/fail.  Run from `donix>` or ash; both
find `/usr/bin/CANARY`.

    canary          # read-only rows
    canary --full   # also the mutating rows (create/remove under /)

**What stays manual**, printed at the end of a `canary` run:

    busybox ash      # interactive; then pwd, cd /bin, ls, exit
    vi test          # fills screen; :wq; ./test

**Regression tests (`userland/musl/tests/`, not canary rows):**

    at_step1       # resolve_at via dirfd; fstatat flags;
                   # AT_EMPTY_PATH; faccessat dirfd; utimensat dirfd
                   # (read-only)
    at_step2       # unlinkat + the unlink/rmdir type check
                   # (mutates the disk)
    envp_step1     # envp survives execve, 3 checks (forks)
    musl_exec2     # the bare-name shim is gone, 3 checks (forks)
    fcntl_lowfd    # fcntl on fd 0/1/2, 3 checks (redirects fd 0)
    readlink_errno # readlink(2) errno split: ENOENT for a missing
                   # path, EINVAL for an existing non-symlink, 3
                   # checks (read-only)
    proc_status    # /proc/self/status: open, read the five keys,
                   # stat (S_IFREG), close; 9 checks
    proc_fd        # /proc/self/fd/N -> /dev/console; the
                   # (st_dev, st_ino) match with fstat(0); 7 checks
    proc_dir       # /proc is a directory; readdir returns self and
                   # numeric pids; 8 checks
    proc_stat      # /proc/<pid>/stat opens, parses; 7 checks
    proc_walk      # walks /proc the way procps_scan does and
                   # REPORTS (does not assert); the detailed check
    proc_walk_fds  # the same walk with low fds occupied
    mmap_stress    # map/touch/unmap/remap; the munmap free path
    exec_churn     # fork/execve/wait4, 24 rounds; the exit free
                   # path, the process-exit page-table teardown,
                   # and a 24-round ELF-load stress
                   # (needs CHURN_HELPER staged)

**Pipe regression suite (`userland/musl/tests/`):**

    pipe_step1    # create, round-trip, close, re-close EBADF
    pipe_step2    # blocking + directed wake
    pipe_step3    # EOF, EPIPE, dup-aware counts
    pipe_step3b   # exit-path wake

Run these when changing `sys_read`/`sys_write`/`sys_close`/
`put_file_slot`/`sys_fork`/`sys_pipe` or adding a `FILE_KIND_*`.
**Run them, do not just read the rule.**

**New tests must be added to both `USERLAND_ELFS` and the
`mcopy_one` chain in `05_boot_kernel64/Makefile`.**  The image now
stages **48 files** — session 50 added no ELF, only kernel code, so
the count is unchanged.

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; use
`busybox sh` or `/bin/busybox sh`.

**Expected noise:** no `Unknown syscall:` lines; no `[fd]` lines
(trace off); no `[a|b|c]` debug line (removed); no `[faccessat]`
trace line (removed).  The `FB: mapped N pages ...` line is
expected.  The `sys_open: f_open FAIL path=...` lines from `vi` on
a new file, from `busybox stat` on nonexistent paths, and from `ps`
looking for `/etc/passwd` are expected diagnostics.  `sys_execve:
f_open FAIL` lines no longer appear, and neither do the `f_open
FAIL path=dev/null` lines from `2>/dev/null`.

**Session 50's own noise to expect:** on a **healthy** boot, none.
The three `PROCESS:` lines session 50's test prints appear **only
when `selftest` runs `create_fail`**, and only from the kernel `k`
shell.  A healthy non-`k` boot prints none.  If a `PROCESS:` or
`sys_mmap: map failed` or `sys_brk: map failed` or `VMM: FATAL`
line appears outside a `selftest` run, that is a real failure the
new checks are reporting — not noise.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **`open-issues.md` item 1: the remaining FatFs-form
   conversions.**  `strip_dot_prefix` and the `"0:"` prefix are the
   FAT backend's own business now; a future VFS absorbs them.
2. **Redirection of a builtin is silently ignored.**
3. **A builtin in a pipeline is refused.**
4. **`rename(2)` does not replace; `chmod`/`ln`/`mount` need their
   own syscalls.**  Deliberate FatFs-limitation first cuts.
5. **`-EPIPE` is delivered without `SIGPIPE`.**  Fixing it means
   signal delivery — the same subsystem `reboot`/`halt`/`poweroff`
   need.

**Item 7 is closed** (session 49, shipped in `v0.6.12`).  The list
keeps its numbering; **item 7a** is the new boot-time `#PF`
(session 50).  Item 8 is symlinks.

Also open: `unlinkat` has no consumer; `sys_brk`'s fixed
`heap_base` and the 4 MB mmap window; real FatFs timestamp
storage; `prctl` is minimal; busybox applet symlinks not
installed; syscall-table audit script; `musl_wait`'s WNOHANG loop
spins; `sys_mmap` rejects all non-anonymous mappings; pipes
support one concurrent reader and one concurrent writer;
`put_file_slot`'s pipe wake is coupled to `sys_close`'s wake;
**`readdir("/dev")` fails**; **`st_rdev` is 0 on device nodes**;
**`open("/proc/<pid>", O_DIRECTORY)` is not done**; **a
nonexistent pid stats as a directory**; **`sys_gettimeofday` (99)
is not implemented**, so `ps -l` is off.

**The session-45 virtual-1 fault and the session-50 `#PF` at
`0x400000` are unobserved, not fixed.**  They did not reproduce
through session 49's full signature change; the `0x400000` one
returned on session 50's first boot and cleared on the next.
Diagnostics for both are permanent: `isr14_handler` prints the four
page types of every `#PF` walk, and the session-50 `ELF:
COPY-FAIL` line names the call site.  **Standing caution:** do not
filter `vmm_clone_page_table`'s leaf copy on `PT_USER` — session 45
found it breaks the kernel's own identity map.

---

## State on disk

**Real project root:** `/home/noneya/code/donix/`.

**The FAT layout:**

    /            HELLO-WORLD.TXT and other data
    /bin         busybox
    /usr/bin     the donix-native ELFs (apps + tests), staged BARE
    /tmp         empty

- `configs/busybox.config` — tracked canonical busybox config.
  Enabled applets: `cat`, `cp`, `cut`, `echo`, `env`, `false`,
  `find`, `head`, `ls`, `mkdir`, `mv`, `od`, `printenv`, `pwd`,
  `rm`, `rmdir`, `seq`, `sort`, `stat`, `tail`, `tee`, `test`,
  `touch`, `tr`, `true`, `uname`, `uniq`, `wc`, `yes`, `cmp`,
  `grep`, `sed`, `vi`, `clear`, `basename`, `dirname`, `unlink`,
  `ttysize`, `tty`, `arch`, `mktemp`, `sleep`, `usleep`,
  `truncate`, `realpath`, `stty`, `ps`, `pstree`, plus `ash`.
- `userland/musl/` — tracked musl userland (`apps/`, `tests/`).
  `build/` gitignored.  New tests must be added to both
  `USERLAND_ELFS` and the `mcopy_one` chain in
  `05_boot_kernel64/Makefile`.
- `04_kernel_64bit/fonts/ter-u18n.psf` — tracked font source.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  — gitignored; rebuild with `./toolchain/install_musl.sh`.
  **Do not edit these.**
- `toolchain/{install_musl.sh,musl-gcc.sh}` — tracked.
- `PFcapture.txt` — the session-45 fault capture (presentation 2).
  Gitignored; **still on disk — `ls` it, `git status` will not
  show it.**  Presentation 1's capture is not on disk; its dump is
  in `docs/session-log.md`, session 49.  Session 50's `#PF` capture
  is in `capture.txt` on disk and in `docs/session-log.md`, session
  50.
- `DFAULT.txt` — the session-48 double-fault capture.  Gitignored.
- `run` — tracked; the build-and-capture fix lives here.

Kernel sources: `04_kernel_64bit/`.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.  Paths relative to the tree root
(`/home/noneya/code/donix/`).

- `docs/strategy.md` — Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.
- `docs/gotchas.md` — every bug writeup, by subsystem.
  **Session 49 added two entries**: "A function that has never run
  is correct by inspection only" and "A comment that names its
  callers goes stale the moment a new caller appears."  Session 48
  added "A zone scan that moves one way does not find pages behind
  its cursor."  Session 47 added "A /proc consumer can stat a path
  it never opens."  Session 46 added "A redirection binds to the
  last command in an `&&` chain."  Session 45's `pmm_get_page_type`
  result (the virtual-1 fault's walk was all `PAGE_TABLE`, ruling
  out use-after-free) is a finding about one fault, not a general
  lesson, and lives in `docs/session-log.md`, session 49, rather
  than here.  **Session 50 added no gotcha** — the fault-injection
  test is a fix to an unexercised path, not a new lesson; the
  session-49 entry it closes is edited in place there.
- `docs/session-log.md` — commit tables and per-test canary notes.
  Session 50's section is at the top; then 49's, 48's, 47's, 46's,
  and 44's.  Session 44's section is recorded but **misplaced** (it
  sits after session 34), noted at the top of the file and
  deferred.  There is no session-45 section: session 45's one kept
  commit is the tooling fix, named in the session-46 section.
- `docs/open-issues.md` — full open-issues list.  **Item 7 is
  closed; the list has a gap where it was, and session 50's
  `#PF` is item 7a in that gap.**  Item 8 is symlinks.  The
  "Test-design notes" section at the bottom is **misplaced** (it is
  instructions, not issues) and is flagged for a move to this
  file's canary section in the next documentation round, together
  with the item-7 note.
- `docs/migration-history.md`, `docs/dons-os-history.md` —
  historical narrative.
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` — the first two
  frozen at `v0.6.0`; `LLD_BUG_REPORT.md` current.
- `ROADMAP.md` — future work only.  **Its `v0.6.12` section is the
  session-49 narrative**; the "Done — Phases A through B" list
  includes `v0.6.12`.  Session 50 has no `ROADMAP.md` section yet —
  add one if session 50's work reaches a milestone, otherwise the
  session-log section is the record.
- `README.md` — reviewed at the `v0.6.11` bump; **review it at the
  next `v*` bump.**
- `run` — tracked; the build-and-capture fix lives here.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  `v0.6.7` shipped the shell and the framebuffer;
`v0.6.8` the `*at()` family; `v0.6.9` envp, the `/usr/bin` layout,
the `execve` shim removal; `v0.6.10` `realpath`, the `readlink`
errno, and `/dev/null`; `v0.6.11` the pathname dispatch seam,
`/proc` per-pid and `ps`, and the PMM zone-scan fix; **`v0.6.12` a
correctness milestone** — item 7's silent `vmm_map_page*` returns
closed, `process_create`'s failure exits fixed, the exit-path
page-table leak closed, and one dead `f_stat_with_retry` block
deleted.  **Session 50** landed the fault-injection test for
`process_create`'s failure exits — three of four now run — and the
first boot after that commit brought the boot-time `#PF` at
`0x400000` back.  **The recommended next session is that `#PF`**
(open-issues item 7a): instrument `vmm_map_page_in_cr3`'s three
failure sites to name which allocation returned 0, then fix;
do not guess from the dump.  See NEXT SESSION.  One change at a
time.**

---

## How to use this file

At session start, paste this file and say "Continue from here and
request any files you need."  Run the four `git` commands in the
repo-state block — they are the state; this file is the narrative.

At session end, **rewrite the narrative**: what changed, what is
next, what is tabled.  Do **not** write repo state — no HEAD, no
ahead/behind count, no tag list, no dirty/clean claim.  State is
what git is for; a hand-written state line is wrong the moment the
commit that writes it lands.  New gotchas go to `docs/gotchas.md`;
new commit rows go to `docs/session-log.md`; new open issues go to
`docs/open-issues.md`.  This file never grows.  Name commits by tag
only, never by SHA.

**When a session's findings change an earlier numbered step, edit
the step in place — do not just add a paragraph above it.**

**Before proposing any command block, read the "Working style"
section at the top.**

**Five gotchas worth reading before the next change.**  "A fix with
no test is indistinguishable from an unfixed defect."  "A test can
encode an earlier version's behavior."  "A consumer inferred from
behavior is not a consumer."  Session 45's: a fix can fail to close
the thing it claims to close, and an intermittent fault that stops
reproducing is not fixed — it is unobserved.  And session 49's: **a
function that has never run is correct by inspection only** — which
session 50 acted on, and which is why the remaining untested path
(exit 3) says so in its commit message.
