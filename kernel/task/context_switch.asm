bits 64

global scheduler_switch_context

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
