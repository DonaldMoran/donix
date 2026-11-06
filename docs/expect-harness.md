# The expect harness

How to drive donix from a script. `tools/gate.exp` is the first
resident; this file is the pattern for writing the next one.

Read `tools/gate.exp` alongside this file. Everything below is
demonstrated there.

---

## Where the `.exp` file goes

**`tools/` is for tracked, reproducible harnesses** -- the
automated gate, and anything else that should ship with a clone
and be re-runnable by any session.

**An ad hoc probe goes in `/tmp`.** If the question is "does
`cat` read this file" or "does this one change still boot," the
`.exp` is scratch: write it to `/tmp/<name>.exp` (and its log to
`/tmp/<name>.expect.log`), run it, delete it or let `/tmp` clear.
Do not leave a one-off under `tools/` -- it shows up as `??` in
`git status` and a later session has to decide whether it is a
tool or debris.

The same `/tmp` caveat as any paste applies: re-write the script
in the same paste that runs it if `/tmp` may have been cleared.

---

## Two spawn patterns

### Pattern A -- `./run headless` (builds and boots)

    spawn ./run headless

`./run headless` runs `make -C 04_kernel_64bit FAT_CONFIG=single`,
`make -C 05_boot_kernel64 hdd-single.img`, then QEMU with
`-serial stdio -display none`. Because it rebuilds the FAT image,
it wipes anything that was `mcopy`'d onto `hdd-single.img` after
the build.

**Use Pattern A when nothing is staged after the build** -- the
automated gate (`canary`, `test.sh`, `EXEC_CHURN`) is already on
the image, so the rebuild is safe and is also a build check.

**Do NOT use Pattern A for a test that stages a binary.** The
`make hdd-single.img` step recreates the image; a binary added with
`mcopy` afterward is gone on the next boot, and the guest reports
`MUSL_VFORK: not found` (or whatever the staged name is). That
failure is a stale-image symptom, not a missing file in the tree.

### Pattern B -- spawn QEMU directly (no rebuild, staged binary)

    spawn qemu-system-x86_64 \
        -drive file=05_boot_kernel64/hdd-single.img,format=raw,if=ide,index=0 \
        -enable-kvm -cpu host -serial stdio -s -monitor none \
        -no-reboot -no-shutdown -display none

No `make`, no FAT rebuild -- the image is used as-is. **Use
Pattern B when a binary was staged onto the image after the build.**

The QEMU flags above are the ones `run` uses; if `run` changes,
re-read them with `grep -n 'qemu-system' run` rather than trusting
this copy. The load-bearing ones for `expect` are `-serial stdio`
(so `expect` sees the console) and `-display none` (so it does not
need a window). `-s` is the gdb stub on `:1234`; drop it if the
test does not use gdb.

**Control bytes (`^C`, `^Z`) need the socket form, not either pattern
above.** Pattern A pipes QEMU through `tee`, so a byte written to the
expect pty never reaches the guest UART. Pattern B with `-serial
stdio` makes QEMU's stdin a host tty, where `^C` is the tty INTR char
and SIGINTs QEMU itself (`qemu-system-x86_64: terminating on signal
2`). To deliver a control byte to the guest, put the serial on a UNIX
socket and drive it through `socat` in raw mode:

    spawn qemu-system-x86_64 ... \
        -serial unix:/tmp/donix-ctrlc.sock,server,nowait ...
    while {![file exists /tmp/donix-ctrlc.sock]} { after 100 }
    spawn socat -,raw,echo=0 UNIX-CONNECT:/tmp/donix-ctrlc.sock
    ...
    send "\003"

`-,raw,echo=0` is load-bearing: without it `0x03` is socat's INTR char
and kills socat. `tools/coreutils_sweep.exp`'s `sleep 100 + ^C` row is
the worked example.


---

## Two verdict-pattern rules (both are bugs we hit)

### Rule 1 -- match ONLY the verdict string

Wrong:

    expect {
        -re {57 passed, 0 failed} { set ok 1 }
        -re {/ # $}               { set ok 0 }   ;# WRONG
    }

`expect` matches the **earliest** pattern in the buffer. After the
previous command, a stray `\n/ # ` is still in the buffer, so the
prompt alternative matches **before** `test.sh` has run, and the
script reports a false FAIL. This happened: `test.sh` printed
`57 passed, 0 failed` and the harness said it had not.

Right:

    expect {
        -re {57 passed, 0 failed} { set ok 1 }
        timeout                   { set ok 0 }
    }
    expect -re {(\r?\n|^)/ # $}          ;# drain to the prompt

The verdict string is the only evidence. A missing verdict is a
`timeout`, which is a real failure.

### Rule 2 -- drain to the prompt after each check

After the verdict matches, consume the trailing prompt **before**
sending the next command, or the next block starts on a dirty
buffer and Rule 1 bites again:

    send "canary --full\r"
    expect -re {28 passed, 0 failed}
    expect -re {(\r?\n|^)/ # $}
    send "/root/scripts/test.sh\r"
    ...

Anchor the prompt at end-of-line (`(\r?\n|^)...$`), not as a
substring; `~ #` also matches inside the BusyBox banner line
`BusyBox v1.36.1 ...`.

---

## Shutdown rule (`-no-shutdown`)

`./run headless` and Pattern B both use `-no-reboot -no-shutdown`.
A fault stops the guest; `exit` from `ash` drops to `donix>`, and
`donix>` does not exit. So:

- **Do not wait for `eof` after `send "exit\r"`.** It will not
  come.
- **Kill the `./run` child** (or the QEMU pid) instead:

      set run_pid [exp_pid $spawn_id]
      send "exit\r"
      after 2000
      catch {exec kill -TERM $run_pid}
      catch {exec pkill -TERM -P $run_pid}
      close
      wait

- **If the test needs a clean guest shutdown** (e.g. to prove that
  `reboot` works), use a `no-freeze` QEMU base (no `-no-shutdown`)
  and spawn QEMU directly, not `./run headless`.

---

## Capture rule

`capture.txt` is written by `./run` and is **overwritten on every
run** -- by hand or by `expect`. It is the guest's serial log.

- **Do not copy it.** One artifact, overwritten each run.
- **The `expect` side has its own log.** `tools/gate.exp` uses
  `log_file` for the pty stream (build output + guest + the
  script's own `puts`). It is `*.log`, gitignored under `tools/`.
- **The verdict lines live in the `expect` log, not in
  `capture.txt`.** `capture.txt` ends where the guest stopped; it
  does not contain the `GATE VERDICT` block.

---

## What the harness is not

`tools/gate.exp` covers `canary --full`, `test.sh`, and
`EXEC_CHURN`. It does **not** cover the two shell behaviors:

- `sleep 100` then `^C` -> the prompt returns
- `exit` from `ash` -> `donix>` -> `busybox sh` -> the ash prompt

Those need real keystroke timing, and a green `tools/gate.exp` is
**not** the gate. See `handoff.md`'s "THE GATE GAP".
