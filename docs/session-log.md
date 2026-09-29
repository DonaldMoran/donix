Append-only.  One row per commit, named by tag only -- never by
SHA.  Git resolves tags; the handoff never duplicates what git
already records.  Working tags are local and permanent.

## Session 33 (2026-09-29)
| Tag | What |
|-----|------|
| `20260929-max-process-files-64` | process: raise MAX_PROCESS_FILES 8 -> 64; `sh script.sh` works |
| `20260929-execve-script-fallback` | execve: strip `./` before open; return ENOEXEC for short non-ELF; `./script.sh` works |
| `20260929-docs-session-33` | docs: session 33 -- script execution, MAX_PROCESS_FILES, execve fixes |
| `20260929-uname` | uname: implement uname(2) -- syscall 63; enable busybox applet |
| `20260929-lseek` | kernel: implement lseek(2) -- syscall 8 |
| `20260929-busybox-head` | busybox: enable head applet |
| `20260929-busybox-tail` | busybox: enable tail applet |
| `20260929-busybox-cp` | busybox: enable cp applet |
| `20260929-busybox-grep` | busybox: enable grep applet |
| `20260929-busybox-sed` | busybox: enable sed applet |
| `20260929-docs-session-33-complete` | docs: session 33 complete -- busybox applets, uname, lseek |
| `20260930-redirect` | kernel: inherit fds across fork; honor dup2'd fds 0/1/2; close them on exit |
| `20260930-busybox-text-utils` | busybox: enable text utilities (cut, sort, stat, tee, test, tr, cmp) |

**Shell scripts run; busybox file utilities enabled; shell
redirection works.**  Thirteen commits on `dev`, all scratch-tagged
(local, dropped before the next `v*` push).

### Thread 1 -- script execution

Three kernel fixes made script execution work:

- `MAX_PROCESS_FILES` 8 -> 64.  At 8, busybox ash's script
  fork+exec path fails with `sh: 3: Invalid argument`; at 64 the
  same script prints its output.  Controlled: same build, same
  script, only the constant varied.  Cost +448 B/pcb;
  assembly-safe (offset past `block_kind`).
- `sys_execve`: normalize the path with `strip_dot_prefix` before
  the first `f_open`, so `./test.sh` no longer fails with
  `FR_INVALID_NAME` at the open.
- `sys_execve`: return `ENOEXEC`, not `EIO`, for a short non-ELF
  read, so ash falls back to running the script through `sh`.

Result: `./test.sh`, `sh test.sh`, and `busybox sh test.sh` from
`donix>` all print the script output.

### Thread 2 -- two new syscalls

- `uname(2)` -- 63.  Not needed for scripting; the experimental
  tree's comment attributing the script failure to a missing
  uname was wrong.  Added for correctness; `busybox uname` /
  `uname -a` work.
- `lseek(2)` -- 8.  Surfaced by `head -n N`, which seeks to
  `SEEK_END` to size the file.  Without it: `Unknown syscall: 8`
  after correct output.  Backed by `f_lseek` / `f_tell`;
  `SEEK_SET`/`CUR`/`END` mapped to absolute offsets; result
  clamped to `[0, file_size]`.

### Thread 3 -- shell redirection (the big one)

`cmd < file` and `cmd > file` did not work.  Diagnosis with
temporary serial traces found **three interlocking bugs**, all
fixed in one commit (`20260930-redirect`):

1. **`sys_read` / `sys_write` hard-coded fds 0/1/2.**  `sys_read`
   sent `fd == 0` to the keyboard; `sys_write` sent `fd == 1 || 2`
   to the screen -- both *before* consulting `file_table[]`.  After
   ash's `dup2(file_fd, 0)` for `<`, `read(0, ...)` still read the
   keyboard (`cat < file` hung forever).  After `dup2(file_fd, 1)`
   for `>`, `write(1, ...)` still wrote the screen (the file was
   never written).
