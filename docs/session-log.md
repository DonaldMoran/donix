Append-only.  One row per commit, named by tag only -- never by
SHA.  Git resolves tags; the handoff never duplicates what git
already records.  Working tags are local and permanent.

## Session 33 (2026-09-29)
| Tag | What |
|-----|------|
| `20260929-max-process-files-64` | process: raise MAX_PROCESS_FILES 8 -> 64; `sh script.sh` works |
| `20260929-execve-script-fallback` | execve: strip `./` before open; return ENOEXEC for short non-ELF; `./script.sh` works |

**Shell scripts run.**  Two commits, both on `dev`, both scratch-
tagged (local, dropped before the next `v*` push):

- `MAX_PROCESS_FILES` 8 -> 64.  At 8, busybox ash's script
  fork+exec path fails with `sh: 3: Invalid argument`; at 64 the
  same script prints its output.  Controlled: same build, same
  script, only the constant varied.  Cost +448 B/pcb; assembly-safe
  (offset past `block_kind`).

- `sys_execve`: two coordinated fixes.  (1) Normalize the path
  with `strip_dot_prefix` before the first `f_open`, so `./test.sh`
  no longer fails with `FR_INVALID_NAME` at the open.  (2) Return
  `ENOEXEC`, not `EIO`, for a short non-ELF read, so ash falls back
  to running the script through `sh`.

Result: `./test.sh`, `sh test.sh`, and `busybox sh test.sh` from
`donix>` all print the script output.  Verified by the focused
canary (below) plus the three one-off script invocations.  No
`Unknown syscall:` lines.

Not ported from the experimental tree: `uname(2)` and the extra
`busybox.config` applets.  Neither is needed for script execution;
both remain candidates for a later milestone.

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

Focused canary, boot-into-ash, cwd-aware — green as of the
`20260929-execve-script-fallback` commit:

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

- **Script execution, three ways** (new this session):
  - `./test.sh` → `Hello, world` (kernel: `sys_execve: not an ELF
    file` → ash fallback → fork+sh → child runs script).
  - `sh test.sh` → `Hello, world`.
  - `busybox sh test.sh` from `donix>` → `Hello, world`.
- vi round-trip: `vi test.sh`, edit, `:wq`, `cat test.sh` reads
  the text back.
- `fault_pf` — user-mode `#PF`, process killed, shell returns.
- `mkdir`/`rmdir` create/remove, nested cwd create/remove (from
  the `v0.6.4` notes; unchanged).
