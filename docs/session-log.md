
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
broadcast.  Blocking loops follow the cli-test-record-block-hlt
discipline from `sys_read` fd-0 and `sys_poll`.  Test
`pipe_step2.c`: 7 checks, pass.

**Step 3.**  `file_slot_t.end` flag (read/write);
`pipe_t.readers_open`/`writers_open` *counts*.  Counts, not flags,
because `pipe(fds); dup2(fds[1], 1)` leaves two fds holding the
write end, and closing one must not look like "the writer closed."
`read` on empty + `writers_open == 0` → 0 (EOF); `write` with
`readers_open == 0` → `-EPIPE`, checked *before* the full-buffer
check.  Test `pipe_step3.c`: 6 checks, pass; log ordering confirms
the reader stayed blocked through the first close.

**Step 3.5.**  `put_file_slot`'s pipe case now wakes the peer when
a count reaches zero.  Without it, a writer that `_exit`s without
closing leaves a reader stuck in `hlt` until a keystroke.  Test
`pipe_step3b.c`: child holds write end, never closes, `_exit`s;
parent's `read` must return 0 on the exit alone.  STEP3B OK first
run.

**Step 4.**  The stdio guard inversion.  `sys_read` fd-0 and
`sys_write` fd-1/2 guards changed from `kind != FILE_KIND_FILE` to
`kind == FILE_KIND_CONSOLE`.  The old form mis-routed a pipe (kind
`FILE_KIND_PIPE` is `!= FILE_KIND_FILE`, so a piped stdin took the
keyboard path and blocked).  Full writeup: `gotchas.md`, "Negative
fd-kind tests don't extend to new kinds."

### Also in the milestone

| Tag | What |
|---|---|
| (dropped) | `dup(2)` (syscall 32) as `fcntl(F_DUPFD, 0)` |
| (dropped) | bump version banner to `v0.6.6` |
| (dropped) | `pipe_step3` back to `dup()` now that `dup(2)` exists |

`dup(2)` was found by `pipe_step3`'s first run, not by reading
code: musl's `dup()` reaches `SYS_dup` directly and donix had no
handler, so any caller got `-ENOSYS`.  Implemented as a one-line
delegation to `sys_fcntl`.  The test had been using
`fcntl(F_DUPFD)` as a workaround; with `dup(2)` in, the test goes
back to `dup()` so it exercises the real path.

### Canary

Focused canary green.  Pipeline rows (new this milestone, run from
ash):

    cat hello-world.txt | head -n 2
    echo hi | wc                      # 1 1 3
    echo hello | cat                  # hello

`cat | head` exercises blocking on both ends; `echo hi | wc`
exercises EOF (the reader must see the writer close and stop);
`echo hello | cat` is the smallest end-to-end case.

Pipe regression suite (`userland/musl/tests/`), run from ash:

    pipe_step1    # object + non-blocking I/O
    pipe_step2    # blocking + directed wake
    pipe_step3    # EOF, EPIPE, dup-aware counts
    pipe_step3b   # exit-path wake

Four binaries, `STEPn OK` on success.  Not canary rows — they fork
and take seconds — but they are the only regression suite the pipe
code has, and they do not mutate the disk.

### Expected noise

No `Unknown syscall:` lines.  No `[fd]` lines (trace is off).  The
usual `EXIT: pid=N state=...` lines for forked children;
`EXIT-FALLBACK: switching to idle, ...` when a child is the last
runnable process; the `WAIT-WNOHANG-OK` spin from `musl_wait`; the
`uniq: can't open '<'` line from `donix>` (the `musl_sh`
redirection gap, still open — headline for `v0.6.7`).

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