2. **`sys_fork` never copied `file_table[]`.**  `process_create`
   zeroes the child's PCB, so a forked child started with no fds.
   ash opens the redirect file, `dup2`s it onto fd 0, then forks
   and execs -- the child saw fd 0 as NULL.  Fixed by copying every
   open slot and bumping its refcount, exactly as `dup2` shares a
   slot.
3. **`close_all_files` started at `i = 3`.**  fds 0/1/2 were never
   closed on exit, so a redirect-created file's refcount never
   reached 0, so `f_close` never ran, so FatFs never committed the
   directory entry -- `cat out.txt` after `echo hi > out.txt` read
   an empty file.

Plus a new `get_file_slot_any()` helper that accepts fds 0-2; the
general `get_file_slot()` keeps its `fd >= 3` guard.

Verified: `cat < hello-world.txt` prints the file and returns;
`echo hi > out.txt` is silent; `cat out.txt` prints `hi`.

### Thread 4 -- busybox applets

Twelve applets enabled across two commits.  Each was **probed
first** for `Unknown syscall:` noise, per the session-33 pattern.

| Tag | Applet(s) | Kernel work needed |
|---|---|---|
| `20260929-busybox-head` | `head` | `lseek` (added above) |
| `20260929-busybox-tail` | `tail` | `lseek(SEEK_END)` |
| `20260929-busybox-cp` | `cp` | none |
| `20260929-busybox-grep` | `grep` | none |
| `20260929-busybox-sed` | `sed` | none |
| `20260930-busybox-text-utils` | `cut`, `sort`, `stat`, `tee`, `test`, `tr`, `cmp` | none |

All tested and confirmed working.  `wc`, `echo`, `cat`, `ls`,
`pwd`, `mkdir`, `rm`, `rmdir`, `touch`, `vi` were already enabled
from earlier sessions.

Two applets are **deliberately off**:

- **`uniq`** -- hangs on any invocation, with or without `-c`.
  Cause unknown; needs a trace-first investigation.  Its own
  session.
- **`od`** -- needs `readv(2)` (syscall 19), which donix lacks.
  `readv` is the mirror of `writev` (20), which exists.  Its own
  commit, then re-enable `od`.

`diff` is also off -- deliberately, larger surface area.

### Known limitation surfaced this session

**`musl_sh` (`donix>`) does not strip shell quotes and does not
parse redirection.**  `busybox sed -n '1p' file` fails from there
with `sed: unsupported command '`.  `cat < hello-world.txt` execs
`cat` with `argc=3` (`cat`, `<`, `hello-world.txt`) and `cat` tries
to open a literal `<`.  Both work correctly from ash.  See
`docs/gotchas.md`, "musl_sh does not strip shell quotes, and does
not parse redirection," and `docs/open-issues.md`.

**Rule of thumb: any test involving `<`, `>`, `|`, `&&`, `;`, or
quoting must be run from ash, not from `donix>`.**

## Session 32 (2026-09-29)
| Tag | What |
|-----|------|
| `20260928A` | session 31: port VT100/vi work from experimental tree (scratch) |
| `20260928B` | session 31: syscall.h cleanup, numeric dispatch order, comment fixes |
| `20260928C` | basics: rm works; unlink/mkdir resolve paths against cwd |
| `20260928D` | basics: rmdir works; SYS_RMDIR(84) + handler + CONFIG_RMDIR=y |
| `20260928E` | basics: cd .. works at donix>; chdir resolves against cwd |
| `20260928F` | tests: fault_pf verifies user-mode #PF and #GP kill both processes |
| `20260928G` | kmain: kernel shell reads DEL/CR, matching keyboard.c |
| `v0.6.4` | milestone: the basics are done |

**Milestone `v0.6.4` — the basics.**  Create, read, write, and
remove files and directories; `cd` up and down from both shells;
full-screen software runs; a faulting process is killed cleanly.

