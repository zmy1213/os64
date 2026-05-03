bits 64
default rel

global _start

; 用户态数据段选择子，RPL=3。
%define USER_DATA_SELECTOR_RPL3 0x3B

; syscall 编号必须和内核里的 `SyscallNumber` 保持一致。
%define SYSCALL_WRITE_NUMBER 9
%define SYSCALL_EXIT_NUMBER 10
%define USER_ELF_DATA_MAGIC 0x31464C45

; 这份 ELF 程序自己的成功标志位。
; 内核会据此区分：
; - 上一轮 flat binary 文件程序
; - 这一轮真正的 ELF 文件程序
%define USER_MODE_RESULT_ELF_PROGRAM_OK 0x0040

section .text

_start:
    ; 先把常用数据段寄存器都切到用户态数据段。
    xor eax, eax
    mov ax, USER_DATA_SELECTOR_RPL3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; 先验证第二个 PT_LOAD 段里的已初始化数据真的被装进来了。
    cmp dword [rel user_elf_program_data_magic], USER_ELF_DATA_MAGIC
    jne .fail

    ; 再验证 .bss 区域确实被零填充了，而不是残留脏数据。
    mov rax, [rel user_elf_program_zero_qword]
    test rax, rax
    jne .fail

    ; write(1, "user_elf_program=hello from elf\n")
    mov eax, SYSCALL_WRITE_NUMBER
    mov edi, 1
    lea rsi, [rel user_elf_program_message]
    mov edx, user_elf_program_message_end - user_elf_program_message
    int 0x80

    ; exit((USER_MODE_RESULT_ELF_PROGRAM_OK << 16) | cs)
    ; 低 16 位保留真正用户态 CS，高位带回 ELF 程序自己的成功标志。
    mov edi, USER_MODE_RESULT_ELF_PROGRAM_OK << 16
    mov di, cs
    mov eax, SYSCALL_EXIT_NUMBER
    int 0x80

.fail:
    ; 如果第二段数据或 .bss 不对，就故意不带成功标志退出。
    xor edi, edi
    mov di, cs
    mov eax, SYSCALL_EXIT_NUMBER
    int 0x80

.hang:
    ; 理论上 exit 不会返回；如果返回了，停在这里方便排查。
    jmp .hang

section .data

user_elf_program_data_magic:
    dd USER_ELF_DATA_MAGIC

user_elf_program_message:
    db "user_elf_program=hello from elf", 10
user_elf_program_message_end:

section .bss

user_elf_program_zero_qword:
    resq 1
