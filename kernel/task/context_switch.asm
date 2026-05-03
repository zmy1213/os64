bits 64

global scheduler_switch_context
global user_mode_enter
global user_mode_resume_kernel
global user_mode_smoke_program_start
global user_mode_smoke_program_end

section .text

; 这一轮的上下文切换先只保存“函数调用约定要求被调用者保留”的寄存器。
; 原因是当前我们切换点都发生在 C++ 函数边界附近：
; - yield
; - 线程退出
; - 调度器第一次切进线程
;
; 所以：
; - caller-saved 寄存器本来就允许被 clobber
; - 真正必须跨线程保住的是 callee-saved + RSP
;
; 入参：
;   RDI = 要把当前 RSP 保存到哪里
;   RSI = 下一条线程恢复时应该使用的 RSP
scheduler_switch_context:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp
    mov rsp, rsi

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; 下面这几个偏移必须和 `kernel/task/user_mode.hpp` 里的
; `UserModeLaunchContext` 完全一致。
%define USER_MODE_KERNEL_RESUME_RSP   0
%define USER_MODE_KERNEL_ROOT_PHYS    8
%define USER_MODE_USER_ROOT_PHYS     16
%define USER_MODE_USER_RIP           24
%define USER_MODE_USER_RSP           32
%define USER_MODE_USER_RFLAGS        40
%define USER_MODE_USER_CS            48
%define USER_MODE_USER_SS            56
%define USER_MODE_RETURN_VALUE       64

%define USER_DATA_SELECTOR_RPL3      0x3B
%define KERNEL_DATA_SELECTOR         0x20
%define SYSCALL_GETCWD_NUMBER        0
%define SYSCALL_OPEN_NUMBER          2
%define SYSCALL_READ_NUMBER          3
%define SYSCALL_CLOSE_NUMBER         4
%define SYSCALL_WRITE_NUMBER         9
%define SYSCALL_EXIT_NUMBER          10
%define USER_MODE_RESULT_ROOT_CWD_OK       0x0001
%define USER_MODE_RESULT_README_PREFIX_OK  0x0002

; 第一次进用户态不是普通 `call`，而是：
; 1. 先把“当前这个内核调用点”需要恢复的 callee-saved 寄存器压栈
; 2. 记住这时的 RSP
; 3. 切到目标 CR3
; 4. 手工伪造 `iretq` 需要的 ring 3 返回帧
;
; 入参：
;   RDI = UserModeLaunchContext*
;
; 返回：
;   不会立刻返回；真正返回发生在用户态 `int 0x80 -> exit` 之后，
;   那时 `user_mode_resume_kernel` 会把现场接回到这次调用的下一条指令。
user_mode_enter:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi + USER_MODE_KERNEL_RESUME_RSP], rsp

    mov rax, [rdi + USER_MODE_USER_ROOT_PHYS]
    mov cr3, rax

    mov rax, [rdi + USER_MODE_USER_SS]
    push rax
    mov rax, [rdi + USER_MODE_USER_RSP]
    push rax
    mov rax, [rdi + USER_MODE_USER_RFLAGS]
    push rax
    mov rax, [rdi + USER_MODE_USER_CS]
    push rax
    mov rax, [rdi + USER_MODE_USER_RIP]
    push rax
    iretq

