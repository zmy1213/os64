; Copied to physical 0x7000 only after all BIOS/bootstrap smoke tests finish.
; 0x7e00 contains the BSP-written mailbox, separate from stage2 BootInfo.
bits 16
section .rodata
align 16
global smp_trampoline_start
global smp_trampoline_end
smp_trampoline_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x6ff0
    lgdt [0x7000 + ap_gdt_pointer - smp_trampoline_start]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp dword 0x08:(0x7000 + ap_protected - smp_trampoline_start)
bits 32
ap_protected:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax
    mov eax, [0x7e00]
    mov cr3, eax
    mov ecx, 0xc0000080
    rdmsr
    or eax, [0x7e04]              ; LME, and NXE iff BSP enabled it.
    wrmsr
    mov eax, cr0
    or eax, 0x80010000            ; paging + supervisor write protection.
    mov cr0, eax
    jmp 0x18:(0x7000 + ap_long - smp_trampoline_start)
bits 64
ap_long:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, [abs 0x7e08]
    and rsp, -16
    xor rbp, rbp
    mov edi, [abs 0x7e18]
    mov rax, [abs 0x7e10]
    call rax
.halt:
    cli
    hlt
    jmp .halt
align 8
ap_gdt:
    dq 0, 0x00cf9a000000ffff, 0x00cf92000000ffff
    dq 0x00af9a000000ffff, 0x00cf92000000ffff
ap_gdt_end:
ap_gdt_pointer:
    dw ap_gdt_end - ap_gdt - 1
    dd 0x7000 + ap_gdt - smp_trampoline_start
smp_trampoline_end:
