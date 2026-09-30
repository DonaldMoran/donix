[bits 16]
[org 0x10000]
default abs   ; suppress NASM warning about implicit DEFAULT ABS

; ============================================
; Constants
; ============================================
BOOTINFO_MAGIC equ 0x4F534F444E4F53
BOOTINFO_VERSION equ 1

; ============================================
; Kernel staging and destination layout
; --------------------------------------------
;   Staging A : 0x80000 .. 0x9FFFF  (128 KB, PASS 1 + PASS 2)
;   Staging B : 0x20000 .. 0x7FFFF  (384 KB, PASS 3 .. PASS 7,
;                                    of which the first 320 KB is used)
;   Destination: 0x100000 .. (128 KB from A, then 320 KB from B)
;                for a total of 448 KB.
;
;   Low memory layout, from low to high:
;     0x00000 .. 0x004FF   IVT + BDA
;     0x00500 .. 0x07BFF   conventional free
;     0x07C00 .. 0x07DFF   boot sector (stage1)
;     0x07E00 .. 0x1FFFF   conventional free
;     0x20000 .. 0x7FFFF   staging B (384 KB)  <-- PASS 3..7
;     0x80000 .. 0x9FFFF   staging A (128 KB)  <-- PASS 1..2
;     0xA0000 .. 0xBFFFF   VGA memory (unusable)
;     0xC0000 .. 0xFFFFF   BIOS ROM (unusable)
;     0x100000 ..          kernel destination + free RAM
;
;   Why two staging areas instead of one: the only low-memory window
;   large enough for a single staging area is split by the VGA hole at
;   0xA0000. So the kernel is read in two rounds:
;     - 128 KB from staging A (PASS 1 + 2)
;     - 320 KB from staging B (PASS 3..7)
;   and long_mode_entry performs both rep movsq copies back-to-back.
;
;   Historical note: an early version staged PASS 3 at 0xA0000, which
;   is VGA graphics memory. Writes there are unreliable in QEMU/BIOS,
;   so PASS 3's bytes were frequently 0xFF and corrupted the kernel
;   image. All staging is now below the VGA hole.
;
; Kernel source: LBA 128 on the disk. Each PASS reads 128 sectors
; (64 KB) starting at LBA 128, 256, 384, 512, 640, 768, 896.
; ============================================
KERNEL_SECTORS_PER_PASS equ 128
KERNEL_PASSES           equ 7
KERNEL_TOTAL_SECTORS    equ KERNEL_SECTORS_PER_PASS * KERNEL_PASSES

; Total bytes actually copied = 448 KB
;   128 KB from staging A (PASS 1+2) + 320 KB from staging B (PASS 3..7).
; Staging B is 384 KB (0x20000..0x7FFFF), so the final 64 KB of that
; window is unused. The read count is still 7 * 128 = 896 sectors, which
; matches KERNEL_TOTAL_BYTES.
KERNEL_TOTAL_BYTES      equ 448 * 1024
KERNEL_TOTAL_QWORDS     equ KERNEL_TOTAL_BYTES / 8

; First copy: 128 KB from 0x80000 -> 0x100000
KERNEL_COPY1_QWORDS     equ (128 * 1024) / 8                ; 16384
; Second copy: 320 KB from 0x20000 -> 0x120000 (fills total 448 KB)
KERNEL_COPY2_QWORDS     equ (320 * 1024) / 8                ; 40960

; ============================================
; DAP entries
; ============================================
dap_kernel:
    db 16
    db 0
    dw 128            ; Read exactly 128 sectors (64 KB) per pass
    dw 0x0000
.segment:
    dw 0x8000         ; Dynamically modified below
.lba:
    dd 128            ; Kernel starts at LBA 128
    dd 0

