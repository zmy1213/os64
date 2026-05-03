bits 64

; 这一份不是 ELF，可执行体就是“原始 flat binary 字节流”。
; 内核稍后会把整个文件原样读进 1 页用户代码页里，
; 然后把 RIP 直接指到这份代码开头开始执行。

; ring 3 数据段选择子。
; 低两位 RPL=3，表示用户态权限级。
%define USER_DATA_SELECTOR_RPL3 0x3B

; 这几个编号必须和 `kernel/syscall/syscall.hpp` 保持一致。
; 这里先只用到最小的 `write` 和 `exit` 两个调用。
%define SYSCALL_WRITE_NUMBER 9
%define SYSCALL_EXIT_NUMBER 10

; 这是这份“文件里装载的用户程序”独有的成功标志位。
; 用户程序退出前会把它塞进返回值高位，内核据此确认：
; “这次回来的确实是文件加载程序，不是旧的内嵌 smoke program。”
%define USER_MODE_RESULT_FILE_PROGRAM_OK 0x0020

section .text

user_file_program_start:
    ; AX 只写低 16 位，正好用来装段选择子。
    xor eax, eax
    ; 把用户态数据段选择子装进 AX。
    mov ax, USER_DATA_SELECTOR_RPL3
    ; DS 指向用户态数据段。
    mov ds, ax
    ; ES 也一起切到用户态数据段。
    mov es, ax
    ; FS 先保持和 DS 一样，避免后面如果用到也落在合法用户段里。
    mov fs, ax
    ; GS 同理。
    mov gs, ax

    ; EAX = syscall number = write。
    mov eax, SYSCALL_WRITE_NUMBER
    ; EDI = fd = 1，也就是 stdout。
    mov edi, 1
    ; RSI = 要写出的字符串地址。
    lea rsi, [rel user_file_program_message]
    ; EDX = 字符串长度。
    mov edx, user_file_program_message_end - user_file_program_message
    ; 真正进入内核执行 write。
    int 0x80

    ; 先把“文件程序成功标志”放进返回值高 16 位以上。
    mov edi, USER_MODE_RESULT_FILE_PROGRAM_OK << 16
    ; 再把当前用户态 CS 塞进低 16 位。
    ; 这样内核回来后既能验证“返回时真的是 CPL=3 的代码段”，
    ; 也能验证“文件程序自己的成功标志位”。
    mov di, cs
    ; EAX = syscall number = exit。
    mov eax, SYSCALL_EXIT_NUMBER
    ; 进入内核，退出当前用户程序。
    int 0x80

.hang:
    ; 理论上 exit 不会返回；如果返回了，就原地停住，方便调试。
    jmp .hang

user_file_program_message:
    ; 这一行故意做成稳定日志，测试脚本会直接 grep 它。
    db "user_file_program=hello from fs", 10
user_file_program_message_end:
