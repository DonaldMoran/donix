# donix session log

Newest first.  Each session that did real work has a section; the
most recent few are kept in full.  Older sessions are indexed at the
bottom — for their detail see `git log`, the annotated `v*` tags, and
`ROADMAP.md`, which carry the milestone narratives the index points
at.  This file's job is "what happened, in what order, and what to
read for the detail"; it is not a second copy of the commit record.

Canary counts and new-test lists are here because they are nowhere
else.  Commit narratives are in the commit messages and the tag
annotations, not repeated here.

---

## Session 49 — item 7 closed; the `process_create` failure paths and the exit-path page-table leak

Five commits on `dev`, scratch-tagged, unpushed.  **Not a
milestone** — five separate changes that add up to a session on
kernel failure paths, plus one dead-code deletion.

| Tag | What |
|---|---|
| `20261003-vmm-int-return` | `vmm_map_page*` return `int`; the six silent returns now report `-1`; `isr14_handler` gains the `pmm_get_page_type` diagnostic (item 7, commit 1 of 2) |
| `20261003-vmm-callers` | the nine callers handle the `-1`; **item 7 closed** (commit 2 of 2) |
| `20261003-process-create-cleanup` | `process_create`'s failure exits clean up; the unchecked `vmm_clone_page_table` return now fails the create; new `process_free_clone` |
| `20261003-dead-has-drive` | `f_stat_with_retry`'s dead `has_drive` and its `if` block deleted |
| `20261003-exit-frees-tables` | `process_reclaim` and `process_destroy` free the page tables via `process_free_clone` |

### Item 7, and how it closed

Item 7 was the silent `if (!phys) return;` in `vmm_map_page_in_cr3`
and `vmm_map_page`: six sites — the PDPT, PD, and PT allocation paths
in each — where a page-table allocation failure meant no mapping was
made and the caller could not tell.  Item 7's own text calls this "a
function that cannot report failure, called by code that assumes
success."

**The reason this was not done in session 45.**  Session 45
attempted the signature change and reverted it, because `canary
--full` then faulted at a different address and mechanism:

    CR2 = RIP = 0x1
    Raw Error Code = 0x15       (present, write, user, fetch)
    pte = 0x0000000000000003    (present, write, NO user)
    PTE PRESENT, phys 0x0000000000000001
    pmm: pml4=2 pdpt=2 pd=2 pt=2    (every page PAGE_TABLE)

A `pmm_get_page_type` diagnostic in `isr14_handler` proved the walk's
pages were all `PAGE_TABLE`, ruling out a use-after-free of a
page-table page.  The mechanism was never isolated, and the fault
has not been seen since.  Item 7's instruction was to **re-apply the
diagnostic before redoing the signature change**, and to treat the
fault as unknown.

**What this session did, in order:**

- **`20261003-vmm-int-return`** — the signature change only, with
  the `isr14_handler` diagnostic in the same commit.  `vmm_map_page`
  and `vmm_map_page_in_cr3` return `int` (0 = mapped, -1 =
  page-table allocation failed); the six `if (!phys) return;` sites
  became `return -1;`; both functions `return 0` on success.  The
  huge-page split's halt was left alone — it already halts with
  `VMM: FATAL` (session 42).  Two `kmain.c` inline `extern`s
  (`test_map`, `test_nx`) changed to match.  **No caller handled the
  return yet** — the point was to isolate the signature change from
  the caller changes.  Verified on both boot paths; the
  `pmm_get_page_type` diagnostic **did not fire**, so the session-45
  fault did not reproduce.

- **`20261003-vmm-callers`** — the nine callers handle the `-1`,
  in the shape the callers themselves dictated:

  | Site | Class |
  |---|---|
  | `vmm.c`, `ensure_hhdm_mapped` | **halt** — no caller to report to |
  | `vmm.c`, `vmm_init`'s identity map | **halt** — boot |
  | `elf.c`, `elf_load_into_process` | recover — returns 0; both callers check |
  | `process.c`, `process_create`'s stack loop | recover — `return NULL` |
  | `heap.c`, `heap_extend` | recover — unmaps and frees its partial region, returns 0 |
  | `user_syscall.c`, `sys_mmap` | recover — `-ENOMEM` |
  | `user_syscall.c`, `sys_brk` | recover — returns `old_brk`, per the brk ABI |
  | `user_syscall.c`, `exec_alloc_user_stack` | recover — returns 0 |
  | `user_syscall.c`, `sys_fork`'s `EAGER_COPY_REGION` macro | recover — destroys the child, `-ENOMEM` |

  `heap_extend` is the one nontrivial site: `heap_brk`/`heap_mapped`
  advance only on full success, so a partial extension is invisible
  to the heap's own bookkeeping *and* to process exit (the heap uses
  the raw PMM, not `elf_add_page_to_pcb`).  On failure it unmaps and
  frees every page the call allocated.

  **Item 7 is closed.**  The session-45 virtual-1 fault did not
  reproduce on either boot path, with the diagnostic in place to
  catch it if it had.

### The defect the item-7 work surfaced

