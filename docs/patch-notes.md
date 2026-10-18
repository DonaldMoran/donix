# Patch notes — G1 piecewise re-derivation

This directory holds reference patches developed on top of the
seven-of-eight G1 state (tag `20261007-G1-7of8`, commit `a970fd7`).

The patches are **reference artifacts**, not the mechanism to
apply the changes.  Each section below says what the actual
mechanism is.

## Current tree state

Seven of the eight G1 trace-gating pieces are applied and
committed:

| Piece | File                                    | Status       |
|-------|-----------------------------------------|--------------|
| P1    | `.gitignore` + `include/trace.h`        | committed    |
| P2    | `kmain.c` boot prints                   | committed    |
| P3    | `sys_open` FAIL prints                  | committed    |
| P4    | `scheduler.c` EXIT: prints              | committed    |
| P5    | `sys_execve:` success + ENOEXEC prints  | **NOT APPLIED** |
| P6    | `sys_wait4` WW: wait4 prints            | committed    |
| P7    | `process.c` WW: child= prints           | committed    |
| P8    | `sys_execve:` ERR-family prints         | committed    |

## P5 — why it is not applied

P5 gates the `sys_execve:` success-path print (and the ENOEXEC
family of prints).  Both macros default to `0` in `trace.h`, so
applying P5 compiles out the success print.

That print is a **timing cushion**.  It performs ~5 ms of UART
busy-wait on every `execve`, which slows the fork/exec/wait
cycle enough that a frame-corruption bug on the syscall-return
path stays quiescent.

With P5 applied, the bug is exposed:

- `pipe7e` faults at iteration 0.
- `test.sh` faults under load.
- The fault is either:
  - a kernel-mode `#PF` or `#UD` at `CS=0x18` with a **user
    RIP** and a **user-stack RSP** (the "hybrid frame"
    signature), or
  - a user-mode `#PF` with a **corrupted user RSP** — the RSP
    is an offset-like value (~0x290), not an address.

**The frame bug has not been fixed.**  Do not apply P5 to the
main branch until it is.

## How to apply P5 (for investigation)

Do NOT use `git apply docs/patch-P5-sys_execve-gates.patch`.
That patch was generated as a diff between `ca370f5` and the
P5-only tag; its context lines will not match the current tree
because P6, P7, and P8 have since changed `user_syscall.c`.
(Confirmed: `git apply --check` reports "patch does not apply".)

Use the commit instead:

    git cherry-pick eb36ece

That applies P5's source edit on top of the current HEAD and
commits it as a new commit.  To undo it:

    git reset --hard HEAD~1

Alternatively, check out the tag that already has P5 applied:

    git checkout -b p5-investigation 20261007-P5-only-repro

That branch is `ca370f5` plus P5, and does not include P6/P7/P8.

### Note on the include

The P5 patch adds `#include "include/trace.h"` a second time
(P3 had already added it).  The duplicate is harmless — the
header has an include guard — but a reviewer who wants a clean
P5-only edit should drop the second occurrence, not the first.

## The vmm huge-page fix

`patch-vmm-hugepage-delegation.patch` is a separate, correct
improvement to `04_kernel_64bit/vmm.c`.  It does **not** fix the
frame bug; it fixes a latent huge-page corruption bug.

The problem: `vmm_map_page`, `vmm_unmap_page`, and
`vmm_is_mapped` each carried their own page-table walk that did
not handle 2 MB huge pages.  The bootloader installs huge pages
for the low-memory identity map.

- `vmm_map_page` wrote a new PTE into the huge page's physical
  base (dereferencing the PDE as a page-table pointer).
- `vmm_unmap_page` wrote a zero into the same place.
- `vmm_is_mapped` returned a wrong answer, which fed
  `ensure_hhdm_mapped` into calling `vmm_map_page`
  unnecessarily.

The fix delegates these three to their `_in_cr3` variants
(which already handle huge pages) and adds the missing split
to `vmm_unmap_page_in_cr3`.

Apply with:

    git apply docs/patch-vmm-hugepage-delegation.patch

This patch's base is `a970fd7` (the current HEAD), so it applies
cleanly.  Verify first:

    git apply --check docs/patch-vmm-hugepage-delegation.patch

It is orthogonal to P5; apply it whenever you want.

## Reproducing the frame bug

The tag `20261007-P5-only-repro` is `ca370f5` plus P5 applied.

    git checkout -b p5-investigation 20261007-P5-only-repro
    make -C 04_kernel_64bit
    make -C 05_boot_kernel64 hdd-single.img
    ./run
    # at the # prompt:
    pipe7e

The fault fires at iteration 0.

## Candidate mechanisms for the frame bug

The kernel returns a process to user mode with a corrupted user
RSP.  Only RSP is wrong; RIP, CS, and RFLAGS are consistent.
The corrupt RSP is an offset-like value (~0x290), not an
address.  The user's first stack access after resume faults at
CR2 ~= RSP - 8.

Places that write a user RSP into a frame the kernel will
`iretq`:

1. `process_fork_copy_frame` in `process.c` — builds the child's
   first iretq frame at `child->kernel_stack_top - 160`.  The
   frame's user RSP slot is the parent's user RSP at fork time.
2. `context_switch.asm`'s user save path — uses `prev->rsp`
   for the RSP slot.
3. The timer preempt's user-mode save — sets
   `current->rsp = stack_pointer`.  Whether this is a user or
   kernel stack address depends on where the interrupt frame
   was pushed.
4. The child's own syscall entry (`user_syscall_entry.asm`)
   pushes 16 slots at the top of the kernel stack.  If those
   overlap the fork frame at `top - 160`, the fork frame is
   overwritten before the first iretq.

A useful diagnostic to add: in `context_switch.asm`'s restore
path, right before `iretq`, read `[rsp + 0x90]` (the RSP slot
the iretq will load) and, if it is small (< 0x1000 or between
0x200 and 0x400), print it along with the frame base and the
current PID.  That catches the corruption at the moment of use.

## Tags to keep

- `20261007-G1-7of8`         seven of eight G1 pieces (P5 out)
- `20261007-P5-only-repro`   P5 applied on `ca370f5` (reproduces bug)
- `20261007-Bug2-repro`      earlier WIP with instrumentation + yield fix
- `20261007-BaselineB`       state before any G1 pieces
- `20261007-session-start`   the first commit of the session

Do not drop these; the next session depends on them.

## Do not

- Do not edit `third_party/`
- Do not apply P5 to `main` until the frame bug is fixed
- Do not re-apply the `rsp`-selector change to `context_switch.asm`
  (tested, made the fault rate worse, reverted)
- Do not assume the patches in this directory are the mechanism
  to apply their changes; read the section for each patch.