; ============================================
; Real Mode Code
; ============================================
start:
    cli
    mov ax, 0x1000
    mov ds, ax
    mov es, ax

    ; Real-mode stack below pt_low.
    mov ax, 0x0000
    mov ss, ax
    mov sp, 0x7C00

    ; ----------------------------------------------------
    ; Set a VBE linear-framebuffer mode.
    ;
    ; Was: VGA text mode 03h (int 0x10, ax=0x0003).
    ; Now: VBE mode 0x118 = 1024x768x32bpp, with the
    ;      0x4000 "use linear framebuffer" bit set, so the
    ;      mode info block reports a linear framebuffer
    ;      address we can write pixels to directly.
    ;
    ; This must happen HERE, in real mode, before the switch
    ; to protected/long mode: VBE is a real-mode BIOS service.
    ; Once stage2 has gone to long mode there is no way to call
    ; it without a real-mode trampoline.
    ;
    ; On failure (AX != 0x4F after the set), fall back to text
    ; mode 03h and leave the framebuffer fields in `bootinfo`
    ; zero.  The kernel checks framebuffer_addr == 0 and stays
    ; on VGA text.  A VBE failure therefore degrades to the
    ; previous behaviour rather than a black screen.
    ;
    ; The BIOS can clobber ds/es/si/di across an int 0x10, so
    ; ds and es are saved and restored around the whole block.
    ; ----------------------------------------------------
    push ds
    push es

    ; --- Set VBE mode 0x118 with linear framebuffer.
    ;     AX = 0x4F02, BX = mode | 0x4000.  Success: AX = 0x004F. ---
    mov ax, 0x4F02
    mov bx, 0x4118          ; mode 0x118 | 0x4000 (linear fb)
    int 0x10
    cmp ax, 0x004F          ; VBE success returns AX = 0x004F
    jne .vbe_failed

    ; --- Get mode info for 0x118 into the buffer.  ES:DI must
    ;     point at a 256-byte region; CX = mode number. ---
    mov ax, 0x1000
    mov es, ax
    mov di, vbe_mode_info - $$
    mov ax, 0x4F01
    mov cx, 0x0118
    int 0x10
    cmp ax, 0x004F
    jne .vbe_failed

    ; --- Mode attributes: bit 7 must be set (linear framebuffer
    ;     supported).  If not, this mode is not usable as a
    ;     linear framebuffer. ---
    mov al, byte [vbe_mode_info + 0x00]
    test al, 0x80
    jz .vbe_failed

    ; --- Copy the fields we need into the `bootinfo` block:
    ;       +0x40  framebuffer_addr    (qword, physical)
    ;       +0x48  framebuffer_width   (dword, pixels)
    ;       +0x4C  framebuffer_height  (dword, pixels)
    ;       +0x50  framebuffer_pitch   (dword, bytes/scanline)
    ;       +0x54  framebuffer_bpp     (dword, bits/pixel)
    ;     These slots are already present in the bootinfo
    ;     block (see the layout below); they start zeroed. ---
    mov ax, 0x1000
    mov ds, ax

    ; framebuffer_addr = dword [mode_info + 0x28], extended to
    ; a qword.  VBE 3.0 also carries a high dword at +0x2A, but
    ; for a mode whose framebuffer is below 4 GB (QEMU's is),
    ; the low dword is enough and the high dword is zero.
    mov eax, dword [vbe_mode_info + 0x28]
    mov dword [bootinfo + 0x40], eax
    mov dword [bootinfo + 0x44], 0

    ; framebuffer_pitch = word [mode_info + 0x10]
    movzx eax, word [vbe_mode_info + 0x10]
    mov dword [bootinfo + 0x50], eax

    ; framebuffer_width = word [mode_info + 0x12]
    movzx eax, word [vbe_mode_info + 0x12]
    mov dword [bootinfo + 0x48], eax

    ; framebuffer_height = word [mode_info + 0x14]
    movzx eax, word [vbe_mode_info + 0x14]
    mov dword [bootinfo + 0x4C], eax

    ; framebuffer_bpp = byte [mode_info + 0x19]
    movzx eax, byte [vbe_mode_info + 0x19]
    mov dword [bootinfo + 0x54], eax

    pop es
    pop ds
    jmp .vbe_done

.vbe_failed:
    ; Fall back to VGA text mode 03h.  The bootinfo framebuffer
    ; fields stay zero, and the kernel will keep using the VGA
    ; text console.
    mov ax, 0x0003
    int 0x10
    pop es
    pop ds

.vbe_done:

    in  al, 0x92
    or  al, 00000010b
    out 0x92, al

    ; ----------------------------------------------------
    ; PASS 1: 128 sectors (64 KB) -> 0x8000:0000
    ; ----------------------------------------------------
    mov si, dap_kernel
    mov dl, 0x80
    mov ah, 0x42
    int 0x13
    jc disk_error

    add word [dap_kernel.segment], 0x1000   ; -> 0x9000
    add dword [dap_kernel.lba], 128         ; -> 256

    ; ----------------------------------------------------
    ; PASS 2: 128 sectors (64 KB) -> 0x9000:0000
    ; ----------------------------------------------------
    mov si, dap_kernel
    mov dl, 0x80
    mov ah, 0x42
    int 0x13
    jc disk_error

    ; ----------------------------------------------------
    ; PASS 3: 128 sectors (64 KB) -> 0x2000:0000
    ;   Reset segment to 0x2000 (NOT +0x1000) to land at 0x20000.
    ; ----------------------------------------------------
    mov word [dap_kernel.segment], 0x2000
    add dword [dap_kernel.lba], 128         ; -> 384

    mov si, dap_kernel
    mov dl, 0x80
    mov ah, 0x42
    int 0x13
    jc disk_error

    ; ----------------------------------------------------
    ; PASS 4: 128 sectors (64 KB) -> 0x3000:0000
    ; ----------------------------------------------------
    add word [dap_kernel.segment], 0x1000   ; -> 0x3000
    add dword [dap_kernel.lba], 128         ; -> 512

    mov si, dap_kernel
    mov dl, 0x80
    mov ah, 0x42
    int 0x13
    jc disk_error

    ; ----------------------------------------------------
    ; PASS 5: 128 sectors (64 KB) -> 0x4000:0000
    ; ----------------------------------------------------
    add word [dap_kernel.segment], 0x1000   ; -> 0x4000
    add dword [dap_kernel.lba], 128         ; -> 640

    mov si, dap_kernel
    mov dl, 0x80
    mov ah, 0x42
    int 0x13
    jc disk_error

    ; ----------------------------------------------------
    ; PASS 6: 128 sectors (64 KB) -> 0x5000:0000
    ; ----------------------------------------------------
    add word [dap_kernel.segment], 0x1000   ; -> 0x5000
    add dword [dap_kernel.lba], 128         ; -> 768

    mov si, dap_kernel
    mov dl, 0x80
    mov ah, 0x42
    int 0x13
    jc disk_error

    ; ----------------------------------------------------
    ; PASS 7: 128 sectors (64 KB) -> 0x6000:0000
    ; ----------------------------------------------------
    add word [dap_kernel.segment], 0x1000   ; -> 0x6000
    add dword [dap_kernel.lba], 128         ; -> 896

    mov si, dap_kernel
    mov dl, 0x80
    mov ah, 0x42
    int 0x13
    jc disk_error

    ; Memory map
    xor ebx, ebx
    mov di, e820_buffer
    mov dword [e820_count], 0

e820_loop:
    mov eax, 0xE820
    mov ecx, 24
    mov edx, 0x534D4150
    int 0x15
    jc e820_done
    cmp eax, 0x534D4150
    jne e820_done
    add di, 24
    inc word [e820_count]
    test ebx, ebx
    jnz e820_loop

e820_done:
    lgdt [gdt_descriptor]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp dword 0x08:pm_entry

disk_error:
    mov ax, 0xB800
    mov es, ax
    xor di, di
    mov byte [es:di], 'E'
    inc di
    mov byte [es:di], 0x0C
    hlt
    jmp disk_error

; ============================================
; 32-bit Protected Mode
; ============================================
[bits 32]
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
;   mov esp, 0x90000
    mov esp, 0x1FFF0
    
    mov eax, cr4
    or  eax, 1 << 5
    mov cr4, eax

    mov eax, pml4
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or  eax, 1 << 8
    wrmsr

    mov eax, cr0
    or  eax, 0x80000000
    mov cr0, eax

    push dword 0x18
    push dword long_mode_entry
    retf

; ============================================
; 64-bit Long Mode
; ============================================
[bits 64]
long_mode_entry:
    mov rsp, 0x80000

    ; Fill BootInfo
    mov rbx, bootinfo
    mov qword [rbx + 0x10], e820_buffer
    movzx rax, word [e820_count]
    mov qword [rbx + 0x18], rax

    ; ============================================
    ; Copy kernel:
    ;   - 128 KB from staging A (0x80000) -> 0x100000
    ;   - 320 KB from staging B (0x20000) -> 0x120000
    ; ============================================
    mov rsi, 0x00080000
    mov rdi, 0x00100000
    mov rcx, KERNEL_COPY1_QWORDS
    rep movsq

    mov rsi, 0x00020000
    mov rdi, 0x00120000
    mov rcx, KERNEL_COPY2_QWORDS
    rep movsq

    ; Jump to kernel
    mov rdi, bootinfo
    mov rax, 0xFFFFFFFF80100000
    jmp rax

; ============================================
; GDT
; ============================================
gdt_start:
    dq 0x0000000000000000
    db 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00
    db 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00
    db 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xAF, 0x00
    db 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0xAF, 0x00
    db 0xFF, 0xFF, 0x00, 0x00, 0x00, 0xFA, 0xAF, 0x00
    db 0xFF, 0xFF, 0x00, 0x00, 0x00, 0xF2, 0xAF, 0x00
    dq 0x0000000000000000
    dq 0x0000000000000000
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dq gdt_start

; ============================================
; Page Tables
; ============================================
align 4096
pml4:
    dq pdpt_identity + 3
    times 255 dq 0
    dq pdpt_hhdm + 3
    times 253 dq 0
    dq pml4 + 0x003
    dq pdpt_higher + 3

align 4096
pdpt_identity:
    dq pd + 3
    times 511 dq 0

align 4096
pdpt_hhdm:
    dq pd_hhdm + 3
    times 511 dq 0

align 4096
pdpt_higher:
    times 509 dq 0
    dq pml4 + 0x003
    dq pd + 3
    dq 0

align 4096
pd:
    dq pt_low + 3
    %assign i 1
    %rep 511
        dq (i * 0x200000) + 0x83
        %assign i i+1
    %endrep

align 4096
pd_hhdm:
    dq pt_low + 3
    %assign i 1
    %rep 511
        dq (i * 0x200000) + 0x83
        %assign i i+1
    %endrep

align 4096
pt_low:
    %assign i 0
    %rep 512
        dq (i * 0x1000) + 0x03
        %assign i i+1
    %endrep

; ============================================
; BootInfo Structure
; ============================================
align 16
bootinfo:
    dq BOOTINFO_MAGIC
    dq BOOTINFO_VERSION
    dq e820_buffer
    dq 0
    dq 0x00100000
    dq 0x00100000 + KERNEL_TOTAL_BYTES
    dq pml4
    dq 0xFFFFFF7FBFDFE000
    dq 0
    dd 0
    dd 0
    dd 0
    dd 0
    dq 0x80
    dq 0
    dq 0
    dq 0
    dq 0
    dq 0
    times 6 dq 0

; ============================================
; Data Structures
; ============================================
e820_buffer:
    times 64*24 db 0

e820_count:
    dw 0
    dd 0

; VBE mode-info block.  Filled by the int 0x10 / AX=0x4F01 call in
; `start`.  Must be 256 bytes (VBE spec) and should be aligned, though
; alignment is not strictly required — VBE writes dwords/words/bytes
; into it and 16-byte alignment is conventional.  Only the fields the
; boot code reads are documented here:
;
;   0x00  mode attributes    (bit 7 = linear framebuffer supported)
;   0x10  bytes per scanline (word)
;   0x12  X resolution       (word)
;   0x14  Y resolution       (word)
;   0x19  bits per pixel     (byte)
;   0x28  physical address of linear framebuffer (dword)
align 16
vbe_mode_info:
    times 256 db 0
