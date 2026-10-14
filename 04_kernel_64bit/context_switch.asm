[bits 64]
default rel

section .text
global context_switch

; Correct pcb_t offsets (from process.h). These MUST match the C struct
; layout exactly. Any change to pcb_t that shifts a field used here must
; be reflected below — the assembly has no way to know on its own. The
; _Static_assert block at the bottom of process.c guards these offsets
; at compile time; if the struct layout ever changes, the build fails
; with a message pointing at this file.
;
; Current layout:
;   cr3              = 0x30
;   entry_point      = 0x38
;   user_stack_top   = 0x70
;   r15 = 0x98, r14 = 0xA0, r13 = 0xA8, r12 = 0xB0
;   r11 = 0xB8, r10 = 0xC0, r9  = 0xC8, r8  = 0xD0
;   rbp = 0xD8, rdi = 0xE0, rsi = 0xE8, rdx = 0xF0
;   rcx = 0xF8, rbx = 0x100, rax = 0x108
;   rsp = 0x110, rip = 0x118
;   block_kind       = 0x158

%define KERNEL_BASE 0xFFFFFFFF80000000

; void context_switch(pcb_t* prev, pcb_t* next)
; void context_switch_no_save(pcb_t* prev, pcb_t* next)
;
; A hand-off entry point for a caller that is NOT coming back --
; process_exit's tail.  It shares context_switch's prologue and
; resume side but skips the entire save block, so no resume frame
; is built and no pcb field of `prev` is written.
;
; WHY THIS EXISTS.  context_switch's save paths build a resume
; frame whose RIP is either [rsp + 48] (the caller's return
; address -- the ring0_save path) or prev->rip (the user-preempted
; path).  Both are correct for a caller that expects to be resumed
; at the instruction after `call context_switch`.  process_exit
; does NOT: it is __attribute__((noreturn)), and the instruction
; after its `call context_switch` is the `while(1) hlt` guard the
; compiler emits as the noreturn fallback.  Saving a resume frame
; there produces a well-formed kernel frame whose RIP is the hlt
; guard.  The frame is not malformed; the resume point is wrong.
; If such a process is ever resumed -- by a rescan, a self-switch,
; or a slot reuse that did not overwrite the frame -- it resumes
; at the guard and stops.
;
; WHAT IT DOES NOT DO.  It does not write prev->rsp, prev->cr3, or
; the prev GPR slots.  It does not consult prev at all (rdi is
; ignored).  The exiting process is about to have its PCB reclaimed
; or reaped; nothing reads those fields again.
;
; WHY IT IS SAFE.  The resume side (context_switch_resume) reads
; ONLY `next`: cr3 at [r12 + 0x30], frame base at [r12 + 0x110],
; and the frame at that base.  It never reads prev.  So skipping
; the save side leaves next's resume byte-identical to
; context_switch's, and prev is simply not written.
;
; WHY THIS IS NOT FRAMING (c).  (c) added a conditional jump to
; the resume label from INSIDE ring0_save, so it reached the
; resume label with a different stack depth and register state
; than the normal path -- the prologue's six pushes plus the save
; block's own pushes, with r15 live.  (c) made pipe7e worse.  This
; entry point reaches the resume side by the SAME path
; `context_switch(NULL, next)` already takes: the prologue's six
; pushes, `mov r12, rsi`, then the jump to the resume label.  That
; path already runs today -- process_init's first dispatch of idle
; has prev == NULL and goes through it -- so it is exercised code,
; not a new depth.  r12 (next) is the only register the resume
; side needs, and the prologue sets it.
;
; WHAT THE PROLOGUE'S SIX PUSHES ARE FOR HERE.  On the switch-to-
; next path they are ABANDONED, not balanced: the pop_frame path
; pops the 15-GPR resume frame and iretqs, and the six prologue
; pushes are left on the stack of the process being left behind.
; They are balanced only on the restore_and_return path
; (next == NULL), which is what `context_switch(NULL, next)` uses
; today.  Pushing them here costs six stores and no correctness;
; it keeps this entry point reaching the resume side by the
; identical path.
;
; Callers: process_exit (scheduler.c).  Do not call this from a
; site that expects to be resumed -- use context_switch.
global context_switch_no_save
context_switch_no_save:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov r12, rsi            ; r12 = next
    ; rdi (prev) is deliberately ignored.
    jmp context_switch_resume

