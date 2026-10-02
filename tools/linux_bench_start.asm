bits 64
global _start
extern main
section .text
_start:
    xor rbp, rbp
    mov rdi, [rsp]
    lea rsi, [rsp+8]
    and rsp, -16
    call main
    mov edi, eax
    mov eax, 60
    syscall
    ud2
