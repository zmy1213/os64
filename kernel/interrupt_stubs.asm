bits 64

section .text

global isr_divide_error_stub
global isr_invalid_opcode_stub
global isr_double_fault_stub
global isr_general_protection_stub
global isr_page_fault_stub

extern kernel_handle_exception

%macro ISR_NO_ERROR 2
%1:
    push 0                          ; 没有硬件错误码的异常，这里手工补一个 0。
    push %2                         ; 再压入异常向量号，方便 C++ 侧统一判断是哪一类异常。
    jmp isr_common
%endmacro

%macro ISR_WITH_ERROR 2
%1:
    push %2                         ; 这类异常 CPU 已经自动压了 error code，
                                    ; 我们这里只需要再补一个 vector，让栈布局和上面统一。
    jmp isr_common
%endmacro

ISR_NO_ERROR isr_divide_error_stub, 0
ISR_NO_ERROR isr_invalid_opcode_stub, 6
ISR_WITH_ERROR isr_double_fault_stub, 8
ISR_WITH_ERROR isr_general_protection_stub, 13
ISR_WITH_ERROR isr_page_fault_stub, 14

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
