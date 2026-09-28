Live state only. Read top to bottom when starting a session.
This file is rewritten fresh each session; it does not accumulate.
Reference material lives in `docs/` and is **not needed to start a
session** -- ask for it when the current task needs it.

**Last updated:** 2026-09-28 (session 29)
**Current HEAD:** tag `20260928-17-shell-autolaunch`, branch `dev`
**Last milestone:** `v0.6.2` (published)
**Next milestone:** `v0.6.3` — "the shell is fully usable":
`geteuid(2)` (107) and `chdir(2)` (80) are the last two gaps.
The exploring-copy port is complete.

Commits are named by tag only, never by SHA.  Working tags are
local and permanent -- `git show <tag>` always resolves.  The
commit record is `docs/session-log.md`; the commit message carries
the narrative.

---

## Session 30 (new, do this first)

**Goal: close `v0.6.3` — make the shell fully usable.**  The
session-29 port is done and verified.  Two syscall gaps remain,
and each is one commit.

**Nothing else in this handoff comes before this section.**

### Item 1 — `geteuid(2)`, syscall 107

Return a fixed uid.  1000 is fine; nothing on donix checks it.
This stops the one-time `Unknown syscall: 107` that prints at
every ash startup.

Three edits:

1. `include/syscall.h`: add `#define SYS_GETEUID 107`.
2. `04_kernel_64bit/user_syscall.c`: add, next to `sys_setsid`:

   ```c
   /*
    * Linux x86_64 geteuid(2) — syscall 107.
    *
    * donix has no users; return a fixed uid.  1000 matches the
    * typical Fedora user and is what musl/busybox expect to see
    * as a plausible non-root uid.  Nothing on donix checks the
    * value.
    *
    * HISTORY: session 24 mistakenly implemented setsid at 107,
    * which meant ash's geteuid() call received the caller's pid
    * where it expected a uid.  Session 27 (tag 20260928-05)
    * moved setsid to 112 and exposed the real 107 gap.  This is
    * the closure of that gap.
    */
   long sys_geteuid(void) {
       return 1000;
   }
   ```

3. `syscall_dispatch`: add `case SYS_GETEUID: return (uint64_t)sys_geteuid();`

**Test:** boot, and confirm the serial log shows **no**
`Unknown syscall: 107` at ash startup.  `Unknown syscall: 157`
(`prctl`) will still appear once per busybox invocation — that
one is separately tracked and not in scope.

**Tag:** `20260928-18-geteuid`.

---

### Item 2 — `chdir(2)`, syscall 80

Busybox `ash`'s `cd` calls it and currently gets `ENOSYS`.
Minimal first cut, per the session-29 plan:

- Add a `cwd` field to `pcb_t` (`char cwd[USER_PATH_MAX]` or a
  fixed size; default `"/"`).
- Implement `sys_chdir`: copy the path, resolve it with
  `f_stat_with_retry`, check the result is a directory
  (`AM_DIR`), and store the path in `self->cwd`.  Return 0 or
  `-errno`.
- Make `sys_getcwd` return `self->cwd` instead of the hardcoded
  `"/"`.

**Full relative-path threading** (making `sys_open`, `sys_stat`,
`sys_access`, `sys_execve` resolve relative paths against
`self->cwd`) is a **follow-up**, not part of this first cut.  If
a test needs it, do it as its own commit.  For now, `cd` succeeds
and `pwd` returns the stored path, which is enough to stop the
error and let the shell be considered usable.