context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, rsi            ; r12 = next

    test rdi, rdi
    jz context_switch_resume

    ; r15 = prev (scratch; real r15 value is reloaded from PCB slot).
    mov r15, rdi

    ; Decide which save path: ring-0 or user.
    ; Primary signal: prev->block_kind. A blocking syscall sets this
    ; before yielding, because prev->rip still holds the process's last
    ; user-mode RIP at that point and cannot distinguish "blocked in
    ; kernel" from "preempted from user mode".
    ; Secondary signal (block_kind == 0): prev->rip >= KERNEL_BASE.
    mov rax, [r15 + 0x158]              ; prev->block_kind
    test rax, rax
    jnz .ring0_save

    mov rax, [r15 + 0x118]              ; prev->rip
    cmp rax, KERNEL_BASE
    jae .ring0_save

    ; --- USER-PREEMPTED SAVE PATH ---
    ;
    ; RSP here is the USER stack pointer, not pcb->rsp.  pcb->rsp
    ; (0x110) is the KERNEL resume-frame base -- both save paths
    ; write it that way, and reading it here delivered a kernel
    ; address to a user-mode resume.  pcb->user_rsp (0x610) is
    ; written by the user-mode preemption paths in interrupts.c.
    push qword 0x2B                     ; SS       [0x98]
    push qword [r15 + 0x610]            ; RSP      [0x90]  <- pcb->user_rsp
    push qword 0x202                    ; RFLAGS   [0x88]
    push qword 0x33                     ; CS       [0x80]
    mov rax, [r15 + 0x118]              ; prev->rip
    test rax, rax
    jnz .have_rip
    mov rax, [r15 + 0x38]               ; fall back to entry_point
.have_rip:
    push rax                            ; RIP      [0x78]

    push qword [r15 + 0x108]            ; rax      [0x70]
    push qword [r15 + 0x100]            ; rbx      [0x68]
    push qword [r15 + 0x0F8]            ; rcx      [0x60]
    push qword [r15 + 0x0F0]            ; rdx      [0x58]
    push qword [r15 + 0x0E8]            ; rsi      [0x50]
    push qword [r15 + 0x0E0]            ; rdi      [0x48]
    push qword [r15 + 0x0D8]            ; rbp      [0x40]
    push qword [r15 + 0x0D0]            ; r8       [0x38]
    push qword [r15 + 0x0C8]            ; r9       [0x30]
    push qword [r15 + 0x0C0]            ; r10      [0x28]
    push qword [r15 + 0x0B8]            ; r11      [0x20]
    push qword [r15 + 0x0B0]            ; r12      [0x18]
    push qword [r15 + 0x0A8]            ; r13      [0x10]
    push qword [r15 + 0x0A0]            ; r14      [0x08]
    push qword [r15 + 0x098]            ; r15      [0x00]

    mov [r15 + 0x110], rsp              ; prev->rsp = frame base
    mov rax, cr3
    mov [r15 + 0x30], rax
    jmp context_switch_resume

    ; --- RING-0 SAVE PATH ---