Session 31 ported the VT100/vi work from the experimental tree
(five files: `keyboard.c`, `vga.c`, `user_syscall.c`,
`include/syscall.h`, `configs/busybox.config`).  Session 32 finished
the remaining basics: `rm`, `rmdir`, `cd ..` at `donix>`, and the
user-mode `#PF` test.  The `#PF` test found a second bug — the
ring-3 `#GP` path halted the kernel instead of killing the process
— fixed in `isr13_handler` by keying the ring test on `CS & 3`
rather than the error code.  A third fix: `kmain_shell_loop` read
the old BS/LF bytes and had to be updated to DEL/CR after the
session-31 keyboard change.

All scratch tags (`20260928A`–`G`) were dropped before the
`v0.6.4` push, so the rows above may no longer resolve; the commit
messages carry the narrative.

## Session 30 (2026-09-28)
| Tag | What |
|-----|------|
| `20260928-20-geteuid` | kernel: implement geteuid(2) -- syscall 107 |
| `20260928-21-prctl` | kernel: implement prctl(2) PR_SET_NAME -- syscall 157 |
| `20260928-22-chdir` | kernel: implement chdir(2) -- syscall 80 |
| `20260928-23-cwd-resolution` | kernel: resolve relative paths against cwd, end to end |
| `20260928-24-userland-cwd` | userland: ls/cat pass paths through; kernel resolves cwd |
| `20260928-25-musl_sh-builtins` | musl_sh: cd, pwd, exit builtins; printf for pwd newline |
| `v0.6.3` | milestone: the shell is fully usable |

**Milestone `v0.6.3`** — `cd`, `pwd`, `ls`, `cat` respect the
working directory in both shells (`musl_sh` and busybox ash),
across `fork` and `execve`.  The serial log is free of
`Unknown syscall:` lines.

**Known limitation at this milestone:** `cd ..` at `donix>` failed
(the builtin passed raw `..` to FatFs); fixed in `v0.6.4`.

## Session 29 (2026-09-28)
| Tag | What |
|-----|------|
| `20260928-11-handoff-session-29` | handoff: session 29 -- port exploring-copy changes |
| `20260928-12-kernel-abs-path` | kernel: normalize Unix absolute paths in sys_execve |
| `20260928-13-musl_sh-passthrough` | musl_sh: pass argv[0] to execve unchanged |
| `20260928-14-kernel-bin-fallback` | kernel: add /bin fallback to sys_execve bare-name resolution |
| `20260928-15-busybox-standalone` | busybox: standalone shell mode, exec path /bin/busybox |
| `20260928-16-image-bin-busybox` | image: stage busybox at /bin/busybox, not BUSYBOX.ELF |
| `20260928-17-shell-autolaunch` | musl_sh: auto-launch /bin/busybox sh at startup |

## Session 28 (2026-09-28)
| Tag | What |
|-----|------|
| `20260928-08` | kernel: implement poll(2) -- syscall 7 |

(Note: `20260928-08` was previously an accidental duplicate of
`20260928-07` on commit `a413c21`.  It was local-only and had no
log row.  Deleted and re-pointed at the poll commit.)

## Session 27 (2026-09-28)
| Tag | What |
|-----|------|
| `20260928-04` | kernel: isr14_handler kills faulting process on user-mode #PF |
| (untagged) | handoff: session 27 mid-session checkpoint |
| `20260928-05` | kernel: align mkdir (83) and setsid (112) with the Linux ABI |
| `20260928-06` | busybox: enable pwd, wc, mkdir applets |
| `20260928-07` | handoff: session 27 -- syscall ABI audit, poll/geteuid next |

## Session 26 (2026-09-28)
| Tag | What |
|-----|------|
| `20260928-01` | handoff: split live state from reference material |
| `20260928-02` | vga: ANSI/VT100 subset parser -- busybox backspace works |
| `20260928-03` | handoff: session 26 -- vga VT100 subset, backspace fixed |