**Test:** boot → ash → `cd /bin` → `pwd` shows `/bin`, no error.
Then `cd /` → `pwd` shows `/`.  Then `ls` still works (from ash,
`ls` is an applet; the kernel isn't consulted).

**Note:** `cd` is a busybox *builtin*, so this only exercises
`sys_chdir` from inside ash.  `busybox cd` from `donix>` will
still fail with `applet not found` — expected, `cd` is not an
applet.

**Tag:** `20260928-19-chdir`.

---

### Item 3 — cut `v0.6.3`, then stop

Once both items are in and the focused canary is green, tag
`v0.6.3`.  Update this file's "Last milestone" line and rewrite
the "Next milestone" line.

Then stop.  Next-next, in rough priority order:

- The user-mode `#PF` test binary (open-issues item 4).
- Busybox applet symlinks on the FAT volume (open-issues item 5).
- `FEATURE_TAB_COMPLETION`.
- The **VFS layer** — see `docs/open-issues.md`.  The
  `sys_execve` path resolution is a shim; the VFS is the real
  fix, and it should not be preceded by a fourth hardcoded path.

---

## What donix is

A fork of dons-os. Goal: a Unix-like OS that runs static musl-linked
binaries on the Linux x86_64 syscall ABI, natively. Newlib is gone.
Userland is a tracked source tree at `userland/musl/`. Phase B
(busybox) is well underway.

Full strategy, rules, and the one-change-at-a-time discipline:
`docs/strategy.md`.

---

## Where we are

Session 29 completed the exploring-copy port.  Six commits:

| Tag | What |
|---|---|
| `20260928-12-kernel-abs-path` | abs-path attempt (b) in sys_execve |
| `20260928-13-musl_sh-passthrough` | shell stops rewriting argv[0] |
| `20260928-14-kernel-bin-fallback` | `/bin` fallback + VFS SHIM note |
| `20260928-15-busybox-standalone` | config: in-process applets |
| `20260928-16-image-bin-busybox` | image: `/bin/busybox` layout |
| `20260928-17-shell-autolaunch` | boot drops into ash |

All six verified end-to-end in QEMU.  What the shell can now do:

- Boot → busybox ash directly (no `donix>` first).
- `exit` at ash → `donix>` prompt.
- `busybox sh` (or `/bin/busybox sh`) at `donix>` → back into ash.
- Inside ash, `ls`, `cat`, `echo`, `ls /bin`, `ls /` run
  **in-process**, no fork (standalone mode).
- Path forms that resolve: bare name (`ls`, `hello`), leading
  slash (`/ls`, `/bin/busybox`), full FatFs (`0:/LS.ELF`).
- Root shadows `/bin`: `ls` at `donix>` is the donix-native
  `LS.ELF`, not a busybox applet.

The two known gaps, both to be closed for `v0.6.3`:

- **`geteuid(2)` (107) not implemented.**  Prints `Unknown
  syscall: 107` once at every ash startup.
- **`chdir(2)` (80) not implemented.**  `cd` in ash fails with
  `Function not implemented`.

---

## Canary state (focused canary green as of `20260928-17`)

**The focused canary changed in session 29** because the shell
auto-launches now: on boot you land at the ash `$` prompt, not at
`donix>`.

    # on boot, ash is already running
    ls                          # standalone applet, in-process
    echo hi                     # standalone applet
    exit                        # back to donix>
    ls                          # donix-native LS.ELF
    ls hello-world.txt
    memtest
    musl_fork
    musl_exec2
    musl_wait
    busybox ls                  # /bin fallback (c2)
    busybox ash                 # re-enter ash
    # at the ash prompt: ls, echo hi, exit
    # back at donix>: hello

**Do NOT add a bare `sh` row.**  There is no `/bin/sh`; `sh` at
`donix>` resolves to `0:/SH.ELF` (missing) then `0:/BIN/SH`
(missing).  Use `busybox sh` or `/bin/busybox sh`.  This is
expected until applet symlinks are installed.

**Note:** inside ash, `cat hello-world` (no extension) fails with
`No such file or directory`.  That is busybox's own `cat` doing
relative-path resolution against `/`, where the file is
`HELLO-WORLD.TXT`.  Not a bug.  Use `cat hello-world.txt`.

The **full canary** (milestone-only) adds: `echo`, `cat`,
`musl_stat`, `musl_min`, `musl_malloc`, `musl_printf`,
`musl_exec`, `musl_readdir`, `musl_r10probe`, `brk_verify`,
`brkraw`, `brkgrow`, `musl_dup2`, `musl_dupfd`, `musl_ids`,
`musl_getcwd`, `busybox echo`, `busybox pwd`,
`busybox wc hello-world.txt`.

**Canary rows must not mutate the disk.**  No standing `mkdir`
row; `rmdir`/`unlink` don't exist yet to clean up.  Test `mkdir`
manually once, then rebuild the image.

**Known expected noise (not canary failures):** running ash
prints `Unknown syscall: 157` once per invocation
(`prctl(PR_SET_NAME, ...)`) — harmless, tracked.  `Unknown
syscall: 107` is being closed in session 30.  `Unknown syscall:
7` no longer appears (poll is implemented).

---

## Open issues (top 5; full list in `docs/open-issues.md`)

1. `geteuid(2)` (107) not implemented — session 30 item 1.
2. `chdir(2)` (80) not implemented — `cd` in ash fails.
   Session 30 item 2.
3. **VFS layer (eventual).**  `sys_execve`'s three-attempt path
   resolution is a stand-in for a virtual filesystem.  When a VFS
   lands, delete the block; do not add a fourth attempt.  See
   `docs/open-issues.md`.
4. The user-mode `#PF` kill path (`fault_kill_current(0x0E)` in
   `isr14_handler`, tag `20260928-04`) is in but unverified
   end-to-end — needs a test binary that dereferences a bad
   pointer without setting `g_expect_fault`.
5. Busybox applet symlinks not installed on the FAT volume.
   Standalone mode side-steps this for applets; symlinks are
   still relevant for `/bin/NAME` as a real file.

Also open: syscall-table audit script; fork is O(~6 MB) per call;
`sys_newfstatat` (262) not implemented; `sys_munmap` is a stub;
`sys_brk`'s fixed `heap_base` and the 4 MB mmap window are latent
collisions; `musl_wait` emits hundreds of progress dots before
its children exit.

---

## State on disk

- `configs/busybox.config` — tracked canonical busybox config.
  Edit only this copy; `userland/musl/Makefile` installs it to
  `third_party/busybox/.config`.  Now has
  `FEATURE_PREFER_APPLETS=y`, `FEATURE_SH_STANDALONE=y`,
  `BUSYBOX_EXEC_PATH="/bin/busybox"`.
- `userland/musl/` — tracked musl userland (`apps/` 6, `tests/`
  19).  `build/` gitignored.
- `third_party/{busybox,busybox-install,musl-src,musl-install}/`
  — gitignored; rebuild with `./toolchain/install_musl.sh`.
- `toolchain/{install_musl.sh,musl-gcc.sh}` — tracked.
- `~/code/x/` — the exploring copy.  **Fully ported as of
  session 29**; the four changes it held are now in the real
  tree.  Safe to delete whenever; kept for now as a reference.
  Do not edit it; do not pull from it.
- `docs/` — reference material, see below.

---

## Where things live

Not needed to start a session; ask for a file when the current task
needs it.

- `docs/strategy.md` — Phase A/B plan, rules, files-not-to-touch,
  tagging convention, git hygiene, recovery.
- `docs/gotchas.md` — every bug writeup, by subsystem.  Lookup
  material; grep when you hit a specific problem.  **Session 29
  added: "Bare-name resolution has two layers, and the shell is
  the wrong place for it."**
- `docs/session-log.md` — commit tables (tag-only) and per-test
  canary notes.  Append a row per commit.
- `docs/open-issues.md` — full open-issues list and deferred
  cleanups.  **Session 29 added the VFS-layer item.**
- `docs/migration-history.md`, `docs/dons-os-history.md` —
  historical narrative (A1-A6, pre-fork).
- `docs/{CHECKLIST,MAINTENANCE,LLD_BUG_REPORT}.md` — frozen at
  `v0.6.0`.

---

## The one-line summary

**donix runs static musl-linked binaries on Linux x86_64 syscalls.
Newlib is gone.  The userland is a tracked source tree at
`userland/musl/`.  Phase B: busybox ash is interactive, runs
applets in-process, and is the boot shell; `exit` returns to the
donix prompt.  Session 29 completed the exploring-copy port and
added a `/bin` fallback so bare `busybox` resolves.  Session 30
implements `geteuid(2)` (107) and `chdir(2)` (80), the last two
gaps, and cuts `v0.6.3`.**

---

## How to use this file

At session start, paste this file and say "Continue from here."

At session end, **rewrite this file fresh** with the new HEAD tag,
canary state, and next step.  Do not append.  New gotchas go to
`docs/gotchas.md`; new commit rows go to `docs/session-log.md`;
new open issues go to `docs/open-issues.md`.  This file never
grows.  Name commits by tag only, never by SHA.