.ring0_save:
    ; Persist the live callee-saved registers into the PCB.
    mov rax, [rsp + 0x00]
    mov [r15 + 0x098], rax              ; r15
    mov rax, [rsp + 0x08]
    mov [r15 + 0x0A0], rax              ; r14
    mov rax, [rsp + 0x10]
    mov [r15 + 0x0A8], rax              ; r13
    mov rax, [rsp + 0x18]
    mov [r15 + 0x0B0], rax              ; r12
    mov rax, [rsp + 0x20]
    mov [r15 + 0x100], rax              ; rbx
    mov rax, [rsp + 0x28]
    mov [r15 + 0x0D8], rax              ; rbp

    ; Now build the resume frame from the (now up-to-date) PCB values.
    mov rax, [rsp + 48]                 ; caller's return address = kernel RIP
    push rax                            ; temp0 (RIP)  @ [rsp+0]
    lea rax, [rsp + 64]                 ; caller's RSP = orig_rsp + 56
    push rax                            ; temp1 (RSP)  @ [rsp+0], temp0 @ [rsp+8]

    push qword 0x20                     ; SS       [0x98]
    push qword [rsp + 8]                ; RSP      [0x90]  <- temp1
    push qword 0x202                    ; RFLAGS   [0x88]
    push qword 0x18                     ; CS       [0x80]
    push qword [rsp + 40]               ; RIP      [0x78]  <- temp0

    push qword [r15 + 0x108]            ; rax      [0x70]
    push qword [r15 + 0x100]            ; rbx      [0x68]
    push qword [r15 + 0x0F8]            ; rcx      [0x60]
    push qword [r15 + 0x0F0]            ; rdx      [0x58]
    push qword [r15 + 0x0E8]            ; rsi      [0x50]
    push qword [r15 + 0x0E0]            ; rdi      [0x48]
    push qword [r15 + 0x0D8]            ; rbp      [0x40]
    push qword [r15 + 0x0D0]            ; r8       [0x38]
    push qword [r15 + 0x0C8]            ; r9       [0x30]
    push qword [r15 + 0x0C0]            ; r10      [0x28]
    push qword [r15 + 0x0B8]            ; r11      [0x20]
    push qword [r15 + 0x0B0]            ; r12      [0x18]
    push qword [r15 + 0x0A8]            ; r13      [0x10]
    push qword [r15 + 0x0A0]            ; r14      [0x08]
    push qword [r15 + 0x098]            ; r15      [0x00]

    mov [r15 + 0x110], rsp              ; prev->rsp = frame base
    mov rax, cr3
    mov [r15 + 0x30], rax
    ; fall through to context_switch_resume

context_switch_resume:
    test r12, r12
    jz .restore_and_return

    ; Load the incoming process's address space.
    mov rax, [r12 + 0x30]
    mov cr3, rax

    ; Restore from the saved frame. This unified path replaces the
    ; previous two-branch (user / kernel) dispatch.
    ;
    ; The old code chose the resume path by comparing next->entry_point
    ; against KERNEL_BASE. That works for the *first* dispatch of a
    ; process, because process_create builds the frame with
    ; entry_point in the RIP slot and CS=0x33 or 0x18. But it is wrong
    ; for resuming a process that was preempted or blocked in kernel
    ; mode: such a process has entry_point=0x8000000000 (a user
    ; address) but its saved frame has CS=0x18 and a kernel RIP. The
    ; old code would take the user branch and rebuild the frame from
    ; entry_point / user_stack_top, silently restarting the process
    ; at its _start as if it had just been created. That is what
    ; produced the "shell restarts after waitpid returns" symptom.
    ;
    ; The fix is to trust the saved frame verbatim. process_create and
    ; the two save paths above all build frames in the same 20-slot
    ; layout: [0..14] GPRs, [15] RIP, [16] CS, [17] RFLAGS, [18] RSP,
    ; [19] SS. The CS field distinguishes kernel (0x18) from user
    ; (0x33) resumes. Only the validation of RIP differs by CS, and
    ; only because a mismatch there is a strong indicator of frame
    ; corruption.
    mov rsp, [r12 + 0x110]

    mov rbx, [rsp + 0x80]               ; saved CS
    cmp rbx, 0x18
    je .frame_cs_kernel
    cmp rbx, 0x33
    je .frame_cs_user

    ; Unknown CS: emit 'C' and halt.
    mov dx, 0x3F8
    mov al, 'C'
    out dx, al
    mov al, 0x0A
    out dx, al
    hlt

.frame_cs_kernel:
    mov rbx, [rsp + 0x78]               ; saved RIP
    mov rcx, KERNEL_BASE
    cmp rbx, rcx
    jae .pop_frame
    ; Kernel frame with user RIP: emit 'K' and halt.
    mov dx, 0x3F8
    mov al, 'K'
    out dx, al
    mov al, 0x0A
    out dx, al
    hlt

.frame_cs_user:
    mov rbx, [rsp + 0x78]               ; saved RIP
    mov rcx, KERNEL_BASE
    cmp rbx, rcx
    jb .pop_frame
    ; User frame with kernel RIP: emit 'U' and halt.
    mov dx, 0x3F8
    mov al, 'U'
    out dx, al
    mov al, 0x0A
    out dx, al
    hlt

.pop_frame:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq

.restore_and_return:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