Reading the nine call sites turned up something adjacent: in
`process_create`,

    pcb->cr3 = vmm_clone_page_table(current_cr3);

**had no check.**  A `0` return became `pcb->cr3 = 0`, and the next
`vmm_map_page_in_cr3(pcb->cr3, …)` walked page tables at
`HHDM_START + 0` — a wild write into physical page 0's HHDM alias.
For a kernel-mode `entry_point` it avoided the mapping but still
handed out a PCB whose `cr3` would be loaded into `%cr3` by
`context_switch.asm`.  Either way it is worse than a leak.

The same function's failure exits also leaked: the user-stack loop's
two `return NULL`s and the kernel-stack-pool exhaustion exit left
the cloned page-table hierarchy, the pages already in `elf_page_list`,
a live pid, and `process_count` all behind.

**`20261003-process-create-cleanup`** fixes both: a new
`process_free_clone(uint64_t cr3)` walks the cloned PML4 and frees
entries 0..255 and the PML4 itself, skipping `RECURSIVE_PML4_INDEX`,
skipping entries >= 256 (shared with the parent — the HHDM and the
kernel image), and skipping 2 MB huge PDEs (leaves, not tables).
The four failure exits now run
`process_cleanup_elf_pages` + `process_free_clone` + the PCB-slot
bookkeeping.

**No test.**  All four are failure paths, and nothing on a healthy
boot runs them.  The commit message says so: "correct by inspection;
UNEXERCISED."

### The exit-path leak, and the run that verified it

**`20261003-exit-frees-tables`** closes the same leak on the
*success* path: `process_reclaim` and `process_destroy` freed a
process's ELF pages and its kernel stack slot, but abandoned the
cloned page-table hierarchy when the PCB slot was recycled.

Calling `process_free_clone` from those two functions is correct for
a process that has *run*, even though the function was written for
one that never did.  On the exit path the cr3 is still the clone,
but its low half has grown — `elf_load_into_process`,
`exec_alloc_user_stack`, `sys_brk`, `sys_mmap`, `sys_execve`, and
`munmap` all added or removed tables under it.  The walk frees every
present table it finds, which is right: `vmm_clone_page_table`
deep-copied the low half, so every PDPT/PD/PT under index 256
belongs to this process.  The walk frees **tables only**, never the
data pages the leaf PTEs point at; `process_cleanup_elf_pages` runs
first and frees those, and the two are disjoint.

**This commit is what exercised `process_free_clone` for the first
time.**  `exec_churn` — 24 rounds of fork/execve/wait4, all children
exit 0 — ran the walk 24 times, and the `k`-shell selftest's three
exception children ran it 3 more.  No fault, and **no `PMM: WARNING
- Double free`** from `pmm_free_page`, which is where an overlap
between the table-free and the frame-free would have shown.

The helper's header comment was rewritten in the same commit: it
said "called only from `process_create`'s failure paths," which the
two new callers had just falsified.

### Dead code

**`20261003-dead-has-drive`** deletes `f_stat_with_retry`'s
`has_drive`: it computed the variable, cast it to `(void)`, and did
nothing with it, and the whole `if (r == FR_INVALID_NAME || …)`
block was dead — the function returned `r` unchanged either way.
All five call sites collapse the return to `OK`-or-`fatfs_errno`, so
no caller could observe the deletion.  The comment explaining why
the bare-name retry is gone was kept, reflowed to the top of the
function.

`kernel.bin` was **unchanged** at 160632 bytes after this commit —
the code was already optimized away at `-O2`, which is itself the
confirmation that it was dead.

### A process note

This session's own lesson, worth carrying forward: **the helper
commit 3 added was unexercised until commit 4 put it on a hot path.**
Before the `exec_churn` run, "the cleanup is correct" and "the
cleanup has never run" were the same state.  The two are recorded as
separate gotchas — "A function that has never run is correct by
inspection only" and "A comment that names its callers goes stale
the moment a new caller appears" — and the distinction (a claim
about *exercise*, not about *existence*) is why they are separate
from the older session-43 entry about a fix with no test.

The same session re-taught an older rule the cheap way: the first
attempt to write item 4's edits described them from memory of the
pre-commit-2 `process.c`, and the correction was to ask for the
current bytes and quote them.  See the handoff's "When editing a
large file, quote the bytes."

### Verification

Both boot paths, every commit:

| Test | Result |
|---|---|
| `canary` | **15 passed, 0 failed** |
| `canary --full` | **28 passed, 0 failed** |
| `selftest` (`k` path) | **17 passed, 0 failed** |
| `exec_churn` | **24 rounds, all children exit 0, EXEC_CHURN-ALL-PASS** |
| `mmap_stress` | **16 rounds, MMAP_STRESS-ALL-PASS** |

No `#PF`, no `#DF`, no `#GP`, no `Unknown syscall:`, no
`VMM: FATAL`, no `PMM: WARNING - Double free`, and no new
diagnostics from the failure paths — which is the expected result,
since every message the session added is on a path a healthy boot
does not take.

`kernel.bin` grew across the session from 158744 (commit 1) to
160696 (commit 4), the signature change plus the checks plus
`process_free_clone`.