; 这是“用户态 exit 以后重新接回内核”的专用返回器。
; 它不会回到自己的调用者 `kernel_handle_user_mode_exit()`，
; 而是直接把栈和 CR3 恢复到最初 `user_mode_enter()` 调用点，
; 让上层 C++ 看起来像“user_mode_enter 现在终于返回了”。
;
; 入参：
;   RDI = 当初保存下来的 kernel resume RSP
;   RSI = 原来的 kernel CR3
;   RDX = 想作为 `user_mode_enter()` 返回值带回去的值
user_mode_resume_kernel:
    mov rax, rsi
    mov cr3, rax

    mov ax, KERNEL_DATA_SELECTOR
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov rax, rdx
    mov rsp, rdi

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; 这是第一版 ring 3 smoke program。
; 它现在会做 5 件事：
; 1. 把 DS/ES/FS/GS 换成 ring 3 数据段
; 2. 用 `int 0x80` 打一趟 `write(1, "...")`
; 3. 再用 `getcwd` 读出当前进程自己的 cwd，并把结果写回 stdout
; 4. 用相对路径 `readme.txt` 做一次 open/read/close，证明用户态真的在用“自己的 fd/cwd 视图”
; 5. 最后把 “CS + 自检结果位” 一起交给 `exit`，让内核验证它不是只会打印一行字
user_mode_smoke_program_start:
    xor eax, eax
    mov ax, USER_DATA_SELECTOR_RPL3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    sub rsp, 64
    xor ebx, ebx

    mov eax, SYSCALL_WRITE_NUMBER
    mov edi, 1
    lea rsi, [rel user_mode_smoke_message]
    mov edx, user_mode_smoke_message_end - user_mode_smoke_message
    int 0x80

    mov eax, SYSCALL_GETCWD_NUMBER
    mov rdi, rsp
    mov esi, 64
    int 0x80
    cmp eax, 1
    jne .write_cwd
    cmp byte [rsp], '/'
    jne .write_cwd
    cmp byte [rsp + 1], 0
    jne .write_cwd
    or ebx, USER_MODE_RESULT_ROOT_CWD_OK

.write_cwd:
    mov r12d, eax
    cmp r12d, 0
    jle .try_open_readme

    mov eax, SYSCALL_WRITE_NUMBER
    mov edi, 1
    lea rsi, [rel user_mode_cwd_prefix]
    mov edx, user_mode_cwd_prefix_end - user_mode_cwd_prefix
    int 0x80

    mov eax, SYSCALL_WRITE_NUMBER
    mov edi, 1
    mov rsi, rsp
    mov rdx, r12
    int 0x80

    mov eax, SYSCALL_WRITE_NUMBER
    mov edi, 1
    lea rsi, [rel user_mode_newline]
    mov edx, 1
    int 0x80

.try_open_readme:
    mov eax, SYSCALL_OPEN_NUMBER
    lea rdi, [rel user_mode_readme_path]
    int 0x80
    cmp eax, 0
    jl .exit_user_mode

    mov r13d, eax
    mov eax, SYSCALL_READ_NUMBER
    mov edi, r13d
    mov rsi, rsp
    mov edx, user_mode_expected_readme_prefix_end - user_mode_expected_readme_prefix
    int 0x80
    mov r14d, eax

    cmp r14d, user_mode_expected_readme_prefix_end - user_mode_expected_readme_prefix
    jne .close_readme

    cld
    lea rsi, [rsp]
    lea rdi, [rel user_mode_expected_readme_prefix]
    mov ecx, user_mode_expected_readme_prefix_end - user_mode_expected_readme_prefix
    repe cmpsb
    jne .close_readme

    or ebx, USER_MODE_RESULT_README_PREFIX_OK

.close_readme:
    cmp r14d, 0
    jle .close_readme_fd

    mov eax, SYSCALL_WRITE_NUMBER
    mov edi, 1
    lea rsi, [rel user_mode_readme_prefix]
    mov edx, user_mode_readme_prefix_end - user_mode_readme_prefix
    int 0x80

    mov eax, SYSCALL_WRITE_NUMBER
    mov edi, 1
    mov rsi, rsp
    mov rdx, r14
    int 0x80

    mov eax, SYSCALL_WRITE_NUMBER
    mov edi, 1
    lea rsi, [rel user_mode_newline]
    mov edx, 1
    int 0x80

.close_readme_fd:
    mov eax, SYSCALL_CLOSE_NUMBER
    mov edi, r13d
    int 0x80

.exit_user_mode:
    add rsp, 64

    xor edi, edi
    mov di, cs
    shl ebx, 16
    or edi, ebx
    mov eax, SYSCALL_EXIT_NUMBER
    int 0x80

.hang:
    jmp .hang

user_mode_smoke_message:
    db "user_mode_message=hello from ring3 via int80", 10
user_mode_smoke_message_end:
user_mode_cwd_prefix:
    db "user_mode_cwd="
user_mode_cwd_prefix_end:
user_mode_readme_prefix:
    db "user_mode_readme_prefix="
user_mode_readme_prefix_end:
user_mode_readme_path:
    db "readme.txt", 0
user_mode_expected_readme_prefix:
    db "os64fs readme"
user_mode_expected_readme_prefix_end:
user_mode_newline:
    db 10
user_mode_smoke_program_end:
