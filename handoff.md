Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-28 (session 28 + exploring-copy handoff)
**Current HEAD:** tag `20260928-10`, branch `dev`
**Last milestone:** `v0.6.2` (published)
**Next milestone:** `v0.6.3` — busybox applet dispatch, built-in
`/bin` layout, and shell auto-launch, ported from the exploring copy.

Commits are named by tag only, never by SHA.  Working tags are
local and permanent -- `git show <tag>` always resolves.  The
commit record is `docs/session-log.md`; the commit message carries
the narrative.

---

## Session 29 (new, do this first)

**Goal: bring the exploring-copy work into the real tree.**  A
temporary copy of the project at `~/code/x` was used to prototype
and verify four interdependent changes.  All four are working
there and have been tested in QEMU.  This session ports them,
one commit each, in the order below.  Nothing else in this
handoff comes before this section.

The exploring copy's git status showed exactly four modified
files, all of which need to be ported:

    modified:   04_kernel_64bit/user_syscall.c
    modified:   05_boot_kernel64/Makefile
    modified:   configs/busybox.config
    modified:   userland/musl/apps/musl_sh.c

Each is described below.  Apply them one at a time, test the
canary after each, tag each.

---

### Change 1 — Kernel: absolute-path normalization in `sys_execve`

**File:** `04_kernel_64bit/user_syscall.c`

**What it does.**  `sys_execve` currently hands the path string
straight to FatFs, which only accepts drive-relative paths like
`0:/NAME`.  A Unix-style absolute path like `/bin/busybox` fails
with `EXEC-FAILED` because FatFs rejects the leading slash.  This
change adds a retry inside `sys_execve` that translates a leading
`/` into `0:` before re-trying `f_open`, preserving case and
suffix.

**Where it goes.**  Inside `sys_execve`, in the block that
currently reads:

```c
FIL file;
FRESULT fr = f_open(&file, path, FA_READ | FA_OPEN_EXISTING);
if (fr != FR_OK) {
    int has_drive = 0;
    for (const char* p = path; *p; p++) {
        if (*p == ':') { has_drive = 1; break; }
    }

    char resolved[USER_PATH_MAX];
    int can_retry = !has_drive &&
                    exec_resolve_bare_name(path, resolved,
                                           sizeof(resolved)) == 0;

    if (can_retry) {
        FRESULT fr2 = f_open(&file, resolved, FA_READ | FA_OPEN_EXISTING);
        if (fr2 == FR_OK) {
            fr = FR_OK;
        }
    }
}
```

Replace with a three-attempt version:

1. **As-given** — `f_open(path)`.  Catches already-normalized paths.
2. **Unix-absolute → FatFs-absolute** — if the path starts with
   `/` and contains no `:`, try `0:` + path verbatim.  This is
   the new behavior.  Preserves case, does not append `.ELF`.
3. **Bare name → `0:/NAME.ELF`** — the existing
   `exec_resolve_bare_name` retry, unchanged.

The exact code is in the exploring copy at `~/code/x`.  Diff it
against the real tree:

```bash
diff -u /path/to/real/04_kernel_64bit/user_syscall.c \
        ~/code/x/04_kernel_64bit/user_syscall.c
```

Only the `f_open` block inside `sys_execve` should differ.  If
the diff shows more, reconcile before committing.

**What it enables.**
- `/bin/busybox sh` works from `musl_sh` (no more `0:` needed).
- `/LS.ELF` works from inside ash.
- Any future Unix-style absolute path in userland resolves.

**Test after applying.**  Rebuild the kernel and image, boot,
and from the `donix>` prompt:

    donix> /LS.ELF
    donix> /bin/busybox sh
    $ /LS.ELF
    $ exit

All four should work.  The kernel log should show
`sys_execve: ... (busybox)` and `sys_execve: ... (LS.ELF)` with
no `f_open` failure line.

**Tag:** e.g. `20260928-10-kernel-abs-path`.

---

### Change 2 — BusyBox config: standalone shell and 64-bit install path

**File:** `configs/busybox.config`

**What it does.**  Enables BusyBox's standalone shell so that
`ash` runs applets in-process without needing `/bin/NAME`
symlinks (which FAT16 cannot provide).  Also sets the
`BUSYBOX_EXEC_PATH` to the location the binary now lives at on
the image.

