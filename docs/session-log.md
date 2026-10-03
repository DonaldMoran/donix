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

**Known issue with this file.**  Session 44's section is at the
**bottom** of the file, after session 34, instead of at the top
where the newest-first convention puts it.  That misordering is
noted here and **not** fixed in this change — reordering a file
this size from a paste is the edit the working-style rules say to
avoid.  Fix it in a separate change when the file is next
rewritten.

## Session 42 — envp, the `/usr/bin` layout, and the shim removal

Six commits on `dev`, scratch-tagged, unpushed.  Opens `v0.6.9`
with envp, and then — because envp exposed it — moves the binaries
to `/usr/bin` and removes the kernel's bare-name resolution.  Two
subjects, one session, in the order they happened.

| Tag | What |
|---|---|
| `20261001-envp` | `sys_execve` copies `envp` onto the new stack; argv region 4 KB → 16 KB; envp snapshot kmalloc'd |
| `20261001-env-applets` | config: enable busybox `env` and `printenv` |
| `20261001-envdocs` | session-42 docs (the first pass) |
| `20261001-usrbin` | executables to `/usr/bin`; `canary.c` the smoke test |
| `20261001-nosuffix` | drop the `.ELF` suffix; binaries staged bare |
| `20261001-noshim` | `execve`: remove the bare-name attempt; (a) and (b) remain |

**`v0.6.8` was bumped and pushed mid-session** (tag `v0.6.8` at
`b64a4fa`, merged to `main`), and the nine `2026*` scratch tags for
that milestone were dropped at the bump.  The `v0.6.9` work begins
with `20261001-envp`.

---

### Session 42, continued — the 19 commits after the shim removal

The opening six commits were the milestone's original scope.  What
followed was not planned: a test for envp that found a `%rax`-clobber
bug, a rewrite of `musl_exec2`, a page-table bug the shim removal
surfaced, four syscalls, ten busybox applets, three small gaps, and
two doc cleanups.  `v0.6.9` grew from three subjects to a large
milestone.  All 19 are tagged; the table below is the rest of the
session.

**The envp regression test and what it found**

| Tag | What |
|---|---|
| `20261001-envtest` | `tests/envp_step1.c` + `envp_helper.c` — envp survives `execve`, 3 checks |
| `20261001-lenfix` | `puts_raw` computes its own length; `musl_exec2` rewritten; two gotchas |

The envp test was the handoff's own next-session item 1.  It forks
three times and `execve`s a helper with one variable, two variables,
and an empty envp, checking the child's **exit status** (not stdout —
donix cannot capture a child's output).  It proved the session-42
envp code by failing against pre-envp `execve`, and it surfaced two
bugs, both in the test itself:

- **The `%rax` clobber.**  `puts_raw`'s inline `syscall` asm declared
  `%rax` only as an *input*, with no output and no `"rax"` clobber.
  GCC believed `%rax` survived the block, so two `puts_raw` calls
  back to back issued the second `syscall` **without reloading
  `%rax`** — and it ran with the first syscall's return value as its
  number.  The log:

      Unknown syscall: 41
      Unknown syscall: 18446744073709551578

  `18446744073709551578` is `2^64 - 38`, the bit pattern of the
  `-ENOSYS` that syscall 41 (`socket`) returned.  A syscall number
  that is the previous syscall's *return value* is only possible if
  `%rax` was never reloaded.  Fixed by giving every syscall asm an
  `"=a"(ret)` output.  `musl_exec.c` and `musl_exec2.c` had the same
  latent `puts_raw`; their callers always reset `%rax` after, so it
  never showed.  See `gotchas.md`, "An input-only `syscall` asm
  block does not tell GCC that `%rax` is overwritten."

- **Hand-counted string lengths.**  Of `envp_step1.c`'s fourteen
  `puts_raw` literals, **eight were wrong** — off by one or two.
  A length one too long writes the string's NUL terminator as a
  byte, invisible on the console but a one-byte over-read of
  `.rodata` and a fault the day a string ends at a page boundary.
  A length too short drops the trailing newline, which is how it
  was noticed: `MUSL_EXEC2-ALL-PASS` ran together with the kernel's
  `EXIT:` line.  Fixed by removing the length parameter entirely —
  `puts_raw` computes it.  There is no compile-time check on a
  hand-counted length, so recounting is not the fix.  See
  `gotchas.md`, "A hand-counted string length in a syscall wrapper
  will be wrong."

**`musl_exec2` rewritten.**  It asserted `sys_execve`'s bare-name
retry (removed by `20261001-noshim`) and the `.ELF` suffix form
(removed by `20261001-nosuffix`), so all three of its checks were
asserting removed behavior and failing.  Inverted: one positive
(`execve("/usr/bin/HELLO")` resolves) and two negative
(`execve("HELLO")` and `execve("/HELLO.ELF")` do not).  Kept rather
than deleted, so a reintroduction of either form goes red.  The two
negative checks are the record of the shim removal.

**The page-table bug the shim removal surfaced**

| Tag | What |
|---|---|
| `20261001-splitfix` | the huge-page split in `vmm_map_page_in_cr3` halts on allocation failure instead of returning |
| `20261001-splitdiag-off` | removed the temporary `VMM: SPLIT` prints |
| `20261001-vmm-issues` | recorded the six remaining silent `vmm_map_page*` returns as item 7 |

