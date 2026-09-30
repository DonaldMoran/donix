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

### Redirection, sequences, pipelines

| Tag | What |
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

The busybox config commit enabled `false`/`true` (needed to test
`&&` short-circuiting on a real child's exit code) and
`yes`/`seq`/`clear` (bundled in the same edit; `yes` and `seq` are
for pipeline stress tests and were not required for this session).
The commit has no message body, so this is the only record of why.

The debug print was scaffolding for the tokenizer steps and was
removed once the parser consumed argv.  No functional change.

### Verification (`donix>`, not canary rows)

Quoting and operator splitting:

    echo "a b" c        -> a b c
    echo 'x y'          -> x y
    echo hi>out         -> tokenized echo hi > out; echo prints "hi > out"
    echo a && echo b    -> a then b

Redirection round trip:

    echo hi > out.txt ; cat out.txt
    echo hi2 >> out.txt ; cat out.txt      # hi then hi2
    busybox cat < out.txt                  # hi
    echo one > out.txt ; cat out.txt        # one (truncates)
    echo two > out.txt ; cat out.txt        # two, not one+two

Note: `cat < out.txt` (donix-native `cat`) fails with `usage: cat
FILE` — donix's `cat` has no stdin mode.  `busybox cat < out.txt`
works.  That is a `cat.elf` limitation, not a redirection bug.

Sequences:

    echo a ; echo b
    busybox false && echo skipped           # nothing
    busybox false && echo x ; echo y        # y only
    cd /nope && echo not-reached            # cd error, no not-reached
    exit ; echo no                          # bye, no no

Pipelines (the milestone headline, now runnable from `donix>`, not
only ash):

    cat hello-world.txt | busybox head -n 2
    echo hi | busybox wc                    # 1 1 3
    echo hello | busybox cat                # hello
    cat hello-world.txt | busybox head -n 2 | busybox wc   # 2 16 97
    cat hello-world.txt | busybox head -n 2 > f ; cat f
    cd /bin | busybox cat                   # refused: builtin in pipeline
    busybox yes | busybox head -n 1         # see below

`cat | head | wc` is the three-stage case that proves the middle
child's fd close: it would deadlock if the middle command held the
upstream write end.

### `-EPIPE` observation — the docs prediction was wrong

`v0.6.6`'s `open-issues.md` and handoff predicted that
`busybox yes | busybox head -n 1` would hang, because `-EPIPE` is
delivered without `SIGPIPE` and busybox `yes` "does not handle it."

Observed this session: it does **not** hang.  `head` prints `y` and
exits; `yes` receives `-EPIPE`, **handles it**, prints
`yes: Broken pipe`, and exits; the shell reaps both and returns to
the prompt.  The prediction was too pessimistic and named the wrong
example.  The general gap (a program that expects to be *killed* by
`SIGPIPE` and does not check `write`'s return) remains open, but no
program has been found that exhibits it.  `open-issues.md` corrected
accordingly.

### Expected noise

No `Unknown syscall:` lines.  No `[fd]` lines (the fd trace is off).
No `[a|b|c]` debug line — removed this session.  Expected
informational lines: `EXIT: pid=N state=2 parent=P qhead=Q` for each
forked child; `EXIT: pid=N state=1 parent=P qhead=Q` for a forked
shell's own exit; `EXIT-FALLBACK: switching to idle, ...` when a
child is the last runnable process.  `yes: Broken pipe` from the
`busybox yes | busybox head -n 1` line is expected.

### Docs note — `capture.txt` and backspaces

A backspaced line appears in `capture.txt` as its full typed history
(the `\b \b` erase bytes are captured literally), not its corrected
form.  Read the `sys_execve: ... argc=N` line or the VGA, not the
echoed line.  See `gotchas.md`, "`capture.txt` shows backspace
history…".  This cost real time this session chasing a phantom
tokenizer bug.

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