**The three lines to change.**  In the exploring copy, the
relevant config sections read:

```
CONFIG_FEATURE_PREFER_APPLETS=y
CONFIG_FEATURE_SH_STANDALONE=y
CONFIG_BUSYBOX_EXEC_PATH="/bin/busybox"
```

The real tree's `configs/busybox.config` will have these as
`# ... is not set` or a different exec path.  Diff to confirm:

```bash
diff -u /path/to/real/configs/busybox.config \
        ~/code/x/configs/busybox.config
```

Only those three lines (plus any lines that moved because of
`menuconfig` reordering) should differ.  If the diff shows
unrelated option churn, copy just the three settings by hand
rather than the whole file.

**What it enables.**
- `mkdir`, `ls`, `cat`, `cp`, `echo`, `pwd`, `wc`, etc. all work
  by bare name inside ash, with no symlinks.
- The shell knows where its own binary is.

**Test after applying.**  Force a busybox rebuild (the stamp
caches the old binary):

```bash
rm -f userland/musl/build/busybox.elf
rm -f userland/musl/build/.busybox.stamp
rm -f third_party/busybox/.config
make -C 05_boot_kernel64 hdd-single.img
```

Then boot, enter ash, and:

    $ mkdir testdir
    $ ls
    $ exit

`mkdir` should succeed.  (Remember the canary note: delete the
directory before the next image build, or don't leave it in.)

**Tag:** e.g. `20260928-11-busybox-standalone`.

---

### Change 3 — Image layout: `/bin/busybox` instead of `BUSYBOX.ELF`

**File:** `05_boot_kernel64/Makefile`

**What it does.**  Two changes to the `hdd-single.img` rule:

1. Removes `mcopy_one "$(USERLAND_BIN)/busybox.elf" BUSYBOX.ELF`
   from the applet list — `busybox` no longer lands at the root.
2. Adds a block that creates `::/bin` in the FAT image and copies
   the busybox binary to `::/bin/busybox`.

The added block looks like:

```make
	@echo "--- staging busybox at /bin/busybox ---"
	@mmd -i hdd-single.img@@$$(( $(SINGLE_DRIVE_LBA) * 512 )) ::/bin 2>/dev/null || true
	@if [ -f "$(USERLAND_BIN)/busybox.elf" ]; then \
	    echo "mcopy $(USERLAND_BIN)/busybox.elf -> ::/bin/busybox"; \
	    mcopy -i hdd-single.img@@$$(( $(SINGLE_DRIVE_LBA) * 512 )) \
	        "$(USERLAND_BIN)/busybox.elf" "::/bin/busybox" ; \
	else \
	    echo "ERROR: $(USERLAND_BIN)/busybox.elf missing — did the busybox build fail?"; \
	    exit 1; \
	fi
```

Plus an added `mdir ::/bin` line in the verification section so
the build log confirms the binary landed.

**Why `/bin/busybox` and not `BUSYBOX.ELF` at the root.**  The
`CONFIG_BUSYBOX_EXEC_PATH` from Change 2 points at
`/bin/busybox`; the binary must actually be there for standalone
mode's re-exec to work.  If the two disagree, ash will fail
mysteriously when it tries to fork for an applet.

**Diff:**

```bash
diff -u /path/to/real/05_boot_kernel64/Makefile \
        ~/code/x/05_boot_kernel64/Makefile
```

Only the busybox staging lines and the added `mdir ::/bin`
verification line should differ.

**Test after applying.**  Rebuild the image and check:

```bash
mdir -i 05_boot_kernel64/hdd-single.img@@1048576 ::/bin
```

Should show `busybox` and only `busybox` (plus `.` and `..`).
Then boot and run:

    donix> /bin/busybox sh
    $ ls /bin
    busybox
    $ exit

**Tag:** e.g. `20260928-12-image-bin-busybox`.

---

### Change 4 — Shell: pass paths through unchanged, auto-launch ash

**File:** `userland/musl/apps/musl_sh.c`

**What it does.**  Two changes:

1. **Delete the path-rewriting block.**  The current code in the
   child builds a `char path[128]` by prepending `0:/` and
   appending `.ELF` to `argv[0]`.  This produces
   `0://bin/busybox.ELF` for a leading-slash input, which FatFs
   rejects before the kernel's new retry (Change 1) can see it.
   Replace the whole block with a direct
   `execve(argv[0], argv, (char**)0);`.

2. **Add auto-launch of ash at startup.**  A new helper
   `run_busybox_sh()` forks, execs `/bin/busybox sh` in the
   child, and waits in the parent.  `main()` calls it once
   before its own prompt loop, so boot drops straight into ash.
   Typing `exit` at ash returns to the `donix>` prompt.  Typing
   `sh` at the `donix>` prompt re-enters ash.

The full replacement `musl_sh.c` is in the exploring copy.
Diff to see exactly what changed:

```bash
diff -u /path/to/real/userland/musl/apps/musl_sh.c \
        ~/code/x/userland/musl/apps/musl_sh.c
```

Only the child's exec block and the new `run_busybox_sh` helper
plus its call site should differ.

**What it enables.**
- Boot → ash directly.
- `exit` from ash → `donix>` prompt.
- `sh` at `donix>` → back into ash.
- `/bin/busybox sh` at `donix>` → same, via the kernel's path
  normalization.
- No more `0://bin/busybox.ELF` failures.

**Test after applying.**  Rebuild the userland and the image,
boot, and confirm:

    [boot drops into ash]
    $ ls
    $ exit
    donix> /LS.ELF
    donix> sh
    $ exit
    donix> exit

**Tag:** e.g. `20260928-13-shell-autolaunch`.

---

### Order of application

Apply and tag **one at a time**, in this order:

1. Change 1 (kernel) — independent, no busybox needed.
2. Change 3 (Makefile) — image layout for the new binary path.
3. Change 2 (busybox config) — rebuilds busybox against the new
   config; needs Change 3's image layout to test end-to-end.
4. Change 4 (shell) — depends on Changes 1, 2, and 3 being in
   place to work end to end.

Actually, re-reading: Change 2 (busybox config) and Change 3
(image layout) are independent of each other in terms of *when*
you apply them, but the *test* for Change 2 needs Change 3
applied first.  Suggested order:

1. Change 1 (kernel) — test with `0:/bin/busybox sh` still, or
   the old `BUSYBOX.ELF` at root.
2. Change 2 (busybox config) — rebuild busybox.
3. Change 3 (Makefile) — image now has `/bin/busybox`, tests
   Change 2's standalone mode.
4. Change 4 (shell) — the last piece, ties it together.

The exploring copy has all four applied and working.  Port
them in that order and test the focused canary after each.

---

### Canary additions for the new work

The existing focused canary (below) stays.  Add these rows for
the port, once Change 4 is in:

    # after boot, ash should already be running
    ls                          # standalone applet
    echo hi                     # standalone applet
    exit                        # back to donix>
    /LS.ELF                     # custom ls via kernel abs-path retry
    /bin/busybox sh             # explicit re-entry
    exit                        # back to donix>
    sh                          # shortcut re-entry
    exit                        # back to donix>

**Do not add a bare `mkdir` row.**  Session 28's note still
applies: `mkdir` mutates the disk, and `rmdir`/`unlink` do not
exist yet to clean up.  Test `mkdir` manually once, then rebuild
the image.

---

### Known state after the port

- `Unknown syscall: 157` still appears at ash startup.
  `prctl(PR_SET_NAME, ...)` — harmless, not addressed by this
  port, tracked in `docs/open-issues.md`.
- `Unknown syscall: 107` still appears once at ash startup.
  `geteuid` — the pre-existing next-step item, unchanged.
- `cd` still fails with `Function not implemented`.
  `chdir(2)` (syscall 80) not implemented.  This is now the
  *next* next-step, because the port makes the shell usable
  enough that `cd` is the obvious missing piece.
- `busybox cd` from the `donix>` prompt fails with
  `applet not found`.  Expected: `cd` is a shell builtin, not an
  applet.  It only exists inside `ash`.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 syscall ABI, natively. Newlib is gone.
Userland is a tracked source tree at `userland/musl/`. Phase B
(busybox) is underway.

Full strategy, rules, and the one-change-at-a-time discipline:
`docs/strategy.md`.

---

## Current milestone

**`v0.6.2` -- Phase B: `busybox ash` is interactive, and `ls <file>`
works in both shells.**  `sys_ioctl` answers `TIOCGWINSZ` (what ash
actually probes), busybox is built with `FEATURE_EDITING=y`, and the
musl `ls` port stats its argument before `opendir`.

Session 25 added `sys_mkdir` and enabled
`FEATURE_EDITING_HISTORY=256`.

Session 27 found that the kernel's syscall table had diverged from
the Linux x86_64 ABI in two places -- `mkdir` was at 7 (Linux:
`poll`) and `setsid` was at 107 (Linux: `geteuid`).  Both had been
guessed from the "Unknown syscall: N" diagnostic rather than read
from the canonical table, so each handler sat at a number no
correct caller uses and shadowed a different syscall.  Corrected
`mkdir` to 83 and `setsid` to 112 (tag `20260928-05`); busybox
`mkdir` now works end to end.  The previously masked gaps --
`poll(2)` at 7 and `geteuid(2)` at 107 -- were exposed.

**Session 28: `poll(2)` implemented (syscall 7).**  `busybox ash`
stays interactive across many commands with no `Unknown syscall: 7`
noise.  The handler blocks on fd 0 when `timeout < 0`, mirroring
`sys_read`'s fd-0 path (`cli` / `state = BLOCKED` with
`BLOCK_KIND_NONE` / `sti; hlt`), and `irq1_handler`'s
`process_wake_all_blocked` wakes it on the next keystroke.  A
non-blocking first version was rejected: ash interprets
`poll(fds, 1, -1)` returning 0 as end-of-input and exits, and on
real Linux that return combination is unreachable.  Also added
`kbd_buffer_has_data()` to `keyboard.c` -- a non-destructive
readability check that must not consume a byte, because the caller
is about to `read(0)` itself.  Tag `20260928-08` (kernel), then
`20260928-09` (docs).

