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
sentinels in fds 0/1/2 (so a fresh process's `open` returns 3),
`alloc_file_slot` scanning from 0 (so `open` returns 0 after
`close(0)`), and `sys_dup2` / `sys_fcntl(F_DUPFD)` using
`get_file_slot_any` (so a redirect whose scratch fd is 0/1/2 works
and the save/restore dance does not break the *second* redirect in a
shell).  Full writeup in `gotchas.md`, "Low fds (0/1/2) are
first-class."

### Thread 2 — busybox

| Tag | What |
|---|---|
| `20260930-busybox-uniq` | `CONFIG_UNIQ=y` |

The applet's hang was the kernel fd bug above, not an applet bug.
With low fds first-class it works in both the FILE-argument and
redirected-stdin forms.

### Canary

Focused canary green on both commits.  The new read-only `uniq` rows
are now valid additions:

    uniq hello-world.txt
    uniq -c < hello-world.txt

Both read-only, both safe as canary rows.  The redirected one is
ash-only (see `open-issues.md`, test-design notes).

Verification one-offs (mutate, not canary):

    cat < hello-world.txt
    echo hi > out.txt       ; cat out.txt
    echo hi2 > out2.txt     ; cat out2.txt
    head -n 2 hello-world.txt
    wc hello-world.txt
    grep Hello hello-world.txt

The two separate `echo >` lines matter: the *second* redirect in one
shell is what failed before the `dup2`/`fcntl` fix.

### Expected noise

No `Unknown syscall:` lines.  No `[fd]` lines (trace is off).  The
usual `EXIT: pid=N state=...` lines, the `WAIT-WNOHANG-OK` spin from
`musl_wait`, and the `uniq: can't open '<'` line from `donix>` (the
`musl_sh` redirection gap) are all expected.
