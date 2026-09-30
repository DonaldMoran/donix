## Session 39 — `20260930-at` (opens `v0.6.8`)

| Tag | What |
|---|---|
| `20260930-at` | `resolve_at`, `file_slot_t.dir_path`, `newfstatat` (262), `openat` (257); stat family inverted; `find` enabled with `-type` |

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