The **exploring copy** (`~/code/x`) went further: it ported a
kernel path-normalization change, switched busybox to standalone
shell mode, moved the busybox binary to `/bin/busybox` on the
image, and made `musl_sh` auto-launch ash at startup.  This
session's first task is bringing those four changes into the
real tree.  See the top section.

---

## Canary state (focused canary green as of `20260928-09`)

The **focused canary** is the default. Run it on every change:

    hello
    ls
    ls hello-world.txt
    memtest
    musl_fork
    musl_exec2
    musl_wait
    busybox ls
    busybox ash
    # at the ash prompt: ls, echo hi, exit
    # back at donix>: hello

The **full canary** (milestone-only) adds: `echo`, `cat`,
`musl_stat`, `musl_min`, `musl_malloc`, `musl_printf`, `musl_exec`,
`musl_readdir`, `musl_r10probe`, `brk_verify`, `brkraw`, `brkgrow`,
`musl_dup2`, `musl_dupfd`, `musl_ids`, `musl_getcwd`,
`busybox echo`, `busybox pwd`, `busybox wc hello-world.txt`.

All rows green as of `20260928-09`.  Full per-test notes:
`docs/session-log.md`.

**Canary rows must not mutate the disk.**  `busybox mkdir` was
verified once, end to end, at tag `20260928-05` (directory created,
visible in `ls` as `<DIR>`, then manually removed before the next
image build).  It is deliberately NOT a standing canary row:
every run would leave a directory behind, and the image would fill
or a name would collide with `FR_EXIST` over time.  A write-path
canary row should be added only once `rmdir`/`unlink` exists to
clean up after it -- pair them (mkdir + rmdir) rather than adding
mkdir alone.

