Append-only.  One row per commit, named by tag only -- never by
SHA.  Git resolves tags; the handoff never duplicates what git
already records.  Working tags are local and permanent.

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
`Unknown syscall:` lines.  Three fixes made cwd work end to end:
`resolve_against_cwd` in `sys_open`/`sys_stat`/`sys_access`,
`sys_fork` copying `cwd`, and `sys_chdir` storing an absolute
path.  `ls.c`/`cat.c` stopped prepending `0:/`.  `musl_sh` gained
`cd`/`pwd`/`exit` builtins.

**Known limitation at this milestone:** `cd ..` at `donix>` fails
(the builtin passes raw `..` to FatFs); `cd ..` inside ash works.

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

## Per-test canary notes (session 30, v0.6.3)

Focused canary, boot-into-ash, cwd-aware (green as of
`20260928-25`):

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
| `cd /` (donix>) | no error |
| `pwd` (donix>) | `/` |
| `busybox pwd` (donix>) | `/bin` after `cd /bin` |

No `Unknown syscall:` lines anywhere.  `cd ..` at `donix>` is a
known failure and is NOT a canary row.