The one-page PMM shift at commit 3 (`free=31967` → `31966`,
`KERNEL` 674 → 675) is the kernel image crossing a page boundary,
not a leak: the reservation line moved from `[0x100000, 0x301000)`
to `[0x100000, 0x302000)`.

### Scratch tags kept

All five `20261003-*` tags, local, not pushed.  `dev` is five ahead
of `origin/dev`; nothing is on `main`.

---

## Session 48 — the PMM zone scan wraps; the boot-time `#PF` is fixed

One commit on `dev`, scratch-tagged, unpushed.  **Not a milestone**
— a bug fix.  Found the cause of the intermittent boot-time `#PF`
at `0x400000` that had been tabled since session 46, fixed it, and
added two tests that drive the allocator free paths.

| Tag | What |
|---|---|
| `20261003-pmm-wrap` | `pmm_scan_zone` wraps; the allocator finds pages behind its cursor.  `mmap_stress`, `exec_churn`, `churn_helper`. |

### The bug

The `#PF` reproduced this session — first boot after adding
userland ELFs, cleared by the second or third, came back on the
next image change.  It was **not** item 7's silent
`vmm_map_page_in_cr3` returns.  It was the **PMM zone scan**:

`pmm_alloc_page` scanned for a free page starting at a cursor
(`pmm_next_low_page` / `pmm_next_high_page`) and moved **one
direction**.  The free paths rewound the cursor to a freed page on
the *near* side, but a page that had been free the whole time on
the **far** side was never reached: the scan starts at the cursor
and walks away from it.

Adding ELFs to the image pushed the cursor past such pages before
the boot ELF load ran, so the split's page-table allocation
returned 0 — **with `pmm_free_pages` healthy.**  The counter and
the scan disagreed, and the counter was right.  A reboot cleared
it because `pmm_compute_zones` resets the cursor to the start of
the zone.

**The tell:** an "Out of ... zone memory" message with a nonzero
`Free pages:` count.

### How it was found

Not by reasoning about the allocator — by reading the fault
capture and then instrumenting.  The session's order:

1. **The capture** (`DFAULT.txt`) showed both faults in one run:
   the `#PF` at `0x400000` (`pde = 0x400083`, the supervisor huge
   page), then a **double fault** with `CS = 0x33` (ring 3) but
   `SS = 0x2B` and a kernel `RSP` — a corrupted privilege state,
   reached via the `EXIT-FALLBACK` path in `scheduler.c` after the
   shell died.
2. **The allocator was read in full** (`pmm.c`).  The free paths
   *do* take `pmm_irq_save` and *do* rewind the cursor; the
   interrupt race the file's header comment warns about was
   already fixed.  What the scan never does is look on the far
   side of the cursor.
3. **The fix** wraps the scan: `pmm_scan_zone` tries
   `[cursor, far]` then `[near, cursor)`.  A failed scan does not
   move the cursor.  The per-type `phys < 0x200000` skip became a
   `min_page` parameter so both zones share one function.

### Why this was not item 7

Item 7 was the silent `if (!phys) return;` in
`vmm_map_page_in_cr3`: the caller cannot see that the mapping
failed.  That was a real defect.  But *this* bug is why the
allocation returned 0 in the first place.  **Fixing item 7 alone
would have turned "boots into a broken shell that double-faults"
into "refuses to boot the shell"** — a better failure, not a fix.
The session's first plan was the signature change (item 7); the
observation that it converts a corrupt boot into a failed boot is
what sent the work at the allocator instead.  Item 7 was then
closed in session 49.

### The two new tests

- **`mmap_stress`** — map / touch every page / unmap / remap, 16
  rounds plus a final.  Drives the `munmap` free path.  Chosen over
  a `brk` test because `sys_brk` never shrinks (`brk` moves the
  break up only; freeing happens at process exit and `munmap`).
- **`exec_churn` + `churn_helper`** — fork / execve / wait4, 24
  rounds.  Drives the **exit** free path and does 24 ELF loads per
  run, so the split that failed runs 24 times.  The child's **exit
  status** is checked, because "the ELF loaded but faulted
  immediately" is exactly the failure this catches.  `churn_helper`
  is a separate trivial "exit 0" program, deliberately not
  `envp_helper` (which exits 2 when its variable is absent and
  would make a churn run red for an unrelated reason).

### Verification

**More than ten consecutive boots on the trigger image** — two new
ELFs added, 48 files staged, the condition that reproduced the
fault — with no `#PF`, no double fault, no `EXIT-FALLBACK`.
`exec_churn` and `mmap_stress` pass on every run; `canary` 15/15
and `canary --full` 28/28.  **This is "does not reproduce in ten
plus boots", not a proof the fault can never occur** — and the
commit message says so.

### A process note

The first instinct when the walk needed to be seen was to add
`fprintf`s to `third_party/busybox/libbb/procps.c`.  That is wrong:
`third_party/` is gitignored and rebuilt by the toolchain, so the
edit is invisible to the repo and vanishes on the next build.  The
right instrument — used successfully for `ps` in session 47 — is a
**first-party test that reproduces the consumer's sequence**, plus
a **kernel-side trace** in our own file.  Recorded in the handoff's
working style as "Do not edit `third_party/`."