**Known expected noise (not canary failures):** running
`busybox ash` prints `Unknown syscall: 107` once at startup
(`geteuid`).  That is the last remaining gap from session 27's
audit, tracked in `docs/open-issues.md`, and is the next work
item.  `Unknown syscall: 7` no longer appears -- poll is
implemented and blocks correctly.

---

## Next step after the port (exactly this, then stop)

Once the four changes from the top section are ported and
tagged, and the focused canary is green with the new rows, do:

1. **Implement `geteuid(2)` -- syscall 107.**  Trivial: return a
   fixed uid.  1000 is fine (matches the typical Fedora user, and
   nothing on donix checks it).  This stops the one-time
   `Unknown syscall: 107` at ash startup.  Add `#define SYS_GETEUID
   107` to `include/syscall.h`, a `long sys_geteuid(void)` in
   `user_syscall.c` next to `sys_setsid`, and one `case SYS_GETEUID:`
   in `syscall_dispatch`.  One change, one commit, one tag.

2. **Implement `chdir(2)` -- syscall 80.**  This is the new
   obvious gap after the port.  Busybox `ash`'s `cd` calls it
   and currently gets `ENOSYS`.  The minimal fix: add a `cwd`
   field to `pcb_t`, implement `sys_chdir` that validates the
   path with `f_stat_with_retry`, checks `AM_DIR`, and stores the
   path.  Thread `self->cwd` through the path-resolution
   functions (`sys_open`, `sys_stat`, `sys_access`,
   `sys_execve`) so relative paths resolve against it, and make
   `sys_getcwd` return the stored path.  For a first cut, just
   making `cd` return 0 and `getcwd` return the stored path is
   enough to stop the error; full path threading can follow if
   a test needs it.  One commit, one tag.