## Session 25 (2026-09-27)
| Tag | What |
|-----|------|
| `20260927-18` | kernel: implement mkdir(2) -- syscall 7 |
| `20260927-19` | busybox: enable FEATURE_EDITING_HISTORY=256 |
| `20260927-20` | handoff: session 25 |

## Session 24 (2026-09-27)
| Tag | What |
|-----|------|
| `20260927-13` | kernel: ioctl TCGETS/TCSETS*/TIOCGWINSZ -- interactive busybox ash |
| `20260927-14` | busybox: enable FEATURE_EDITING |
| `20260927-15` | handoff: session 24 |
| `20260927-16` | musl ls: stat argument before opendir |
| `20260927-17` | handoff: session 24 final |

(tags `20260927-13` through `-17` were deleted before the `v0.6.2`
push; from session 25 onward they are kept, so this is the last
session where the tags themselves no longer resolve -- the commit
messages carry the narrative)

## Session 23 (2026-09-27)
| Tag | What |
|-----|------|
| `20260927-12` | kernel: fork copies the ELF image region |

## Session 22 (2026-09-27)
| Tag | What |
|-----|------|
| `20260927-11` | kernel: sys_access/faccessat + FR_NO_PATH in stat retry |

## Session 21
| `20260927-10` | kernel: return proper errnos from file syscalls |

## Session 19
| `20260927-09` | kernel: sys_execve bare-name retry |

## Session 18
| `20260927-08` | kernel: implement getcwd(2) |

## Session 17
| `20260927-07` | kernel: implement setsid(2) and getppid(2) |

## Session 16
| `20260927-06` | kernel: implement F_DUPFD in fcntl(2) |

## Session 15
| `20260927-05` | kernel: implement dup2(2), file_slot_t refcounting |

## Session 14
| Tag | What |
|-----|------|
| `20260927-04` | busybox: track config under `configs/` |
| (untagged) | handoff: session 14 |
| (untagged) | kmain: bump donix banner to 0.6.1 |

## Session 13
| Tag | What |
|-----|------|
| `20260927-01` | kernel: fcntl(2), distinct anonymous mmap VAs, root/`./` path handling |
| `20260927-02` | build: busybox integrated into the userland build |
| `20260927-03` | handoff: session 13 -- published as `v0.6.1` |

## Session 12
| (untagged) | Docs restructure |

## Session 11
| Tag | What |
|-----|------|
| `20260927-00` | cleanup: remove residual newlib artifacts from `04_kernel_64bit` |
| `20260927-01` | A6.1: `userland/musl` tree, Makefile, and all sources |
| `20260927-02` | A6.23: build and stage musl userland from `userland/musl` |
| `20260927-03` | A6.24: delete `build_musl_tests.sh` |
| `v0.6.0` | version bump and A6 documentation pass |

(note: session 11 and session 13 both used `20260927-01`-`-03`;
session 11's tags were deleted before the reuse, so the session-11
tags no longer resolve -- the commit messages are the record)

## Per-test canary notes (session 33)

Focused canary, boot-into-ash, cwd-aware — green as of
`20260930-busybox-text-utils`:

| Row | Result |
|-----|--------|
| (boot lands in ash) | ash prompt; no `donix>` first |
| `pwd` (ash) | `/` |
| `cd /bin` (ash) | no error |
| `pwd` (ash) | `/bin` |
| `ls` (ash) | `busybox` |
| `cd ..` (ash) | no error |
| `pwd` (ash) | `/` |
| `exit` | returns to `donix>` |
| `pwd` (donix>) | `/` |
| `cd /bin` (donix>) | no error |
| `pwd` (donix>) | `/bin` |
| `ls` (donix>) | `busybox` (donix-native ls) |
| `cat busybox` (donix>) | reads /bin/busybox |
| `cd ..` (donix>) | no error |
| `pwd` (donix>) | `/` |
| `cd /` (donix>) | no error |
| `pwd` (donix>) | `/` |
| `ls hello-world.txt` (donix>) | found |
| `memtest` | PASS |
| `musl_fork` | A/P/C, clean EXIT |
| `musl_exec2` | three execs, EXEC2-OK |
| `musl_wait` | WAIT-STATUS-OK 42, WAIT-WNOHANG-OK, WAIT-ANY-1/2, WAIT-ALL-OK |
| `busybox ls` (donix>) | full root listing |
| `busybox pwd` (donix>) | `/` |
| `busybox ash` (donix>) | ash prompt; `pwd`, `cd /bin`, `pwd`, `ls`, `exit` |