**Scratch tag kept:** `20261003-pmm-wrap`, local, not pushed.

---

## Session 47 — /proc per-pid files and the pid directory; `ps` works

Four commits on `dev`, scratch-tagged, unpushed.  **Not a
milestone** — a working feature with known edges.  Finishes the
`/proc` work session 44's seam started: `readdir("/proc")` lists
the pids, `/proc/<pid>/stat`, `status`, and `cmdline` read, and
`ps` and `pstree` are enabled and work.

| Tag | What |
|---|---|
| `20261003-proc-dir` | `/proc` and `/proc/self` are directories; `readdir` returns `self` |
| `20261003-proc-pids` | `readdir("/proc")` lists the live pids |
| `20261003-proc-stat` | `/proc/<pid>/stat`, with `/proc/self/stat` |
| `20261003-proc-ps` | `/proc/<pid>/status`, `cmdline`, and the pid directory; `ps` works |

### The four commits, and what each needed

**Commit 1 — `/proc` is a directory.**  `open_resolved` and
`stat_resolved` accept `/proc` and `/proc/self` and produce a
`FILE_KIND_DIR` slot whose `obj` is `PROC_DIR_SENTINEL` — a tag,
not a `DIR*`, because there is no FatFs directory behind it.
`sys_getdents64` synthesizes one entry, `self`, and the cursor is
`slot->end`.  `put_file_slot` checks for the sentinel before
`f_closedir`.  `fill_kstat_as_dir` reports `KSTAT_IFDIR | 0555`.
Test `proc_dir.c`, 8 checks.  **`ps` still printed nothing** — the
directory was necessary but not sufficient.

**Commit 2 — numeric pids.**  `process_get_pcb(int)` is added to
`process.c` and declared in `process.h`, because `pcb_pool` is
`static` there and `user_syscall.c` cannot index it.  `readdir`
grows a second phase: after `self`, one decimal pid per live
process, from the pool.  `proc_dir.c` gains checks 7 and 8 (a
numeric entry, and this process's own pid).  **`ps` still printed
nothing.**

**Commit 3 — `/proc/<pid>/stat`.**  `PROC_ENTRY_PID_STAT`; the
`(pid, tag)` pair packed into the slot's `obj`
(`PROC_OBJ_MAKE`/`PROC_OBJ_TAG`/`PROC_OBJ_PID`); `proc_build_stat`
emits the field order libbb's `procps_scan` parses with a fixed
`sscanf`.  Real fields from the `pcb`: pid, comm, state, ppid,
utime, stime, start_time, vsize.  Zeros where donix tracks nothing.
`open_resolved` and `stat_resolved` accept `/proc/<digits>/stat`
**and the literal `self`**.  Test `proc_stat.c`, 7 checks.
`cat /proc/self/stat` printed the right line.  **`ps` still printed
nothing.**

**Commit 4 — the rest, and the fix.**  `PROC_ENTRY_PID_STATUS` and
`PROC_ENTRY_PID_CMDLINE`; `proc_build_pid_status` and
`proc_build_pid_cmdline`; the read-path dispatch grows both cases;
`open_resolved` and `stat_resolved` accept the two files.  And the
fix for the skip: **`stat_resolved` accepts `/proc/<digits>` and
`/proc/<digits>/` and reports a directory.**  `ps` works.

### The bug: `ps` skips every entry on a directory stat

`ps` printed its header and no rows.  The cause is in
`libbb/procps.c`'s `procps_scan`, under `PSSCAN_UIDGID` (which
`ps`'s default flag set and `pstree`'s both have):

    if (flags & PSSCAN_UIDGID) {
        struct stat sb;
        if (stat(filename, &sb))     /* "/proc/<pid>/" */
            continue;                 /* skip the entry */
        sp->uid = sb.st_uid;
        sp->gid = sb.st_gid;
    }

`filename` has a **trailing slash**.  donix served the per-pid
*files* but not the per-pid *directory*, so `stat("/proc/1/")` fell
through to FAT and returned `-ENOENT`, and every entry was skipped
before any file under it was read.

**How it was found, in the order it took.**  The tests passed
(`proc_dir`, `proc_stat`, `proc_status`, `proc_walk`); the config
was right (`PS=y`, `!DESKTOP`, `PS_WIDE=y`); fd state was not the
cause (`proc_walk_fds` held six fds and the walk still worked); the
kernel trace showed `ps` opening `/proc`, calling `getdents64` six
times, and then **nothing** — no row, and no open of
`/proc/<n>/cmdline` from the row printer.  So `procps_scan`
returned NULL after draining the directory, and the only `continue`
not yet reproduced was the `PSSCAN_UIDGID` stat.  `proc_walk` was
extended with `stat("/proc/<n>")` and `stat("/proc/<n>/")`, and
both printed `FAILED ... <-- procps_scan would SKIP` for every pid.
That was the bug.

### Two process notes, both worth keeping

