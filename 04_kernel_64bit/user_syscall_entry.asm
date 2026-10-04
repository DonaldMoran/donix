[bits 64]
default rel

section .text
global user_syscall_entry
extern syscall_dispatch
extern process_exit
extern serial_print
extern serial_print_hex
extern g_syscall_stack_top

; ---------------------------------------------------------------------------
; Syscall entry from usermode
;
; On entry (set by the CPU):
;   RCX = user RIP (return address)
;   R11 = user RFLAGS
;   RSP = user stack pointer (UNCHANGED by the syscall instruction)
;   CS  = 0x18, SS = 0x20 (from STAR)
; ---------------------------------------------------------------------------

section .bss
align 8
g_user_rsp_save: resq 1

section .data
align 8
; Last values passed to sysret. Diagnostic only: the exception handler
; reads these if a fault lands at RIP < 0x1000 in user mode, which is
; the signature of a corrupted sysret target.
global g_last_sysret_rcx
global g_last_sysret_r11
global g_last_sysret_rsp
g_last_sysret_rcx: dq 0
g_last_sysret_r11: dq 0
g_last_sysret_rsp: dq 0

section .text

user_syscall_entry:
    cli

    ; Save user RSP. It is pushed later as part of the frame so the
    ; epilogue can recover it symmetrically.
    mov [rel g_user_rsp_save], rsp

    ; Switch to the per-process kernel stack.
    mov rsp, [rel g_syscall_stack_top]
    test rsp, rsp
    jz .no_stack

    sti

    ; Save original callee-saved registers FIRST.
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    ; Save the syscall return RIP / RFLAGS that the CPU put in RCX / R11.
    push rcx                        ; user RIP
    push r11                        ; user RFLAGS

    ; Push the saved user RSP slot (the value lives in the global).
    push qword [rel g_user_rsp_save]

    ; Save r8/r9 so we can freely use them during arg shuffling.
    push r8
    push r9

    ; Save rdi/rsi/rdx/r10 so we can restore them on the parent's return
    ; path AND give a fork child the parent's pre-syscall values.  The
    ; C ABI marks these caller-saved, but the syscall ABI is stricter:
    ; only rcx and r11 are architecturally clobbered.  Compilers do rely
    ; on the other GPRs surviving, and musl's fork wrapper does exactly
    ; that — it caches the TLS base in rdx before the fork syscall and
    ; writes through it after the child resumes:
    ;
    ;     mov %fs:0x0, %rdx
    ;     syscall
    ;     movq $0x0, 0x98(%rdx)
    ;
    ; Without saving rdx, the parent faults at CR2 = 0x98 after the
    ; syscall returns (rdx was overwritten with arg1, which is 0 for
    ; fork).  Without giving the child the parent's rdx, the child
    ; faults at the same instruction.  Same argument for rsi, rdi, r10.
    push rdi
    push rsi
    push rdx
    push r10

    ; Arg5 -> 7th C argument, on stack.
    push r9
    mov rbx, rax                    ; save syscall number
    mov r9, r8                      ; arg4 -> r9
    mov r8, r10                     ; arg3 -> r8
    mov rcx, rdx                    ; arg2 -> rcx
    mov rdx, rsi                    ; arg1 -> rdx
    mov rsi, rdi                    ; arg0 -> rsi
    mov rdi, rax                    ; num  -> rdi

    call syscall_dispatch
    
    add rsp, 8                      ; discard stacked arg5

    cmp rbx, 60
    je .handle_exit

    ; Restore the extra saved GPRs.  The syscall ABI clobbers only
    ; rax, rcx, r11; every other GPR must survive a syscall.  The
    ; previous version discarded the saved r10 and reused r10 as a
    ; scratch for the user RSP, which corrupted any value the caller
    ; had live in r10 across the syscall.  musl's __stdio_write does
    ; exactly that, which is why printf emitted garbage iov[1] values.
    pop r10                         ; restore user r10
    pop rdx                         ; restore user rdx
    pop rsi                         ; restore user rsi
    pop rdi                         ; restore user rdi

    pop r9
    pop r8

    ; At this point rsp points at the saved user RSP slot.  The
    ; frame above it (higher addresses) is:
    ;   [rsp + 0]  = user RSP
    ;   [rsp + 8]  = user RFLAGS
    ;   [rsp + 16] = user RIP
    ;   [rsp + 24] = r15
    ;   [rsp + 32] = r14
    ;   [rsp + 40] = r13
    ;   [rsp + 48] = r12
    ;   [rsp + 56] = rbp
    ;   [rsp + 64] = rbx
    ;
    ; Load RFLAGS and RIP first, then pop the callee-saved
    ; registers, then load the user RSP LAST so no GPR has to hold
    ; it.  rax is left untouched: it carries the syscall return
    ; value that sysret hands back to the user.
    mov r11, [rsp + 8]              ; user RFLAGS -> r11
    mov rcx, [rsp + 16]             ; user RIP    -> rcx

    add rsp, 24                     ; skip user RSP, RFLAGS, RIP slots
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ; rsp is now 72 bytes past the user RSP slot: 24 (skipped) +
    ; 6*8 (the six pops).  Load the user RSP directly into rsp,
    ; with no intermediate GPR.
    mov rsp, [rsp - 72]

    ; Diagnostic: record what we are about to sysret to.  A fault that
    ; lands at RIP < 0x1000 in user mode is the signature of a
    ; corrupted sysret target; the exception handlers print these
    ; globals for that case.  rcx and r11 are set, rsp is not: at this
    ; point every GPR holds a user value and there is no scratch
    ; register free to stage the read.
    mov [rel g_last_sysret_rcx], rcx
    mov [rel g_last_sysret_r11], r11

    o64 sysret

.handle_exit:
    ; Same teardown, but we do not return to user mode: process_exit
    ; never returns. The kernel stack we are on belongs to the exiting
    ; process and will be reused by whichever process gets its slot.
    pop r10                         ; discard saved r10
    pop rdx                         ; discard saved rdx
    pop rsi                         ; discard saved rsi
    pop rdi                         ; discard saved rdi
    pop r9
    pop r8
    add rsp, 8                      ; discard user RSP slot
    add rsp, 16                     ; discard r11, rcx
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    push rax
    mov rdi, exit_msg
    call serial_print
    pop rax
    push rax
    call serial_print_hex
    mov rdi, newline
    call serial_print
    pop rax

    mov rdi, rax
    jmp process_exit

.no_stack:
    ; g_syscall_stack_top was zero. This should never happen once
    ; process_init has run. Emit a diagnostic and halt rather than
    ; corrupting kernel state.
    mov rsp, [rel g_user_rsp_save]
    mov rdi, no_stack_msg
    call serial_print
    cli
.hang:
    hlt
    jmp .hang

section .data
exit_msg: db "SYS_EXIT: status=", 0
newline: db 0x0A, 0x0D, 0
no_stack_msg: db "SYSCALL: g_syscall_stack_top is NULL!", 0x0A, 0x0D, 0
