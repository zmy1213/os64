bits 64
default rel
global _start
extern main
section .text
_start:
    mov ax, 0x3b
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    ; Initial RSP is 16-byte aligned: argc, argv pointers, terminating null.
    mov rdi, [rsp]
    lea rsi, [rsp + 8]
    xor ebp, ebp
    call main
    mov edi, eax
    mov eax, 10
    int 0x80
.hang:
    jmp .hang
; Ensure every executable exercises a writable, zero-filled second PT_LOAD.
section .bss
align 8
    resq 1
section .note.GNU-stack noalloc noexec nowrite progbits
