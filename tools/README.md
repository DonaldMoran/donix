# tools/

Test harnesses for driving donix from a script. None of this is
kernel, userland, or image code.

## gate.exp

Boots `./run headless` and runs the three automated gates:
`canary --full`, `/root/scripts/test.sh`, `/usr/bin/EXEC_CHURN`.
Prints a three-line verdict; exits 0 on all-green.

    expect -f tools/gate.exp

**It does not cover the two shell behaviors** -- `sleep 100` then
`^C`, and `exit` from `ash` -> `donix>` -> `busybox sh` -> ash.
Those stay manual, and a green verdict here is **not** the gate.
See `handoff.md`'s "THE GATE GAP".

`./run headless` uses `-no-reboot -no-shutdown`, so `exit` from
`ash` drops to `donix>` and does not end the guest; the script
kills the `./run` child instead. `capture.txt` is written by
`./run` and is overwritten on every run -- the script does not copy
it. `tools/gate.expect.log` is the expect pty log (gitignored).