No `Unknown syscall:` lines anywhere.  Two informational-only
lines appear and are expected:

- `EXIT: pid=N state=1 parent=2 qhead=N` for the forked busybox
  shell's own exit (existing shell-exit path, unchanged).
- `EXIT-FALLBACK: switching to idle, ...` in `musl_fork` when the
  child is the last runnable process (existing fallback).

One-off verifications (not canary rows — they mutate the disk or
crash the process):

- **Script execution, three ways**:
  - `./test.sh` → `Hello, world` (kernel: `sys_execve: not an ELF
    file` → ash fallback → fork+sh → child runs script).
  - `sh test.sh` → `Hello, world`.
  - `busybox sh test.sh` from `donix>` → `Hello, world`.
- **Shell redirection** (session 33, `20260930-redirect`), run from
  ash:
  - `cat < hello-world.txt` → prints the file, returns.
  - `echo hi > out.txt` → silent; `cat out.txt` → `hi`;
    `ls out.txt` → found.
- **`uname`**: `busybox uname` → `Linux`; `busybox uname -a` →
  `Linux donix 6.0.0 #1 donix x86_64`.
- **`head`**: `busybox head -n 3` / `-n 1` / default against
  `hello-world.txt` → expected lines.  Exercises `lseek`.
- **`tail`**: `busybox tail -n 1` / `-n 2` / default → expected
  lines.  Exercises `lseek(SEEK_END, negative)`.
- **`cp`**: `busybox cp hello-world.txt copy.txt`; `cat copy.txt`
  matches; `ls copy.txt` finds it.
- **`grep`**: `busybox grep Hello hello-world.txt` prints the
  matching line; `grep hello` (lowercase) prints nothing
  (case-sensitive); `grep nosuchstring` prints nothing.
- **`sed`**: `busybox sed -n '1p'` / `sed 's/Hello/Goodbye/'` /
  `sed 's/fatcat/FATCAT/'` from ash → expected output.  From
  `donix>` the quotes must be omitted (musl_sh does not strip
  quotes); `sed -n 1p` works.
- **Text utilities batch** (session 33,
  `20260930-busybox-text-utils`), run from ash:
  - `echo hello` → `hello`
  - `cut -c 1-5 hello-world.txt` → first 5 columns per line
  - `sort hello-world.txt` → ASCII-ordered lines
  - `stat hello-world.txt` → full stat output (expected
    `/etc/passwd` etc. `f_open FAIL` noise)
  - `test 1 -eq 1 && echo YES` → `YES`
  - `tr a-z A-Z < hello-world.txt` → uppercase
  - `tee copy2.txt < hello-world.txt` → writes AND prints;
    `cat copy2.txt` matches
  - `cmp hello-world.txt hello-world.txt` → no output (identical)
- vi round-trip: `vi test.sh`, edit, `:wq`, `cat test.sh` reads
  the text back.
- `fault_pf` — user-mode `#PF`, process killed, shell returns.
- `mkdir`/`rmdir` create/remove, nested cwd create/remove (from
  the `v0.6.4` notes; unchanged).

**Known failures in this session, left off deliberately:**

- `uniq hello-world.txt` — hangs on any invocation, with or
  without `-c`.  Ctrl-C required.  Cause unknown; needs a
  trace-first investigation.  `CONFIG_UNIQ` is off.
- `od -c hello-world.txt` — `Unknown syscall: 19` (`readv`).
  `CONFIG_OD` is off.  `readv` is its own kernel commit.