- **The diagnostic was a kernel trace, not a `third_party/` patch.**
  When the walk had to be seen, the first proposal was to add
  `fprintf`s to `libbb/procps.c`.  That is wrong: `third_party/` is
  gitignored and rebuilt by the toolchain, so the edit is invisible
  to the repo and vanishes on the next build.  The right instrument
  is a **first-party test that reproduces the consumer's sequence**
  — `proc_walk` — plus, when needed, a **kernel-side trace** in our
  own file.  Both are committable; the `third_party/` patch is not.
- **`proc_walk` did not call `stat` on the directory.**  It opened
  the files and ran the consumer's `sscanf`, so it proved every
  *file* worked.  It did not make the one *directory* call
  `procps_scan` makes.  A test of the files under a directory says
  nothing about whether the directory can be stat'd.  See
  `gotchas.md`, "A /proc consumer can stat a path it never opens."

### Busybox

`configs/busybox.config`: `CONFIG_PS=y`, `CONFIG_FEATURE_PS_WIDE=y`,
`CONFIG_PSTREE=y`.  `PS_LONG` and `PS_TIME` stay **off** — they pull
in `time()`/`localtime()` → `gettimeofday`, which donix does not
implement (syscall 99).  `DESKTOP` stays off, so it is the simple
`!DESKTOP` `ps_main`.

**`ps` works:**

    $ ps
      PID USER       VSZ STAT COMMAND
        1 0            0 RW   idle
        2 0           92 S    musl_sh
        3 0          364 S    busybox
        4 0          424 R    busybox

The `USER` column reads `0` because `/etc/passwd` does not exist;
`get_cached_username` falls back to the numeric uid.  The
`sys_open: f_open FAIL path=etc/passwd` line is that fallback.

**`pstree` works but shows only `idle`** — which is correct for
donix's process model: `musl_sh`'s `ppid` is 0, so the shell is a
second root, not a child of pid 1.  `pstree` starts at pid 1 and
prints pid 1's subtree.  Not a bug; a consequence of the shell
having no parent process.

### Known edges, not closed by this session

- **`open("/proc/<pid>", O_DIRECTORY)` is not done.**  `ps` *stats*
  the directory; it does not open it.  `ls /proc/1` would need
  `open_resolved` to accept the same two paths `stat_resolved` now
  does.
- **`stat("/proc/<pid>/")` for a pid with no live process still
  reports a directory.**  The check is on the path shape, not on
  `process_find_by_pid`.  `ps` only stats pids `readdir` gave it, so
  it does not affect the consumer.
- **`sys_gettimeofday` (99) is not implemented**, so `PS_LONG` and
  `PS_TIME` stay off and `ps -l` still hits the unknown syscall.
- **`proc_walk` and `proc_walk_fds` report and pass** — they are
  diagnostics, not assertion tests.  Now that the answer is known,
  the assertion version is the permanent regression test.

### Verification

`proc_dir` 8/8; `proc_stat` 7/7; `proc_status` ALL PASS;
`proc_walk` runs the consumer's own `sscanf` and reports `n=11` for
every pid; `ps` lists four processes.  The tabled boot-time `#PF`
did not appear in any boot this session.

**Scratch tags kept:** the four `20261003-*` tags, local, not
pushed.

---

## Session 46 — the boot-time `#PF` is tabled; the next session is busybox enablement

One commit on `dev`, untagged, unpushed.  **Not a milestone** — a
docs change that tables the boot-time `#PF` and re-points the next
session.  No kernel change.

| Tag | What |
|---|---|
| (none — docs commit) | `handoff: table the boot-time #PF; point the next session at busybox enablement` |

**What it does.**  The boot-time `#PF` at `0x400000` is
**intermittent and allocator-state dependent**, and is not
currently observed.  Rather than being the next session's work, it
is now **TABLED** in `open-issues.md` item 7.  The handoff is
rewritten so a fresh session starts on feature work, and the next
session is pointed at **busybox enablement**.

- `open-issues.md` item 7: marked TABLED; the "do not tag a `v*`"
  gate is lifted; session 45's findings (the virtual-1 fault, the
  `pmm_get_page_type` result, the `vmm_clone_page_table` lessons)
  are folded into the item so they are not lost when a session
  reopens it.
- `handoff.md`: the warning block, the "NEXT SESSION — item 7"
  section, and "Session 45, and what it reverted" are removed or
  replaced; the busybox-enablement table is reframed as a **cost
  estimate** ("what each applet needs"), not a prohibition.  The
  stale `BOOT_PF.TXT` reference is corrected — that file is not on
  disk; `PFcapture.txt` holds presentation 2.  The stale HEAD line
  and the stale "item 1 must be rewritten" paragraph are removed.
- `gotchas.md`: a new entry, "A redirection binds to the last
  command in an `&&` chain" (session 45's `run` build-capture
  finding).