An intermittent `#PF` appeared after the shim removal, on the first
exec, sometimes:

    === PAGE FAULT (#PF) ===
      CR2 (Faulting Address) : 0x0000000000400000
      Raw Error Code         : 0x0000000000000015
      pde                    : 0x0000000000400083
      PDE IS 2 MB PAGE, phys base 0x400000

`0x400083` is present, write, PS, `PT_USER` clear; `0x15` is
present + read + user + instruction-fetch.  The bootloader's low
identity map is 2 MB **supervisor** huge pages; when
`elf_load_into_process` maps the ELF entry page at `0x400000`,
`vmm_map_page_in_cr3` must split that huge page into 4 KB PTEs
before writing the user PTE.  The split allocates one page-table
page — and on allocation failure the function **returned without
splitting**, leaving the supervisor page in place.  The caller does
not check; the process later faults on a user fetch of a supervisor
page.  Intermittent because it depends on whether the allocator has
a table page at that instant.

**This is not the shim removal's fault.**  The shim removal changed
`sys_execve`'s control flow and timing; the defect is the silent
`return`.  Diagnostic confirmed the setup on ten clean boots
(`split` fires for `0x400000`, `pde=0x400083`, `carry_user=0`, and
succeeds); the failure path was **never reproduced**.  The fix is
made because the silent return is wrong on its own terms — it
leaves a supervisor page where the caller asked for a user page —
not because the failure was caught.  A diagnostic patch first,
then the fix (halting with `VMM: FATAL`), then the diagnostic
removed.

The six remaining `if (!phys) return;` sites in `vmm_map_page_in_cr3`
and `vmm_map_page` are recorded as `open-issues.md` item 7 — the
same defect, no evidence yet, a deliberate decision per site.

**Dead code and four syscalls**

| Tag | What |
|---|---|
| `20261001-deadname` | deleted `exec_resolve_bin_name`, dead since the shim removal |
| `20261001-readlink` | `readlink` (89): honest `-EINVAL` (no symlinks), `-ENOENT` for missing |
| `20261001-clock` | `clock_gettime` (228) from `g_ticks`; enables `mktemp` |
| `20261001-nanosleep` | `nanosleep` (35) as a `g_ticks` deadline loop; enables `sleep`, `usleep` |
| `20261001-munmap` | `munmap` (11) for real — the stub leaked; mallocng calls it |

**`exec_resolve_bin_name`** was left behind by the shim removal —
the callers were deleted, the function was not, and
`-Wunused-function` caught it.  This is the deletion-shaped half of
the gotcha the shim removal itself produced.  Deleting it also made
true a comment `sys_execve` still carried ("the helpers it called
are gone with it") — until this commit, one of them was not.

**`readlink`** is the honest answer for a system with no symlinks:
`-EINVAL` for a path that exists (Linux's answer for a non-symlink),
`-ENOENT` for one that does not.  It exists because musl's
`ttyname(3)` tries `readlink("/proc/self/fd/N")` before walking
`/dev`, and without a handler that logged `Unknown syscall: 89` on
every `tty` run.  It does **not** make `tty` print a path — after
`-EINVAL`, `ttyname` walks `/dev`, which does not exist.  The
number was confirmed against musl's `readlink.c` (`#ifdef
SYS_readlink` → 89).  Exercised by `tty`: `not a tty`, no unknown
syscall.

**`clock_gettime`** reports `g_ticks` (100 Hz PIT) as both
`CLOCK_REALTIME` and `CLOCK_MONOTONIC` — donix has no wall clock,
so the two are the same number.  It was needed by `mktemp`: busybox
`mktemp` → musl `mkstemp` → `__randname`, which seeds its `XXXXXX`
replacement from `__clock_gettime(CLOCK_REALTIME)`.  **Not**
`getrandom`, **not** `/dev/urandom` — just the clock, read from
`__randname.c`.  This corrected the handoff's enablement table,
which named `getpid`+`open`.  Exercised:

    $ mktemp
    /tmp/tmp.LimaPc
    $ mktemp /tmp/testXXXXXX
    /tmp/testABfoOf
    $ ls /tmp
    testABfoOf  tmp.LimaPc

**`nanosleep`** is a `hlt` loop on a `g_ticks` deadline — the shape
`sys_poll` and `sys_read` already use.  Duration rounds down to
whole 10 ms ticks; `rem` is never written (no signals to interrupt
a sleep).  musl's `nanosleep(3)` reaches this via
`__clock_nanosleep(CLOCK_REALTIME, ...)`.  Timed with a stopwatch:
`sleep 60` took one minute.

**`munmap`** was a stub returning 0.  That was wrong — **musl's
mallocng calls it** in `free()` (large-block release) and in
`malloc`'s error path.  The stub told `free()` memory was released
when it was not: a leak, not a no-op.  Now unmaps the rounded
range, frees the frames, and removes them from `elf_page_list` so
exit does not double-free.  Exercised by the kernel self-test's
`heap_stress` (4096 malloc/free ops, "no leak, heap intact") and by
`memtest` (`malloc(4096)` → `free()` → `malloc(8192)` at a nearby
address).

**Busybox applets — ten enabled**

| Tag | What |
|---|---|
| `20261001-applets-free` | `basename`, `dirname`, `unlink` |
| `20261001-applets-tty` | `ttysize`, `tty`, `arch` |
| `20261001-truncate` | `truncate` |

All config-only.  `basename`/`dirname` are string ops plus `write`;
`unlink` calls `unlink(2)` (87), already present.  `ttysize` reads
`ioctl(TIOCGWINSZ)`; `tty` prints `not a tty` (no `/dev`, no
`/proc` — correct for donix); `arch` is the `uname` applet under
another name (`applet_name[0] == 'a'`), already working.

**`truncate` is config-only and the handoff's table was wrong.**
The table listed it under "Needs one small syscall first" with
`truncate` (76).  Reading `coreutils/truncate.c` settles it: the
applet does `open()` then `ftruncate()` (77) — already implemented
since session 31.  It never calls `truncate(2)`.  Same "consumer
inferred from the name" error as `unlinkat`.  Exercised: a 6-byte
file truncated to 0, no unknown syscall.

**Three small gaps, the last of the NEXT-SESSION list**

| Tag | What |
|---|---|
| `20261001-fcntl-lowfd` | `sys_fcntl` accepts fd 0/1/2 for all subcommands; new `fcntl_lowfd` test |
| `20261001-ctrl-bracket` | Ctrl- `[` produces ESC (0x1B) |
| (`20261001-munmap`) | `sys_munmap` — counted here; it was gap 1 |

**`fcntl`** used `get_file_slot()` (refuses fd < 3) for everything
but `F_DUPFD`, so `fcntl(0, F_GETFL)` on a redirected fd 0 returned
`-EBADF`.  Now `get_file_slot_any()` throughout.  New test
`fcntl_lowfd.c`: console fd 0 → 0, redirected fd 0 → 0 (the fix),
closed fd 0 → `EBADF` (the control).  All three pass.

**Ctrl-`[`** produced `[` because `scancode_to_ascii` had no Ctrl
parameter.  Added `g_ctrl` (scancodes 0x1D/0x9D) alongside
`g_shift`/`g_caps`, and a fourth `ctrl` parameter; Ctrl- `[` maps to
`0x1B` before the table lookup.  Only Ctrl- `[` is mapped, not the
full Ctrl+letter range — the others currently produce their base
character and changing them would alter what the line editor sees.
Exercised in `vi`.

**Doc cleanups**

| Tag | What |
|---|---|
| `20261001-dev-issue` | open-issues: no `/dev`, no `/proc`; `ttyname` cannot name the console |
| `20261001-mktemp-issue` | open-issues: `mktemp` needs `clock_gettime`, not `getpid`+`open` |
| `20261001-issues-cleanup` | removed the resolved entries from `open-issues.md` |

The two "issue" commits recorded findings *while they were fresh*:
the `/dev` gap found by enabling `tty`, and the `clock_gettime`
dependency found by reading `__randname.c`.  The cleanup commit then
removed the four entries that were no longer true (`fcntl`,
Ctrl- `[`, `munmap`, `mktemp`) — an issues list that claims fixed
things are broken is one you learn to ignore.

**Gotchas added:** "An input-only `syscall` asm block does not tell
GCC that `%rax` is overwritten"; "A hand-counted string length in a
syscall wrapper will be wrong."  (The opening six commits added
"The kernel stack is 16 KB" and "A shim's dead code is only dead if
you watch it not run.")

**Canary:** green from both shells.  `canary` 14/14, `canary
--full` 27/27.  The kernel self-test runs at boot, 17/17.

**New regression tests:** `envp_step1`, `musl_exec2` (rewritten),
`fcntl_lowfd`.  The image stages 38 files.

**Scratch tags kept:** all 24 `20261001-*` tags are local, not
pushed, part of the open `v0.6.9` milestone — dropped at the bump.

---

### Session 43 — `realpath`, and the `readlink` errno closed by test

Two commits on `dev`, after `v0.6.9` was tagged and pushed.  Not a
new milestone — a tail on `v0.6.9` that closes out its applet work
and one of its open issues.

| Tag | What |
|---|---|
| `20261002-realpath` | `CONFIG_REALPATH=y` — enables busybox `realpath`; config-only |
| `20261002-readlink-test` | `readlink_errno.c` — readlink(2) errno split, 3 checks; closes item 8 |

**`realpath` — the trace, done before the edit.**  `realpath_main`
→ `xmalloc_realpath_coreutils` → `xmalloc_realpath` → musl
`realpath()`, plus `xmalloc_readlink` (readlink 89, present) and
`xrealloc_getcwd_or_warn` (getcwd, present).  No
`//config:depends on` for `CONFIG_REALPATH` — the applet does not
require the `readlink` applet, it uses `libbb`'s
`xmalloc_readlink`.  That is why the change is config-only.

**Two of the handoff's own step-4 test expectations were wrong,
and the run showed which.**  The handoff said `realpath
/nonexistent` should fail with a diagnostic.  It does not:

    $ realpath /nonexistent
    /nonexistent

Exit 0.  `xmalloc_realpath_coreutils` has an `ENOENT` fallback:
when musl's `realpath()` fails `ENOENT`, busybox strips trailing
slashes, splits at the last slash, canonicalizes the parent, and
re-appends the basename.  `/nonexistent`'s parent is `/`, which
exists, so the fallback succeeds.  **The failing case is a path
whose parent does not exist:**

    $ realpath /no/such/dir/file
    realpath: /no/such/dir/file: No such file or directory

**The diagnostic is on stderr.**  `realpath /no/such/dir/file >
log.txt` left `log.txt` **empty** while the message appeared on
the console — so the message did not go to stdout, and busybox's
`bb_simple_perror_msg` writes to stderr.

**A new finding, and it sharpens the `/dev` entry.**
`2>/dev/null` does not merely fail to discard stderr — it
**prevents the command from running**:

    $ realpath /no/such/dir/file 2>/dev/null
    sys_open: f_open FAIL path=dev/null flags=0x0000000000008241 ...
    sh: can't create /dev/null: nonexistent directory

    (realpath's own diagnostic never appears — the command did
     not execute)

The shell opens the redirect target before forking, the open
fails, and the whole command is abandoned.  Recorded in
`open-issues.md` under the `/dev` entry, with the acceptance test:
after the device layer lands, `realpath /no/such/dir/file
2>/dev/null` exits non-zero, silently.

**`readlink` errno — the fix was already in the tree.**
`sys_readlink` returned `-ENOENT` for a missing path and `-EINVAL`
for an existing non-symlink; it had since the session-42 readlink
commit.  `open-issues.md` item 8 still described the defect as
open, and so did the handoff and the ROADMAP.  **No applet calls
`readlink(2)`** — the only consumer is musl's `ttyname(3)`, which
cannot distinguish the errnos with no `/proc` and no `/dev`, so
nothing exercised the fix and nothing noticed it was done.

The evidence had been in hand and read past: the `realpath` trace
showed `xmalloc_readlink` collapses every `readlink` error to
NULL, which is only observable if the kernel returns the two
errnos differently — i.e. the fix was in.  That was written up as
"item 8 does not affect realpath's output" and stopped there.

`readlink_errno.c` calls `readlink(2)` directly, since no applet
does, and asserts both answers:

    readlink("/nonexistent")     -> -1, errno == ENOENT
    readlink("/usr/bin/HELLO")   -> -1, errno == EINVAL
    readlink("/")                -> -1, errno == EINVAL

All three pass.  Item 8 is closed with a run behind the claim, not
a reading.  See `gotchas.md`, "A fix with no test is
indistinguishable from an unfixed defect."

**The headline of the session is not "realpath works."**  It is
that reading the source before enabling caught a stale assertion
in the instructions, and the same habit would have caught the
second stale assertion (`readlink` item 8) before the session
spent rounds on it.  Both are the same shape: a document asserting
a state the repository does not have.

**The milestone, in one line:** `v0.6.9` shipped envp, the
`/usr/bin` layout, the shim removal — and then a test, a rewrite, a
page-table fix, four syscalls, ten applets, three gaps, and four
gotchas, because each was the next thing the last one exposed — and
session 43 followed it with `realpath` and the `readlink` errno,
both closed with a run.

---

### envp — `execve` passes the environment through

`sys_execve` ignored its third argument and wrote a single NULL as
the envp terminator, so every program ran with an empty
environment: `getenv` returned NULL, `env`/`printenv` printed
nothing, `$VAR` expansion in ash was always empty.

**Commit 1** (`envp`): Linux semantics — `execve` passes the
caller's `envp` through **verbatim**; it does not synthesize an
environment.  The shell builds it, the kernel carries it.

`sys_execve` section 3 snapshots `envp` from the OLD address space
before the teardown in section 5, exactly as argv is snapshotted.
Section 6 writes the SysV process-entry layout:

    rsp_init  -> argc
                 argv[0] ... argv[argc-1] NULL
                 envp[0] ... envp[envc-1] NULL
                 (argv strings, then envp strings, packed)

The slot after the argv array, which held a single NULL (the whole
envp), now holds the envp pointer array, NULL-terminated, with the
strings following.

**Two sizing decisions.**

- **The argv region grew from 4 KB to 16 KB.**  A shell's
  environment plus argv could approach 4 KB, and `MAINTENANCE.md`
  item 3l (frozen) warned the gap between the region and the
  child's downward-growing stack shrinks when envp is added.  The
  user stack is 64 KB; 16 KB of region leaves 48 KB for frames.
- **The envp snapshot is `kmalloc`'d, not a stack array.**
  `EXEC_MAX_ENVC` × `EXEC_MAX_ARG_LEN` is 64 × 256 = 16 KB, and
  `PROC_STACK_SIZE` is 16 KB (`process.h`).  A stack array that
  size on top of `sys_execve`'s existing frame would overflow.
  See `gotchas.md`, "The kernel stack is 16 KB; do not put a large
  scratch buffer on it."

**Commit 2** (`env-applets`): `CONFIG_ENV=y` and `CONFIG_PRINTENV=y`.
Both read the environment, so both are only useful now that `execve`
passes it.  Before this commit `busybox env` reported "applet not
found" — disabled, not broken.

**Tested — the `$FOO` round trip is the proof.**

    $ export FOO=bar
    $ echo $FOO
    bar

That is the whole feature: `export` puts `FOO` in ash's
environment, `execve` carries it into the child, the child reads
it.  `env` confirms it directly:

    $ env
    PWD=/
    $ export FOO=bar
    $ env
    FOO=bar
    PWD=/

**The `PATH` finding.**  `echo $PATH` inside ash expands to
`/sbin:/usr/sbin:/bin:/usr/bin`, and command lookup works — but
`printenv PATH` prints nothing and `env` does not list `PATH`.
That is because **busybox ash keeps `PATH` as a shell variable and
does not export it.**  `export PATH` then `printenv PATH` prints
the path, confirming the mechanism.  This is ash's behavior, not a
donix bug, but it means every child applet runs with no `PATH` in
its environment.

---

### The `/usr/bin` layout

Moving the binaries was not part of the envp milestone.  It came
from a question — "should the custom ELFs be somewhere other than
the FAT root?" — and the answer was yes, for a reason the two
shells make concrete:

- **ash's applets never consult `PATH`.**  So busybox always wins
  there, and `/bin` is where busybox lives.
- **`musl_sh` does consult its search list**, so a bare name should
  resolve to the donix-native tool.

That is the same precedence a normal Unix gives `/usr/local/bin`
over `/usr/bin`, and it *requires* the two tool sets to be in
different directories.  The layout became:

    /bin         busybox (the base toolset)
    /usr/bin     the donix-native ELFs (apps + tests)
    /tmp         empty
    /            HELLO-WORLD.TXT and other data

**`20261001-usrbin`**: `musl_sh`'s `run_external` searches
`/usr/bin/NAME` then `/bin/NAME`; `kmain` boots the shell from
`0:/usr/bin/MUSL_SH`; the Makefile stages every ELF to `::/usr/bin/`
and creates the directories **before** the copies.

**The mtools finding:** `mcopy` does not create intermediate
directories, and the first version put the `mmd` calls after the
copies.  Every copy failed with `no match for target`.  The mkdirs
have to run right after `mkfs.vfat`, before anything is copied.

**`20261001-canary`** (in the same commit): `tests/canary.c`, the
post-change smoke test.  The canary used to be a list of lines in
`handoff.md` a human read and retyped, and that drifts — the list
said `find / -type d` prints `/` and `/bin`, and after the layout
move it printed five directories, a change nobody noticed.  The
program runs every non-interactive canary row, checks exit status
and output substrings, and reports pass/fail.  `canary` is
read-only; `canary --full` includes the mutating rows.

**Two failures in the canary's own first run, both worth
recording.**

- **Every direct binary path was missing its `.ELF` suffix.**
  `canary.c` calls `execve` directly and does not get `musl_sh`'s
  suffix-adding search, so `/usr/bin/CAT` failed with 127 where
  `/usr/bin/CAT.ELF` was the file.  Seven rows failed for this one
  reason.
- **`find / -type f -name busybox` is a seven-word command line.**
  The `check4` and `check5` helpers cover four and five words; a
  row that uses the wrong helper does not fail loudly, it silently
  drops the trailing words and tests something else.  `check4` on
  that row ran `find / -type` with no argument.  It needed a manual
  `argv[]` build.

---

### The suffix removal

**`20261001-nosuffix`**: drop `.ELF` from the staged names and from
every search.

The `.ELF` suffix was a convention from when binaries lived at the
FAT root and the kernel guessed the name.  It became a problem the
moment the layout moved: **ash's `execvp` walks `$PATH` looking for
an exact filename and does not guess suffixes**, so `/usr/bin/HELLO.ELF`
is invisible to `hello`.  The suffix had to go for bare-name
lookup to work from ash at all.

Staged names became `/usr/bin/HELLO`, `/usr/bin/LS`, ...; `musl_sh`'s
`run_external` builds one form per directory; `kmain` loads
`0:/usr/bin/MUSL_SH`; `canary.c` names the binaries as staged.

**Verified from both shells, same result:**

    donix> canary --full   27 passed, 0 failed
    ash:   canary --full   27 passed, 0 failed
    hello, musl_printf      run by bare name from both shells

---

### The shim removal

**`20261001-noshim`**: remove `sys_execve`'s bare-name attempt.

`sys_execve` tried three spellings of a path: (a) the path as given,
(b) `"0:" + path` for an absolute path, (c) if the path was a bare
name, uppercase it, append `.ELF`, try it at the root and in
`/bin`.  **Attempt (c) could no longer succeed** — the binaries are
staged bare and live under `/usr/bin`, so all three sub-attempts
found nothing.

**Removed:** the attempt-(c) block, `exec_resolve_bare_name`,
`exec_resolve_bin_name`, their forward declaration, and **the retry
call in `f_stat_with_retry`**.

**The `f_stat_with_retry` call was the surprise.**  It was added in
session 22 for ash's `find_execable`, which calls
`access(candidate, X_OK)` on each `PATH` entry before exec'ing.
Without the retry, `access("sbin/ls")` failed and ash concluded the
command did not exist.  **But with the binaries in `/usr/bin`, the
`PATH` probe reaches `access("/usr/bin/ls", X_OK)`, and FatFs
resolves the multi-component path directly** — the retry never
fires.  The canary's `ls` rows passing **from ash** is the proof:
they go through `find_execable` → `access` → `f_stat_with_retry`,
and they found every binary.

**What remains** in `sys_execve` is (a) the path as given and
(b) the `"0:"` prefix translation — the smallest the shim can be
without a VFS.

**The lesson, and it recurred three times.**  "This code has no
consumer" was wrong three times this session:

1. **The shim itself.**  I reasoned that attempt (c) was dead code
   because the canary passed.  It passed because every row went
   through `canary.c`'s full paths or ash's *applets*.  The
   experiment — typing `hello` from ash with the binary only at the
   root — showed ash reaching the kernel's bare-name code.  The
   canary could not see the consumer.
2. **The `f_stat_with_retry` retry.**  I told you to delete the
   helper without checking its callers, and the compiler caught a
   call site I had not seen.  The fix was to delete the call too,
   but only after the canary proved the retry was dead.
3. **The kernel-side record of session 42** (below).

**The pattern:** a decision that was plausible, matched something
nearby, and was never tested against the thing that defines truth.
**The check is an experiment, not an inference** — run the thing and
see, rather than reason about whether it can work.  See
`gotchas.md`.

---

**Gotcha added:** "The kernel stack is 16 KB; do not put a large
scratch buffer on it."

**Canary:** green from both shells, `27 passed, 0 failed`, with
`canary.c` now the executable form of what used to be a hand-typed
list.

**Scratch tags kept:** all six are local, not pushed, part of the
open `v0.6.9` milestone.

## Session 41 — `faccessat` and `utimensat` close the `*at` family

Two commits on `dev`, scratch-tagged, unpushed.  Continues the
`v0.6.8` `*at()` family and closes it: `faccessat` (269) and
`utimensat` (280) were the last two `*at` syscalls with an actual
gap rather than a missing consumer.  Both now resolve through
`resolve_at`, the same resolver `openat`/`newfstatat`/`unlinkat`
use.

| Tag | What |
|---|---|
| `20261001-faccessat` | `access_resolved` extracted; `sys_faccessat` routes through `resolve_at`; flags NOT validated |
| `20261001-utimensat` | `sys_utimensat` routes through `resolve_at`; flags validated |

**Commit 1** (`faccessat`): `sys_faccessat` was a direct alias of
`sys_access`, so `faccessat(dirfd, "rel", ...)` with a real dirfd
resolved against the cwd, not the dirfd — the bug `open-issues.md`
had carried since the `*at` work began.  Fixed by extracting the
existence-check body into `static access_resolved(abs_path)` and
routing `sys_faccessat` through `resolve_at`; `sys_access` keeps
resolving against the cwd (`access(2)` has no dirfd) and shares
`access_resolved`.  No behavior change to `sys_access`.

**Commit 2** (`utimensat`): `sys_utimensat` ignored `dirfd` **and**
never called `resolve_against_cwd`, so both a real dirfd and a
relative path were mishandled.  Routed through `resolve_at` and
`access_resolved` (it stores nothing, so the existence check is the
whole body).  The NULL-path return changed from `-EINVAL` to
`-EFAULT`, matching Linux.

**The finding that matters — the flags register.**  The first
version of `sys_faccessat` validated its flags argument the way
`sys_newfstatat` and `sys_unlinkat` do: unknown bits → `-EINVAL`.
That was wrong, and `at_step1` caught it:

    [faccessat] dirfd=3 flags=0x00000000FFFFFFEA
    FAIL 8: faccessat(dfd, "busybox"): Invalid argument

`0xFFFFFFEA` is `-22` — the **return value of the previous syscall**
(section 7's bad-flag `fstatat`, which returns `-EINVAL`) still
sitting in `%r10`, the register the kernel reads as the fourth
argument.  musl calls this syscall with **three** arguments
(`syscall(SYS_faccessat, fd, filename, amode)` in
`third_party/musl-src/src/unistd/faccessat.c`), so it never writes
`%r10`, and the kernel was reading whatever the last call left
there.  The mask turned a valid call into `-EINVAL`, deterministically.

The fix is not "mask more carefully" — it is **do not validate an
argument the caller's wrapper does not set**.  Linux's own
`faccessat(2)` does not validate flags; only `faccessat2(2)` (439)
does, and donix does not implement 439.  `sys_faccessat` now accepts
and ignores `flags`.  See `gotchas.md`, "A syscall argument the
caller did not set holds the previous syscall's return value."

`sys_utimensat` is the opposite case and keeps its mask: musl passes
`utimensat`'s flags in the fourth argument (the `#else` branch of
`third_party/musl-src/src/stat/utimensat.c`, which is what runs on
x86_64 since there is no 32-bit `time_t`).  `AT_SYMLINK_NOFOLLOW` is
accepted and ignored; `AT_EMPTY_PATH` is refused with `-EINVAL`.
**The two syscalls differ on purpose, and the difference is which
one musl passes four arguments to** — not a style choice.

**Tested.**  `at_step1.c` gains sections 8–11: `faccessat(dfd,
"busybox")` → exists; `faccessat(AT_FDCWD, "busybox")` → `ENOENT`
(the control — proves dirfd is not ignored); `utimensat(dfd,
"busybox")` → 0.  Section 10 (the old `faccessat` bad-flag check)
was **removed**: its `EINVAL` comes from musl, before any syscall
is issued, so it never reached the kernel and could not fail.  A
test that cannot fail is not a test.  `at_step1` is 10/10, read-only,
not a canary row.

**Canary:** green.  Boot spine, `busybox ls`, `busybox pwd`,
`busybox ash` (with `pwd`/`cd /bin`/`pwd`/`ls`/`exit` inside it),
and the `rm`/`rmdir` rows (`touch`/`rm`, `mkdir`/`rmdir`, `rm -r` on
a tree) all behave.  No `Unknown syscall:` lines.

**A `musl_wait` observation, recorded because it prompted a
bisect.**  `musl_wait`'s WNOHANG loop prints a dot per poll and
visibly spun longer this session than remembered.  A bisect in a
scratch copy (`v0.6.6` vs `v0.6.7`) established:

- The spin is **pre-existing** — present at `v0.6.6`, so not from
  any `v0.6.8` work.  It is already on `open-issues.md`'s list
  ("`musl_wait`'s WNOHANG loop spins").
- The **wall-clock duration** increased between `v0.6.6` and
  `v0.6.7` — the interval in which the console changed from VGA
  text to the framebuffer.  Hypothesis: cost per `putchar` (glyph
  blit vs two text-memory writes), not dot count.  **Not isolated**
  by a raw character-output comparison; recorded as a hypothesis.
- The dot *count* was not measured at either tag.

**Not** a regression in `fork`, `wait4`, or anything this session
touched.  The `fork` eager copy (~7 MB per fork, including
read-only pages, per `sys_fork`'s own comment) is a standing
inefficiency, present at `v0.6.6` too; it is already the first
bullet under "Kernel hardening" in `ROADMAP.md`, and that bullet
now records that it is a standing cost, not a regression.

**A `make` staleness trap, recorded because it cost time.**
`make -C 04_kernel_64bit` reported "Nothing to be done" while
`user_syscall.c` had just been edited, because `kernel.bin`'s mtime
was newer than the source's — the incremental kernel build is not
trustworthy after a source edit.  The reliable path is `./run`,
which does `make clean` first.  See `gotchas.md`, "The incremental
kernel build can silently skip."

**Gotchas added:** "A syscall argument the caller did not set holds
the previous syscall's return value"; "The incremental kernel build
can silently skip."

**Scratch tags kept:** `20261001-faccessat` and
`20261001-utimensat` are local, not pushed, part of the open
`v0.6.8` milestone (dropped when the milestone is pushed).

## Session 40 — `unlinkat` (263); `unlink`/`rmdir` gain the type check

Two commits on `dev`, scratch-tagged, unpushed.  Continues the
`v0.6.8` `*at()` family.  Opened as "implement `unlinkat` so
busybox `rm -r` works" and closed with `unlinkat` implemented,
tested, and — importantly — shown to have **no consumer** in
busybox as configured.  The feature that actually made `rm -r`
work was the `unlink`/`rmdir` type check that landed in the same
commit.

| Tag | What |
|---|---|
| `20261001-atrefactor` | `unlink_body` extracted; `sys_unlink`/`sys_rmdir` become `resolve_at` wrappers |
| `20261001-unlinkat` | `unlinkat` (263); `want_dir` + `f_stat_with_retry` type check; `cli` critical section; `at_step2.c` |

**Commit 1** (`atrefactor`): pure refactor.  `sys_unlink` and
`sys_rmdir` were byte-identical except for their diagnostic
string; both now call a shared `static unlink_body(in_path, tag)`
and resolve their path with `resolve_at(AT_FDCWD_, ...)`, the
same resolver `openat`/`newfstatat` use.  No behavior change.
`unlink_body` does `path_copy` → `strip_dot_prefix` → `f_unlink`
→ `fatfs_errno`.  Canary green; neither wrapper is on a canary
path, so the refactor is invisible to it, which is the point.

**Commit 2** (`unlinkat`): `unlink_body` gains `want_dir` and a
type check via `f_stat_with_retry`:

    unlink(path) on a directory  -> -EISDIR   (was: succeeded)
    rmdir(path) on a file        -> -ENOTDIR  (was: succeeded)
    rmdir("/")                   -> -EBUSY    (root is not removable)

Both were bugs: `f_unlink` accepts files and directories alike, so
the two syscalls were silently interchangeable.  Added
`sys_unlinkat` (263), a thin wrapper over `resolve_at` +
`unlink_body` with `AT_REMOVEDIR` selecting the directory case;
unknown flag bits are `-EINVAL`.  Added `SYS_UNLINKAT 263` and a
dispatch case.

The check and the unlink are one critical section, held with
`cli` and released by restoring the caller's saved RFLAGS (not
`sti`'d — `unlink_body` can run inside `sys_execve`'s `cli`).  This
was a deliberate reversal of an initial "no race today, skip it"
call; see the session note below.

Also fixed a stale `sys_chdir` comment claiming `sys_unlink` and
`sys_mkdir` do not resolve against the cwd.  They do; since
`v0.6.4`.

**Tested.**  `at_step2.c` (new), all 8 checks pass: unlinkat file
→ 0; dir without `AT_REMOVEDIR` → `EISDIR`; dir `AT_REMOVEDIR`
→ 0; file `AT_REMOVEDIR` → `ENOTDIR`; `unlinkat(dirfd, rel)` → 0;
bad flag → `EINVAL`; `unlink` missing → `ENOENT`; `rmdir` empty dir
→ 0.  It creates and removes fixtures under `/`, so it **mutates
the disk** and is **not a canary row**.  From busybox:
`touch`/`rm`, `mkdir`/`rmdir`, and `rm -r` on a tree all succeed
silently.

**Canary:** green.  Boot spine — `ls`, `cd /bin`, `pwd`, `cd ..`,
`pwd`, `exit` — all behave.  No `Unknown syscall:` lines.
(`find` rows and the pipe/redirection suites were *not* re-run:
commit 2 touches nothing on those paths.  The `rm`/`rmdir` behavior
change was verified through busybox instead.)

**The `rm -r` finding — the reason this session is worth a log
entry.**  `unlinkat` was added on the belief that busybox `rm -r`
needs it.  It does not.  A tree-wide grep:

    grep -rn "unlinkat" third_party/busybox/ \
        --include='*.c' --include='*.h' | grep -v testsuite

returns **nothing**.  `rm -r` is `libbb/remove_file.c`: `lstat`,
then either `opendir`+`readdir`+recursive `remove_file` with a
concatenated path string and a final `rmdir`, or `unlink`.  No
dirfd anywhere.  So `unlinkat` (263) is implemented, correct, and
tested, but **has no consumer in the current applet set**.

What actually made `rm -r` work correctly was the type check in
the *same* commit: `remove_file`'s `lstat`-branching was advisory
while `f_unlink` accepted both kinds; the type check made it
load-bearing.  The commit message was corrected before the commit
was made — the false sentence never entered history.  See
`gotchas.md`, "A consumer inferred from behavior is not a
consumer."

**A process note, recorded because it cost time twice.**  The first
commit block for this work put `git commit`/`git tag` in the same
paste as the build, with the test *after* — so the commit could
land on a red test.  The second block was an `--amend` for a
commit that had never been made, because the first `git commit`
had not actually run.  Both were caught by asking for `git status`
and `git log` before the block.  The rule carried forward: **the
state check comes before the command block.**

**A design note, recorded because the first answer was wrong.**
The initial decision was to *skip* `cli`/`sti` around the type
check and `f_unlink`, on the grounds that nothing races on one
path today.  That was reversed: "nothing races today" is true of
every race before it happens, and the check is meaningless if the
object can change between the check and the action.  The mechanism
was verified against the source before adopting it — `diskio.c`'s
`disk_read`/`disk_write` and `ata.c`'s `ata_read_sectors_drive`/
`ata_write_sectors_drive` spin on the ATA status port
(`ata_poll_bsy_clear`, `ata_poll_drq`) and never `hlt` or wait on
an IRQ, so `cli` across FatFs cannot hang.

**Gotcha added:** "A consumer inferred from behavior is not a
consumer."

**Scratch tags kept:** `20261001-atrefactor` and
`20261001-unlinkat` are local, not pushed, and are part of the
open `v0.6.8` milestone (dropped when the milestone is pushed).
`20261001-docs` is likewise local.

**`20261001-docs` (between sessions 39 and 40).**  A small docs
commit: the Wayland long-horizon section in `ROADMAP.md` plus
cross-references.  No code.  It sits between `20260930-cursor` and
`20261001-atrefactor` on `dev`.

## Session 39 — `20260930-at` (opens `v0.6.8`)

| Tag | What |
|---|---|
| `20260930-at` | `resolve_at`, `file_slot_t.dir_path`, `newfstatat` (262), `openat` (257); stat family inverted; `find` enabled with `-type` |
| `20260930-cursor` | software block cursor on the framebuffer; no trail; 500 ms blink at 100 Hz; PIT-rate comment fix |

**Commit 1** (`at: resolve_at, newfstatat(262), openat(257)`):
implemented the `*at()` path-resolution rule in one place
(`resolve_at`), added `file_slot_t.dir_path` so a directory fd
remembers its absolute path, inverted the stat family so
`sys_newfstatat` is the general implementation and
`sys_stat`/`sys_lstat`/`sys_fstat` are wrappers, split `open(2)`
into `open_resolved` + `sys_open`/`sys_openat`.  Strict flag
handling in `newfstatat`: `AT_SYMLINK_NOFOLLOW` and
`AT_NO_AUTOMOUNT` are no-ops, `AT_EMPTY_PATH` is the fstat form,
unknown bits are `-EINVAL`.  Added `tests/at_step1.c`.

**Commit 2** (`config: enable busybox find with -type`): enabled
`CONFIG_FIND=y` and `CONFIG_FEATURE_FIND_TYPE=y`.  Other
`FEATURE_FIND_*` predicates deliberately off.

**Canary:** green.  `at_step1` 7/7.  `find /bin`,
`find / -type d`, `find / -type f -name busybox` all behave.  No
`Unknown syscall:` lines for 257 or 262.

**Notes for the milestone narrative:**
- musl 1.2.5 on x86_64 does **not** reach syscall 262 for the
  common stat cases — its `fstatat_kstat()` fast-paths
  `stat`/`lstat`/`fstat` to 4/6/5.  Syscall 262 is reached only
  for a real dirfd + relative path, or non-standard flags.
  `statx` (332) is unreachable: `SYS_fstatat` is defined as
  `SYS_newfstatat`, so musl compiles the `fstatat_kstat` branch,
  not the statx branch.  Confirmed by grep: no `statx` in the
  kernel, and `SYS_statx` never called.
- `sys_fstat_body` was factored out of `sys_fstat` so that
  `sys_newfstatat`'s `AT_EMPTY_PATH` case can call it without
  recursing through `newfstatat`.  `sys_fstat` is now a wrapper.
- The `sys_execve` three-attempt path block was **not** touched.
  It does a different job (bare-name search) and remains the VFS
  shim it was.
- `resolve_at` needs `get_file_slot_any`, which is defined much
  lower in the file.  A forward declaration was added just above
  `resolve_at`, not at the top, because `file_slot_t` is not yet
  in scope at the top.  See the build error it fixed: implicit
  declaration, then "static declaration follows non-static
  declaration."

**Gotchas added:** "The kernel syscall name and the libc name
differ"; "When the count disagrees with the lines, suspect the
test."

**Scratch tag kept:** `20260930-at` is local, not pushed, and is
part of the open `v0.6.8` milestone (not dropped until the
milestone is pushed).

**Cursor (`20260930-cursor`).**  The framebuffer console had lost
its cursor because the VGA text backend had a hardware cursor and
the framebuffer has none; nothing drew a software replacement.
The fix is a software block cursor: an inverse-video repaint of
the cursor cell, drawn and erased through the existing
`paint_cell` chokepoint, so both backends share one draw path and
the VGA text hardware-cursor path is untouched (`con_is_fb`
early-outs).  Erase-then-redraw lives in
`vga_update_hardware_cursor`, which every cursor move already
calls, so no other call site changed.  The bulk-repaint functions
(scroll, erase display, insert/delete chars, alt-screen) erase the
cursor at the top and the trailing update call redraws it.

The first version left an inverted trail: callers move
`cursor_row`/`cursor_col` before calling
`vga_update_hardware_cursor`, so the erase hit the new cell and the
old paint stayed.  `cursor_painted_row`/`cursor_painted_col` now
track the last painted position, which is the only position the
erase can meaningfully hit.

Blink is driven from the PIT tick via `vga_cursor_tick`, called
from `timer_preempt_handler`.  No lock is taken: the tick runs
from the ISR with interrupts off, so its one-cell repaint cannot
interleave with a `vga_putc`, and `serial_lock` there would
deadlock against a `vga_putc` the tick preempted.  Blink is 500 ms
per state (`CURSOR_BLINK_TICKS 50` at the actual 100 Hz PIT
rate).

The PIT rate itself was corrected in a separate comment-only
commit: `sys_poll`'s timeout comment claimed 500 Hz, and the same
wrong figure had propagated into three comments in `vga.c`.  All
now say 100 Hz, verified against `pit_init(100)` in `kmain.c`.
See the gotchas entry "A wrong constant propagated because it was
consistent with itself."

## Session 38 — framebuffer console, Terminus, `vi` fills the screen

Eight commits on `dev`, scratch-tagged, unpushed.  **No milestone
bump yet** — deferred by choice.  Opened with "the VGA text console
is hard to read in a half-screen QEMU window" and closed with the
console fully on a 1024×768 linear framebuffer, Terminus 10×18 text,
and `vi` filling the screen.

The work was kernel + boot chain (not userland): VBE mode setting,
framebuffer mapping, a glyph blitter, and a dual-backend console.

### Boot chain and framebuffer

| Tag | What |
|---|---|
| `20260930-vbe` | VBE mode 0x118 (1024×768×24), framebuffer descriptor captured |
| `20260930-fb` | framebuffer mapped into the kernel; `fb_putpixel`/`fb_fill`/`fb_fillrect` |

**VBE mode set.**  `stage2.asm` replaced the VGA text mode-03h set
with a VBE call for mode 0x118 (linear framebuffer bit) plus a
mode-info query.  The framebuffer physical address, pitch, width,
height, and bpp are written into the `bootinfo` block at 0x40–0x54,
which `bootinfo.h` already defined.  Falls back to text mode 03h if
VBE fails, leaving the fields zero.  `boot.asm` unchanged; no
Makefile change.

**Observed:** mode 0x118 on QEMU is **24bpp, pitch 3072**, at
physical **0xFD000000** — not 32bpp/4096 as assumed.  VBE mode
numbers do not encode bit depth.  The kernel uses the captured
values, so the assumption was harmless, but the comment was
corrected.

**Mapping.**  `fb.c` maps the framebuffer's physical range (2.25 MB)
into the kernel's higher half at `0xFFFFFFFFA0000000`, present +
writable + NX, page by page via `vmm_map_page_in_cr3`.  The
boot page tables do not cover 0xFD000000, so this mapping is
required before any pixel write.

**Byte order — BGR, not RGB.**  The first test pattern came out with
red and blue swapped.  QEMU's Bochs VBE stores 24bpp pixels as
B, G, R.  Fixed with `FB_BYTE_R/G/B` macros in `fb.c`; callers still
pass `(r, g, b)`.  Full writeup: `gotchas.md`, "Assumed byte-order
conventions."

### Font and console

| Tag | What |
|---|---|
| `20260930-font` | Terminus 10×18 glyph blitter (`fb_putchar`/`fb_puts`) |
| `20260930-fbcon` | console drawing routed through a framebuffer-aware cell primitive |
| `20260930-shadow` | shadow cell grid: scroll, insert/delete, alt-screen |

**Font.**  `fonts/ter-u18n.psf` (PSF v2, 256 glyphs, 10×18, 36
bytes/glyph, 32-byte header) is embedded via `xxd -i`, same path as
`test_program_data`.  The `.psf` was built from Terminus 4.49 source
(`make psf`).  The Unicode table is the identity mapping for this
font, so glyph index == character code; the table is not read (the
code comments this).  **The glyph rows are MSB-first**, so the
blitter reads bit `(width-1-col)`; getting this wrong mirrors every
glyph (caught by looking at the output).

**Console routing.**  `vga.c` gained a 16-color VGA→RGB palette and
a single `con_put_cell` chokepoint that draws either via `fb_putchar`
(framebuffer) or to VGA text memory.  Every draw site in `vga.c`
goes through it.  The console grid becomes `con_cols()`/`con_rows()`
— 102×42 on the framebuffer, 80×25 on VGA — so the same VT100 logic
drives either backend.  **The VT100 parser, SGR, cursor addressing,
alt-screen, and the public `vga_*` API are unchanged; callers
outside `vga.c` are untouched** (every one already went through the
public API — verified by grep before the change).

**Shadow cell grid.**  The framebuffer is write-only pixels, so
operations that read cells back (scroll, insert/delete chars,
alt-screen save/restore) could not move or restore existing
content — scrolling blanked instead of sliding, and exiting `vi`
left the screen black.  Fixed with a shadow `uint16_t` cell grid
(the single source of truth on both backends); `con_put_cell`
splits into `shadow_set` + `paint_cell`, and every read-back
operation shifts shadow cells and repaints.

### Cleanup and `vi`

| Tag | What |
|---|---|
| `20260930-fbclean` | test pattern removed; framebuffer cleared to console background |
| `20260930-winsz` | `TIOCGWINSZ` reports the real console grid |
| `20260930-viwinsize` | busybox `FEATURE_VI_WIN_RESIZE=y` — `vi` fills the screen |

**`fbclean`.**  The step-2 pixel test pattern is replaced with a
full-framebuffer fill of the console background (VGA blue,
`0x00,0x00,0xAA`), which also covers the margin strip the 102×42
grid does not reach (the leftover green-square outline at the
bottom right).

**`winsz`.**  `sys_ioctl`'s `TIOCGWINSZ` answered a hardcoded
24×80.  Added `vga_rows()`/`vga_cols()` and reported the live grid
(42×102).  This is the *kernel* half.

**`viwinsize`.**  The *busybox* half: `CONFIG_FEATURE_VI_WIN_RESIZE=y`.
`vi` was sizing to 24×80 despite `TIOCGWINSZ` correctly answering
42×102 — it ignored the result.  Enabling this option made `vi`
consult the winsize at startup, and it fills the screen.  See
`gotchas.md`, "The option name describes its most visible effect,
not its scope": this option was initially dismissed as SIGWINCH-only
(needs signals, which donix lacks), but it *also* gates the startup
winsize query.

### Verification (`donix>` / ash)

Framebuffer console:

    # boot messages render on the framebuffer, not just serial
    # the shell prompt, typed input, and command output render there
    vi test            # ~ markers rows 2..41, status line row 42
    :wq                # saves; screen returns to the shell (alt-screen restore)
    ./test             # the saved script runs

The `vi` round trip — enter alt-screen, edit, save, exit, restore the
prior screen — exercises the shadow grid's alt-screen save/restore
and the VT100 parser's cursor addressing and SGR on the framebuffer.

### Expected noise

No `Unknown syscall:` lines.  No `[fd]` lines.  The `FB: mapped N
pages ...` line is expected (informational).  The usual `EXIT:` lines
for forked children.  The `sys_open: f_open FAIL ...` from `vi` on a
new file is expected (the file does not exist yet).

## Session 37 — `musl_sh`: tokenizer, redirection, sequences, pipelines

Six commits on `dev`, scratch-tagged (one config commit untagged),
unpushed.  **No milestone bump yet** — deferred by choice; the
scratch-tag annotations carry the fuller per-commit narrative and
will seed the milestone summary when it is written.

This session closed the `musl_sh` gap that `v0.6.6`'s handoff named
as the headline `v0.6.7` item: `donix>` now strips quotes and parses
`<`, `>`, `>>`, `|`, `&&`, `;`, and pipelines.  Everything the
kernel needed was already in place from `v0.6.6` (`pipe`, `dup`,
`dup2`, `fork`, `execve`, `wait4`, redirect via `dup2`, a working
cwd); this was userland only, no kernel change.

### Tokenizer

| Tag | What |
|---|---|
| `20260930-tokenizer` | two-phase tokenizer: quote stripping + operator splitting |

Phase 1 splits on unquoted space/tab and strips `'...'` / `"..."`,
in place with a read cursor and a write cursor over the same buffer.
The in-place hazard — the token terminator landing on the blank the
read cursor is about to inspect, which silently collapsed every line
to one token — is written up in the commit and worth remembering:
the blank is consumed *before* the NUL is written.

Phase 2 splits operator characters (`>>`, `&&` first, then
`>`, `<`, `|`, `&`, `;`) out of the words phase 1 produced.  It runs
into a *separate* output buffer, so it has none of phase 1's
in-place hazard.  Together they give `echo hi>out` →
`echo hi > out` and `echo "a b" c` → `echo`, `a b`, `c`.

### Redirection, sequences, pipelines| Tag | What |
|---|---|
| `20260930-redir` | `<`, `>`, `>>` via `open` + `dup2` + `execve` |
| `20260930-seq` | `;` and `&&`; builtins report their own status |
| `20260930-pipe` | `\|`; `fork_child` refactor; N-stage pipelines |

**Redirection.**  Split argv at the first `<`/`>`/`>>`, fork, and in
the child `open` the target and `dup2` it onto fd 0 or 1 before
`execve`.  `>` truncates, `>>` appends; a missing target makes the
child print and exit *without* exec'ing.  Builtin redirection is
silently ignored (the builtin runs in the parent, which has no place
to put a redirected fd) — a known limitation.

**Sequences.**  Split each `;`/`&&` segment and run it; `&&`
short-circuits on a non-zero status, `;` does not.  `builtin_cd`
and `builtin_pwd` now return their own status, so `cd /nope && foo`
correctly skips `foo`.  `exit` is honored at the start of any
segment and terminates the shell, skipping the rest of the line.

**Pipelines.**  Split each segment on `|`; a one-command segment goes
through `run_one` unchanged (so a bare `cd` still changes the shell's
cwd); a multi-command segment with a builtin is refused
(`sh: builtin in pipeline not supported`) because a builtin cannot be
forked without changing its meaning.  The fork/redirect/exec half of
`run_one` was factored into `fork_child`, which takes the pipe fds to
`dup2` and applies file redirection on top (so `a | b > f` sends
`b`'s stdout to `f`).  `run_pipeline` forks all commands, closes
every pipe end in the parent, then waits for all — the close is what
makes EOF propagate.  The pipeline's status is the last command's.

`fork_child` also closes every inherited fd 3..63 in the child.
This is what stops a middle command in `a | b | c` from holding the
upstream write end open and deadlocking the reader.  It is correct
only because the shell holds no fd above 2 at exec time; see
`gotchas.md`, "Blunt fd close in `fork_child`…".

### Also

| Tag | What |
|---|---|
| (untagged) | `configs/busybox.config`: enable `false`, `true`, `yes`, `seq`, `clear` |
| `20260930-nodebug` | remove the tokenizer debug print (`[a\|b\|c]`) |
| `20260930-cat` | donix-native `cat`: stdin mode, multiple files, `-` |
| `20260930-docs` | the session-37 documentation pass |

The busybox config commit enabled `false`/`true` (needed to test
`&&` short-circuiting on a real child's exit code) and
`yes`/`seq`/`clear` (bundled in the same edit).

`cat` gained a stdin mode (`cat < file` works without busybox),
multiple-file concatenation, and `-` for stdin.

### `-EPIPE` observation — the docs prediction was wrong

`v0.6.6`'s `open-issues.md` and handoff predicted that
`busybox yes | busybox head -n 1` would hang, because `-EPIPE` is
delivered without `SIGPIPE` and busybox `yes` "does not handle it."

Observed this session: it does **not** hang.  `head` prints `y` and
exits; `yes` receives `-EPIPE`, **handles it**, prints
`yes: Broken pipe`, and exits; the shell reaps both and returns to
the prompt.  The prediction was too pessimistic and named the wrong
example.  `open-issues.md` corrected accordingly.

## Session 36 — `pipe(2)`, pipelines work (v0.6.6)

Nine commits on `dev`, scratch-tagged, unpushed.  Opened on a
typing bug (Shift+backslash produced nothing) and closed with
pipelines working from ash.  The milestone is `v0.6.6`.

### Keyboard

| Tag | What |
|---|---|
| `20260929-pipekey` (dropped) | assign 0x2B (`\` and `\|`) in both scancode tables |

Shift+backslash produced nothing in either shell: neither
`scancode_ascii[0x2B]` nor `scancode_shift[0x2B]` was assigned, so
`scancode_to_ascii` returned 0 and `kbd_buffer_put`'s NUL guard
dropped the byte.  Not an `interrupts.c` bug (`irq1_handler` tracks
Shift correctly) and not a `vga.c` bug (the byte never got
buffered).  An audit of the printable scancode range showed 0x2B was
the only key missing from both tables.  Prerequisite for typing `\|`
at all.

### Pipe, in five steps

| Tag | What |
|---|---|
| (dropped) | Step 1 — `sys_pipe` (22), pipe object, non-blocking read/write |
| (dropped) | Step 2 — blocking read/write + directed wake |
| (dropped) | Step 3 — EOF, `-EPIPE`, dup2-aware closed-end counts |
| (dropped) | Step 3.5 — wake the peer when an end closes via process exit |
| (dropped) | Step 4 — route pipe fds on stdio correctly; pipelines work |

**Step 1.**  `FILE_KIND_PIPE`, a shared `pipe_t` (4 KB ring,
`capacity` a field not an inlined macro so growth is additive
later), two refcounts kept distinct: slot refcount counts fd
references to one *end*, pipe refcount counts live *ends*.
`sys_pipe` allocates the object and both ends atomically and rolls
back on failure.  Test `pipe_step1.c`: 17 checks, all pass.

**Step 2.**  `BLOCK_KIND_PIPE_READ`/`WRITE` = 2/3 (new values for
the existing `block_kind`, no PCB offset movement).
`reader_waiting`/`writer_waiting` pointers on the pipe;
`pipe_wake_waiter()` checks the waiter is non-NULL, still `BLOCKED`,
and blocked on *this* kind before waking — the block_kind check is
what makes a recycled PCB slot safe.  Directed wake, not a
broadcast.  Test `pipe_step2.c`: 7 checks, pass.

**Step 3.**  `file_slot_t.end` flag (read/write);
`pipe_t.readers_open`/`writers_open` *counts*.  Counts, not flags,
because `pipe(fds); dup2(fds[1], 1)` leaves two fds holding the
write end, and closing one must not look like "the writer closed."
`read` on empty + `writers_open == 0` → 0 (EOF); `write` with
`readers_open == 0` → `-EPIPE`, checked *before* the full-buffer
check.  Test `pipe_step3.c`: 6 checks, pass.

**Step 3.5.**  `put_file_slot`'s pipe case now wakes the peer when
a count reaches zero.  Without it, a writer that `_exit`s without
closing leaves a reader stuck in `hlt` until a keystroke.  Test
`pipe_step3b.c`: STEP3B OK first run.

**Step 4.**  The stdio guard inversion.  `sys_read` fd-0 and
`sys_write` fd-1/2 guards changed from `kind != FILE_KIND_FILE` to
`kind == FILE_KIND_CONSOLE`.  Full writeup: `gotchas.md`, "Negative
fd-kind tests don't extend to new kinds."

### Also in the milestone

| Tag | What |
|---|---|
| (dropped) | `dup(2)` (syscall 32) as `fcntl(F_DUPFD, 0)` |
| (dropped) | bump version banner to `v0.6.6` |
| (dropped) | `pipe_step3` back to `dup()` now that `dup(2)` exists |

### Canary

Focused canary green.  Pipeline rows (run from ash):

    cat hello-world.txt | head -n 2
    echo hi | wc                      # 1 1 3
    echo hello | cat                  # hello

Pipe regression suite (`userland/musl/tests/`), run from ash:

    pipe_step1    # object + non-blocking I/O
    pipe_step2    # blocking + directed wake
    pipe_step3    # EOF, EPIPE, dup-aware counts
    pipe_step3b   # exit-path wake

## Session 34 — low fds first-class, `uniq` works

Twenty-one commits on `dev`, scratch-tagged, unpushed.  Opened with
"investigate `uniq`" and closed with `uniq` working and a kernel fix
that turned out to be the actual bug.  Two threads.

### Thread 1 — low fds (0/1/2) are first-class

| Tag | What |
|---|---|
| `20260930-low-fd-io` | console sentinels; lowest-free-fd `open`; `dup2`/`fcntl` accept 0/1/2 |

`uniq FILE` hangs because `alloc_file_slot` started at fd 3, so
`close(0); open(file)` landed the file on fd 3 and `read(0, ...)`
blocked on the keyboard.  Three interlocking changes: console
sentinels in fds 0/1/2, `alloc_file_slot` scanning from 0, and
`sys_dup2` / `sys_fcntl(F_DUPFD)` using `get_file_slot_any`.  Full
writeup in `gotchas.md`, "Low fds (0/1/2) are first-class."

### Thread 2 — busybox

| Tag | What |
|---|---|
| `20260930-busybox-uniq` | `CONFIG_UNIQ=y` |

### Canary

Focused canary green.  New read-only `uniq` rows:

    uniq hello-world.txt
    uniq -c < hello-world.txt

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
