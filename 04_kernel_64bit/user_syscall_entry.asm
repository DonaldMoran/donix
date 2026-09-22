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

    pop r9
    pop r8
    pop r10                         ; user RSP -> r10 (sysret does not read r10)
    pop r11                         ; user RFLAGS
    pop rcx                         ; user RIP

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ; Diagnostic: record what we are about to load into the CPU's
    ; user-mode RIP/RFLAGS/RSP, so the exception handler can dump it
    ; if the sysret goes wrong.
    mov [rel g_last_sysret_rcx], rcx
    mov [rel g_last_sysret_r11], r11
    mov [rel g_last_sysret_rsp], r10

    mov rsp, r10
    o64 sysret

.handle_exit:
    ; Same teardown, but we do not return to user mode: process_exit
    ; never returns. The kernel stack we are on belongs to the exiting
    ; process and will be reused by whichever process gets its slot.
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
