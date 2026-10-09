# A blocked syscall resumes with a stale caller-saved register file

**Status:** mechanism read; fix not written.  Written in session 61.
**Acceptance test:** `pipe7e` (`/usr/bin/PIPE7E`) under `./run single-kvm`.

## The observation

`pipe7e` (the `x=$(cmd)` syscall shape, first-party, no shell)
fails under KVM with:

    FAIL iter 27: read ret=-2144779632 errno=0 str=No error information

- `read ret = -2144779632` is `0xFFFFFFFF802946E0` -- a kernel
  high-half address, not `-1`, not an errno.
- `errno = 0` because musl's gate is `if (ret > -4096UL)`; a value
  below that is passed through untouched and `errno` is not set.
  The kernel's own docstring at `user_syscall.c:87-92` says this.
- The iteration number in earlier runs (1032176, against ITERS
  50000) was the print reading a wrong value, not a corrupted loop
  counter: `i_copy` printed `27`, in range.

So the failure is: **`sys_read` returns a kernel pointer.**

## What was ruled out, by reading

- **`sys_read` is clean.** Every `return` in the pipe branch, the
  file branch, the `/proc` branch, and the entry is a literal, a
  `long`-cast count, or a negative errno.  None can be a kernel
  address.
- **`syscall_dispatch` is clean.** Every case is
  `return (uint64_t)sys_X(...)`.  The `default:` returns `-ENOSYS`.
  No case stores through a pointer.
- **The sysret tail in `user_syscall_entry.asm` does not touch
  `%rax`.**  It is the syscall return value at `sysret`.

## The mechanism

`context_switch.asm`'s ring-0 save path (`.ring0_save`) builds the
resume frame by pushing 15 registers, every one sourced from the
pcb:

    push qword [r15 + 0x108]   ; rax
    push qword [r15 + 0x100]   ; rbx
    push qword [r15 + 0x0F8]   ; rcx
    push qword [r15 + 0x0F0]   ; rdx
    push qword [r15 + 0x0E8]   ; rsi
    push qword [r15 + 0x0E0]   ; rdi
    push qword [r15 + 0x0D8]   ; rbp
    push qword [r15 + 0x0D0]   ; r8
    push qword [r15 + 0x0C8]   ; r9
    push qword [r15 + 0x0C0]   ; r10
    push qword [r15 + 0x0B8]   ; r11
    push qword [r15 + 0x0B0]   ; r12
    push qword [r15 + 0x0A8]   ; r13
    push qword [r15 + 0x0A0]   ; r14
    push qword [r15 + 0x098]   ; r15

**And the same path writes back to the pcb only six of them:**

    mov rax, [rsp + 0x00] ; mov [r15 + 0x098], rax   ; r15
    mov rax, [rsp + 0x08] ; mov [r15 + 0x0A0], rax   ; r14
    mov rax, [rsp + 0x10] ; mov [r15 + 0x0A8], rax   ; r13
    mov rax, [rsp + 0x18] ; mov [r15 + 0x0B0], rax   ; r12
    mov rax, [rsp + 0x20] ; mov [r15 + 0x100], rax   ; rbx
    mov rax, [rsp + 0x28] ; mov [r15 + 0x0D8], rax   ; rbp

So on a ring-0 resume, the **callee-saved** registers are restored
from live values, and the **caller-saved** registers -- `rax`,
`rcx`, `rdx`, `rsi`, `rdi`, `r8`, `r9`, `r10`, `r11` -- are
restored from pcb slots that the save path never writes.  A grep
for `->rax` / `0x108` across `process.c`, `scheduler.c`,
`user_syscall.c`, `context_switch.asm`, and `process.h` finds one
`_Static_assert` and three reads; **no write.**

The resume is `.pop_frame`:

    pop r15 ... pop rbx; pop rax; iretq

`iretq` returns to the instruction after `hlt` in the compiled
`sys_read`, with the full stale register file.

## Why this produces the observed value

`sys_read`'s pipe block loop compiles to:

    80114199: mov %rax,0x28(%r14)    ; pipe->reader_waiting = self
    8011419d: movl $0x3,0x28(%rax)   ; self->state = BLOCKED
    801141a4: movq $0x2,0x158(%rax)  ; self->block_kind = PIPE_READ
    801141af: sti
    801141b0: hlt
    801141b1: cli                    ; <-- resume lands here
    801141b2: cmpl $0x0,0x4(%r14)    ; pipe->used > 0?
    801141b7: jne 80114141           ; data available: copy path

The copy path uses `%rax`, `%rcx`, `%rsi`, `%rdi` to compute the
return.  After resume those hold pcb-stale values, so the returned
count is garbage -- `0xFFFFFFFF802946E0` in the observed run.

The **same bug** produces the other two symptoms seen this session:

- `#PF` at `CR2 = RSP = 0x296` -- a stale `%rsp` from the pcb.
- `#UD` at `RIP = 3`, and `#PF` at `CR2 = RIP = 0xFFFFFFFF` -- a
  stale `%rip` from the pcb.

## What the fix must do

Save the live caller-saved registers into the pcb **at the block
point**, before `hlt`, so the resume restores the register file the
C code expects.  The block point is the only place the live values
are visible; `context_switch.asm` runs after the C compiler's
register file may already be clobbered by the scheduler call chain.

The block sites in `user_syscall.c` are the ones that do
`state = PROC_STATE_BLOCKED; ... sti; hlt`:

- `sys_read` pipe branch
- `sys_read` console branch
- `sys_poll`
- `sys_wait4`
- any other site that blocks

## The probe, before the full fix

A full fix touches every block site and the resume path.  Before
that, a one-site probe confirms the mechanism:

**Save `%rax` into `pcb->rax` at `sys_read`'s pipe block site
before `hlt`.**  Rebuild, run `pipe7e` under KVM.

- If `read ret` becomes a sane value (a count, `-EINVAL`, `0`) or
  the failure moves, the mechanism is confirmed: the resume was
  restoring a stale register.
- If `read ret` is still a kernel pointer, the copy path is using
  another stale register (`%rcx`, `%rsi`, `%rdi`) and the fix must
  cover all nine -- which the mechanism predicts.

Either outcome is informative, and neither is a fix.  The probe is
a measurement.

## Open questions, before the full fix

- Which registers does the compiled code actually rely on across
  the `hlt`?  Read the disassembly of each block site.
- Does `%rsp`/`%rip` need separate treatment from the nine GPRs?
  The save path sources them separately (`[r15 + 0x110]`,
  `[r15 + 0x118]`); a block inside a syscall has a kernel `rsp`
  that must be saved too.
- Is a per-site inline-asm save correct, or does the save belong
  in one place with a compiler-visible contract?  A partial save is
  worse than none.
- What is the guard?  `pipe7e`, the pipe regression suite
  (`pipe_step1..3b`), and `canary --full`.

## Relationship to open-issues item 7e

Item 7e's writeup names the mechanism as ash-side (`evaltree`
stores a `jmp_buf` pointer in `globals+0x38` and does not restore
it).  That is a **symptom read in ash**: a process resumed with a
corrupted register file, running ash, manifests in ash's
`setjmp`/`longjmp` machinery.  The mechanism here is one layer
down and explains why the family is not ash-specific and not
fork-specific: **any blocking syscall is affected.**

This is a hypothesis about the relationship, not a read.  The
mechanism in `context_switch.asm` is read.
