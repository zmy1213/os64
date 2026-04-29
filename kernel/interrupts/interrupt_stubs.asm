bits 64

section .text

global isr_stub_table

extern kernel_handle_exception

%macro ISR_NO_ERROR 1
isr_stub_%1:
    push 0                          ; 没有硬件错误码的异常，这里手工补一个 0。
    push %1                         ; 再压入异常向量号，方便 C++ 侧统一判断是哪一类异常。
    jmp isr_common
%endmacro

%macro ISR_WITH_ERROR 1
isr_stub_%1:
    push %1                         ; 这类异常 CPU 已经自动压了 error code，
                                    ; 我们这里只需要再补一个 vector，让栈布局和上面统一。
    jmp isr_common
%endmacro

ISR_NO_ERROR 0
ISR_NO_ERROR 1
ISR_NO_ERROR 2
ISR_NO_ERROR 3
ISR_NO_ERROR 4
ISR_NO_ERROR 5
ISR_NO_ERROR 6
ISR_NO_ERROR 7
ISR_WITH_ERROR 8
ISR_NO_ERROR 9
ISR_WITH_ERROR 10
ISR_WITH_ERROR 11
ISR_WITH_ERROR 12
ISR_WITH_ERROR 13
ISR_WITH_ERROR 14
ISR_NO_ERROR 15
ISR_NO_ERROR 16
ISR_WITH_ERROR 17
ISR_NO_ERROR 18
ISR_NO_ERROR 19
ISR_NO_ERROR 20
ISR_WITH_ERROR 21
ISR_NO_ERROR 22
ISR_NO_ERROR 23
ISR_NO_ERROR 24
ISR_NO_ERROR 25
ISR_NO_ERROR 26
ISR_NO_ERROR 27
ISR_NO_ERROR 28
ISR_WITH_ERROR 29
ISR_WITH_ERROR 30
ISR_NO_ERROR 31

section .rodata

align 8
isr_stub_table:
    dq isr_stub_0
    dq isr_stub_1
    dq isr_stub_2
    dq isr_stub_3
    dq isr_stub_4
    dq isr_stub_5
    dq isr_stub_6
    dq isr_stub_7
    dq isr_stub_8
    dq isr_stub_9
    dq isr_stub_10
    dq isr_stub_11
    dq isr_stub_12
    dq isr_stub_13
    dq isr_stub_14
    dq isr_stub_15
    dq isr_stub_16
    dq isr_stub_17
    dq isr_stub_18
    dq isr_stub_19
    dq isr_stub_20
    dq isr_stub_21
    dq isr_stub_22
    dq isr_stub_23
    dq isr_stub_24
    dq isr_stub_25
    dq isr_stub_26
    dq isr_stub_27
    dq isr_stub_28
    dq isr_stub_29
    dq isr_stub_30
    dq isr_stub_31

section .text

isr_common:
    mov rdi, rsp                    ; 第 1 个参数：指向我们约定的 InterruptFrame。
    mov rax, cr2                    ; CR2 只在 page fault 时最有价值，但这里统一都传过去。
    mov rsi, rax                    ; 第 2 个参数：fault address / 最近一次页错误地址。
    and rsp, -16                    ; 调 C++ 前先把栈对齐到 16 字节，满足 SysV ABI 要求。
    cld                             ; 保守起见先清方向位，避免后面的字符串类代码受污染。
    call kernel_handle_exception

.halt:
    cli                             ; 如果处理函数返回了，说明我们仍然不打算恢复执行。
    hlt
    jmp .halt