3. **If time permits, write the syscall-table audit script.**
   `docs/open-issues.md` item 4.  The two divergences session 27
   found were found by accident.  A script that parses
   `syscall.h`'s `#define SYS_* N` lines and diffs the numbers
   against the canonical Linux x86_64 table
   (`arch/x86/entry/syscalls/syscall_64.tbl`) would catch the next
   one before it burns a session.  Separate commit.

4. Stop.  The user-mode `#PF` test binary, the busybox applet
   symlinks, and `FEATURE_TAB_COMPLETION` are next-next.

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. **Port the four exploring-copy changes** — the top section of
   this file.  Highest priority.
2. `geteuid(2)` (syscall 107) not implemented -- ash calls it once
   at startup.  See the "Next step after the port" section.
3. `chdir(2)` (syscall 80) not implemented -- `cd` in ash fails.
   Newly promoted, because the port makes the shell usable enough
   that `cd` is the visible missing piece.
4. The user-mode `#PF` kill path (`fault_kill_current(0x0E)` in
   `isr14_handler`, tag `20260928-04`) is in but unverified
   end-to-end -- needs a test binary that dereferences a bad
   pointer without setting `g_expect_fault`.
5. Busybox applet symlinks not installed on the FAT volume.  The
   standalone-shell approach from Change 2 side-steps this for
   applets; symlinks are still relevant if a future test wants
   `/bin/NAME` to exist as a real file for an external program.

Also open: syscall-table audit script; fork is O(~6 MB) per
call; `sys_newfstatat` (262) not implemented; `sys_munmap` is a
stub; `sys_brk`'s fixed `heap_base` and the 4 MB mmap window are
latent collisions; `musl_wait` emits hundreds of progress dots
before its children exit.

---

## State on disk

- `configs/busybox.config` -- tracked canonical busybox config.
  Edit only this copy; `userland/musl/Makefile` installs it to
  `third_party/busybox/.config`.
- `userland/musl/` -- tracked musl userland (`apps/` 6, `tests/`
  19).  `build/` gitignored.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  -- gitignored; rebuild with `./toolchain/install_musl.sh`.
- `toolchain/{install_musl.sh,musl-gcc.sh}` -- tracked.
- `~/code/x/` -- the **exploring copy**.  Contains the four
  changes described above, all applied and working.  Use it as
  the reference for the port; do not edit it further, and do not
  pull from it.  Delete it once the port is complete.
- `docs/` -- reference material, see below.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.

- `docs/strategy.md` -- Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.
- `docs/gotchas.md` -- every bug writeup, by subsystem.  Lookup
  material; grep when you hit a specific problem.  **Session 28
  added: "poll(fds, 1, -1) never returns 0 on Linux; busybox ash
  treats 0 as end-of-input."**
- `docs/session-log.md` -- commit tables (tag-only) and per-test
  canary notes.  Append a row per commit.
- `docs/open-issues.md` -- full open-issues list and deferred
  cleanups.
- `docs/migration-history.md`, `docs/dons-os-history.md` --
  historical narrative (A1-A6, pre-fork).
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` -- frozen at
  `v0.6.0`.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  Phase B: `busybox ash` is interactive across
many commands with no per-keystroke syscall noise.  Session 28
implemented `poll(2)` (syscall 7).  Session 29 ports four
exploring-copy changes -- kernel abs-path normalization, busybox
standalone mode, `/bin/busybox` image layout, and `musl_sh`
auto-launching ash -- and then implements `geteuid(2)` (107) and
`chdir(2)` (80) to close the two remaining shell gaps.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