**Session 45, in one line, for the record.**  Session 45 attempted
the item-7 kernel fix three ways and reverted all three; **no
kernel change was committed.**  Its one kept commit is `6cfb0e6`
(the `run` script's build-and-capture fix), which rides on `dev`
untagged.  Its findings live in `open-issues.md` item 7.  There is
no session-45 section in this log; the tabled item is the record.

**Session 48 note:** the allocator half of what session 45 was
chasing turned out to be the PMM zone scan, and it is fixed.  The
rest of session 45's findings are still open.

---

## Session 44 — the pathname dispatch seam

Six commits on `dev`, scratch-tagged, **unpushed**, **not tagged
`v*`**.  Opens the `/dev`+`/proc` direction that `ROADMAP.md` calls
the next major work.  It is the first feature the previous
architecture **could not express at all** — `resolve_against_cwd`
plus `fat_lookup` cannot produce `/proc/self/status`, because there
is no FAT entry and never will be — so it is the feature that forces
the pathname dispatch seam into existence.  The seam landed, with
three consumers, and `tty` now prints a path.

| Tag | What |
|---|---|
| `20261002-seam` | `resolve_at` returns a backend tag (FAT/DEV/PROC); `path_backend()` computes it from the first component; every path syscall adopts the new signature; five callers move off `resolve_against_cwd`; cwd-overflow errno becomes `-ENAMETOOLONG` |
| `20261002-dev-null-backend` | `g_dev_table[]` + `dev_lookup()`; **`path_is_devnull` and its three call sites deleted**; `open_resolved`/`stat_resolved`/`access_resolved` take the tag; `fill_kstat_as_chardev` extracted |
| `20261002-proc-status` | `g_proc_table[]` + `proc_lookup()`; `FILE_KIND_PROC`; `proc_build_status()`; `FILE_KIND_PROC` cases in `sys_read`/`put_file_slot`/`sys_fstat_body`; five real fields |
| `20261002-dev-console-tty` | `/dev/console` (`FILE_KIND_DEV_CHAR`, stat-able not openable); `fill_kstat_as_chardev` takes `(st_dev, st_ino)`; `proc_readlink()` + the `sys_readlink` backend branch; **`tty` prints `/dev/console`**; fixes a pre-existing `fstat(0)` bug |
| `20261002-canary-tty` | one read-only canary row: `busybox tty` must print `/dev/console` |
| `20261002-execve-seam` | `sys_execve` calls `resolve_at`; non-FAT backend is `-ENOEXEC`; the `"0:" + path` retry is **deleted** — it was unreachable |

**The seam, in one paragraph.**  `resolve_at` returns, alongside the
resolved absolute path, a backend tag from the path's first
component.  FatFs serves `BACKEND_FAT`; a small table
(`dev_lookup`, `proc_lookup`) serves `BACKEND_DEV` and
`BACKEND_PROC`, or falls through to FAT for an unknown path under
those prefixes.  **No VFS**: no inode, no vnode, no mount table.
`path_backend()` is the one place the mapping lives; `resolve_against_cwd`
now has exactly one caller (`resolve_at`).  See `docs/strategy.md`,
"When a feature may force architecture."

**Three consumers, and what each proves.**

- **`/dev/null`** — `open`, `stat`, `access`.  Before the seam this
  was `path_is_devnull`, an exact-path predicate in three call sites.
  After, it is a table entry.  The seam's first DEV consumer.
- **`/proc/self/status`** — a synthesized *file*: `open`, `read`
  (five real fields — `Name`, `Pid`, `PPid`, `Uid`, `Gid`), `stat`
  (`S_IFREG | 0444`), `close`.  The seam's first PROC consumer.
- **`/dev/console` + `/proc/self/fd/N`** — the first positive
  `readlink` in the tree.  `readlink("/proc/self/fd/0")` returns
  `/dev/console`; `stat("/dev/console")` reports `S_IFCHR` with
  `(st_dev, st_ino) = (1, 1)`, **the same pair `fstat(0)` reports**.
  That match is `ttyname_r(3)`'s gate 3b, and it is why **`tty`
  prints `/dev/console`** instead of `not a tty`.

**A pre-existing bug the new test found.**  `sys_fstat_body` used
`get_file_slot`, which refuses fds below 3, so `fstat(0)` on a
console sentinel returned `-EBADF` **before reaching the
`FILE_KIND_CONSOLE` case that would have answered it** — since
console sentinels were introduced.  The new `proc_fd` test reported
exactly that one failure.  Fixed with `get_file_slot_any`, the same
relaxation `sys_close`/`sys_read`/`sys_write`/`sys_dup2`/`sys_fcntl`
already have.  See `gotchas.md`, "A case in a switch is not reached
if an earlier guard refuses the input."

**The dead retry commit 6 deleted.**  `sys_execve` had a `"0:" +
path` retry whose condition was `exec_path[0] == '/'` — but
`strip_dot_prefix` ran **before** it and removed the leading `/`, so
the condition was false by construction.  The retry was **not
unused; it was unreachable**, and the distinction is the gotcha:
unused code has no caller, unreachable code has a caller whose path
can never satisfy the guard.  Deleted in commit 6, with a comment
recording why.  See `gotchas.md`, "A fix can make an earlier branch
unreachable."

**A build error, and the gotcha it became.**  `proc_readlink` was
placed next to `proc_lookup` for thematic locality — and calls
`safe_copy_to_user`, which is defined **later in the file** with no
forward declaration above line 649.  The compiler caught it
(implicit declaration, then "static declaration follows non-static").
Fix: one line in the forward-declaration block.  See `gotchas.md`,
"Placing a function near its conceptual neighbors does not place it
after its callees."

**Verification.**  canary **15/15** (read-only) and **28/28**
(`--full`); `readlink_errno` 3/3; `at_step1` 10/10; `at_step2` 8/8;
`proc_status` **ALL PASS** (new); `proc_fd` **ALL PASS** (new);
`pipe_step1/2/3/3b` all OK (run for commit 2's `FILE_KIND_*`).
Boot clean, no `Unknown syscall:` lines, no faults.  `tty` prints
`/dev/console`.

**New tests:** `userland/musl/tests/proc_status.c` (9 checks) and
`proc_fd.c` (7 checks).  Both added to `USERLAND_ELFS` and the
`mcopy_one` chain; staged as `::/usr/bin/PROC_STATUS` and
`::/usr/bin/PROC_FD`.  The image now stages **41 files**.

**Busybox: no applet turns on from the seam.**  `ps`/`top`/`kill`/
`pidof` need `readdir("/proc")` and `/proc/<pid>/...`, which the
seam does not provide; `less`/`more` need `/dev/tty` and raw mode,
which it does not provide.  **`tty` is the one applet whose behavior
changed** — it was already enabled and printed `not a tty`; it now
prints `/dev/console`.  The seam's payoff is that the next `/proc`
piece (`readdir`, per-pid entries) is a table entry and a directory
shape, not an architectural change.

**A process failure, recorded because it cost four build cycles.**
The edits to `user_syscall.c` in commits 4 and 6 were described from
memory ("the block above", "after line N") rather than quoted from
the file, and four builds failed before the file was re-read and the
edits quoted.  The rule added to the handoff's working-style section:
**when editing a large file, quote the bytes.**  It is the same
lesson as the whole session — a claim about the source is checked
against the source, not against the claim.

**Open, and not closed by this session:** `readdir("/dev")` and
`readdir("/proc")` fail (the seam serves entries, not directories);
`st_rdev` is 0 on device nodes; `f_stat_with_retry` has dead
`has_drive` at line 3077; `open-issues.md` item 1 is **now stale**
(it says the seam has not landed) and must be rewritten.

**The vmm bug is the next session, and nothing tags `v*` before it.**
`BOOT_PF.TXT` — the huge-page-split `#PF` at `CR2 = 0x400000`,
before any user code ran.  It reproduces on the first boot after an
image rebuild that adds programs and clears by the second or third.
`open-issues.md` item 7.  See `handoff.md`.

**The session's headline is not "the seam works."**  It is that
every claim about the source was checked against the source, and the
one that was not — the edits described from memory — cost four build
cycles.  The seam is the work; reading before writing is what made
it correct.

---

## Session 43 — `realpath`, and the `readlink` errno closed by test

Two commits on `dev`, after `v0.6.9` was tagged and pushed.  A tail
on `v0.6.9`.  **The detail is in `ROADMAP.md`'s `v0.6.9` section
and the `20261002-realpath` / `20261002-readlink-test` tag
annotations.**  In one paragraph:

`CONFIG_REALPATH=y` (config-only — the trace showed the applet
routes through musl's `realpath()` plus `libbb`'s `xmalloc_readlink`
and `getcwd`, all present).  Two of the handoff's own test
expectations were wrong: `realpath /nonexistent` *succeeds* (its
parent `/` exists; the failing case is a path whose parent does not
exist), and the diagnostic goes to stderr.  The run sharpened the
`/dev` entry — `2>/dev/null` does not merely fail to discard, it
stops the command from running, because the shell opens the redirect
target before forking.  And `sys_readlink` already returned
`-ENOENT` for a missing path and `-EINVAL` for an existing
non-symlink; the fix had been in the tree, untested, since session
42.  `tests/readlink_errno.c` calls `readlink(2)` directly (no
applet does) and asserts both answers — 3/3, closing item 8.  See
`gotchas.md`, "A fix with no test is indistinguishable from an
unfixed defect."

**Tags:** `20261002-realpath`, `20261002-readlink-test`.

---

## Session 42 — envp, the `/usr/bin` layout, the shim removal (opens `v0.6.9`)

**The `v0.6.9` milestone.**  Twenty-five commits on `dev`, in two
parts.  **The full narrative is in `ROADMAP.md`'s `v0.6.9` section
and in the `20261001-*` tag annotations**; the log records the shape
and the two bugs it found.

**Part 1 — the opening six commits** (the milestone's original
scope):

| Tag | What |
|---|---|
| `20261001-envp` | `sys_execve` copies `envp` onto the new stack; argv region 4 KB → 16 KB; envp snapshot kmalloc'd |
| `20261001-env-applets` | config: enable busybox `env` and `printenv` |
| `20261001-envdocs` | session-42 docs (the first pass) |
| `20261001-usrbin` | executables to `/usr/bin`; `canary.c` the smoke test |
| `20261001-nosuffix` | drop the `.ELF` suffix; binaries staged bare |
| `20261001-noshim` | `execve`: remove the bare-name attempt; (a) and (b) remain |

**Part 2 — the 19 commits that followed, because each was the next
thing the last one exposed.**  A test for envp that found a
`%rax`-clobber bug; a rewrite of `musl_exec2`; the huge-page-split
`#PF` fix and the recording of item 7; four syscalls (`readlink`,
`clock_gettime`, `nanosleep`, `munmap`); ten busybox applets; three
small gaps (`fcntl` low fds, Ctrl- `[`, `munmap`); and four gotchas.

**The two bugs, both in the tests:**

- **The `%rax` clobber.**  `puts_raw`'s inline `syscall` asm declared
  `%rax` only as an *input*, so GCC believed `%rax` survived the
  block and issued a second `syscall` with the first's return value
  as its number — `Unknown syscall: 18446744073709551578`
  (`2^64 - 38`, the bit pattern of `-ENOSYS`).  Fixed with an
  `"=a"(ret)` output.  See `gotchas.md`.
- **Hand-counted string lengths.**  Eight of `envp_step1.c`'s
  fourteen `puts_raw` literals were wrong by one or two — invisible
  on a console (a NUL prints as nothing) until one dropped a
  newline and ran two lines together.  Fixed by removing the length
  parameter; the callee computes it.  See `gotchas.md`.

**The page-table bug the shim removal surfaced.**  An intermittent
`#PF` at `0x400000` — the huge-page split in `vmm_map_page_in_cr3`
silently returning on allocation failure, leaving a supervisor page
where a user page was asked for.  Fixed in `20261001-splitfix`
(halt with `VMM: FATAL`); the six remaining silent sites recorded
as `open-issues.md` item 7.

**Canary:** green from both shells, `canary` 14/14 and
`canary --full` 27/27; kernel self-test 17/17.

**New tests:** `envp_step1`, `musl_exec2` (rewritten),
`fcntl_lowfd`.  The image staged 38 files.

**Scratch tags kept:** all 24 `20261001-*` tags, local, part of the
open `v0.6.9` milestone at the time — dropped at the bump.

---

## Index — sessions 34 through 41

Each row's detail is in `ROADMAP.md` (the "Done — Phases A through
B" and `v0.6.6`/`v0.6.7`/`v0.6.8` sections), the `v*` tag
annotations, and `git log`.  One line each, for order and quick
reference.

| Session | Milestone / tags | What it did |
|---|---|---|
| 41 | `v0.6.8` | `faccessat` (269) and `utimensat` (280) close the `*at()` family.  `sys_faccessat` must **not** validate flags — musl calls it with three arguments, so `%r10` holds the previous syscall's return value.  `sys_utimensat` must, because musl passes four.  Also a `musl_wait` spin bisect (pre-existing; the console, not `fork`) and the incremental-build trap. |
| 40 | `v0.6.8` | `unlinkat` (263) implemented and shown to have **no consumer** in busybox (`rm -r` uses `lstat`+`unlink`+`rmdir`).  The feature that mattered was the `unlink`/`rmdir` type check in the same commit.  `at_step2.c`. |
| 39 | `v0.6.8` | `20260930-at`: `resolve_at`, `file_slot_t.dir_path`, `newfstatat` (262), `openat` (257); stat family inverted; `find -type` enabled.  Also the software block cursor and the PIT-rate comment fix. |
| 38 | `v0.6.7` | Framebuffer console: VBE mode 0x118, Terminus 10×18, glyph blitter, dual-backend cell primitive, shadow grid; `vi` fills the screen.  Byte order is BGR, not RGB. |
| 37 | `v0.6.7` | `musl_sh` becomes a real shell: tokenizer, redirection, sequences, pipelines.  Also `cat` gains stdin mode.  The `-EPIPE`-without-`SIGPIPE` prediction was **wrong** — `yes | head` does not hang. |
| 36 | `v0.6.6` | `pipe(2)` (22) in five steps; blocking, EOF, `-EPIPE`, dup-aware counts, exit-path wake; `dup(2)` (32); the stdio-guard inversion.  Keyboard fix (Shift+backslash). |
| 34 | (pre-`v0.6.6`) | Low fds (0/1/2) first-class: console sentinels, `alloc_file_slot` from 0, `dup2`/`fcntl` accept 0/1/2.  Fixes `uniq`. |

---

## A note on this file's shape

This file was 1575 lines and growing without bound before session 48.
Every session appended, and much of what it held — the per-commit
rows — duplicated the commit messages and the annotated `v*` tags.
It was restructured in session 48: recent sessions in full, recent
milestones condensed, older sessions indexed, with pointers to where
the full narrative actually lives.

**What stays here:** the per-session narrative that no commit or tag
carries — the order things happened in, the bugs found along the
way, the canary counts, the new-test lists, and the attempts that
were abandoned.

**What does not:** the per-commit tables.  `git log` and the tag
annotations have them.
