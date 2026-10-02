bits 64

section .text

global isr_stub_table
global irq_stub_table
global syscall_interrupt_stub
global irq_stub_240, irq_stub_241, irq_stub_255

extern kernel_handle_exception
extern kernel_handle_irq
extern kernel_handle_syscall
extern kernel_gate_enter
extern kernel_gate_leave_user

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

align 8

%macro IRQ_STUB 2
irq_stub_%1:
    push 0                          ; 硬件 IRQ 没有 CPU 自动压入的 error code，这里补一个 0。
    push %2                         ; 再补上已经重映射后的向量号，比如 IRQ0 -> 32。
    push rax                        ; 从这里开始把通用寄存器现场全部保存下来。
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov r12, rsp                    ; r12 已在寄存器帧中保存，现用于保留未对齐的帧地址。
    and rsp, -16                    ; 所有 C++ 调用都遵守 SysV 栈对齐约定。
    cld
    call kernel_gate_enter          ; 先取得 CPU 所有的内核锁，才能读取共享调度器。
    mov rdi, r12                    ; 现在 IRQ 也把“完整寄存器帧起点”交给 C++，这样后面才能保存用户态被抢占时的全部现场。
    cld
    call kernel_handle_irq
    test byte [r12 + 144], 3         ; 完整帧中的 CS：只有返回 ring3 时才释放内核锁。
    jz %%kernel_return
    call kernel_gate_leave_user
%%kernel_return:
    mov rsp, r12
    pop r15                         ; C++ 处理完后，把刚才保存的寄存器按相反顺序恢复。
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    add rsp, 16                     ; 丢掉我们手工补的 error_code 和 vector。
    iretq                           ; 返回到被中断前的那条指令继续执行。
%endmacro

IRQ_STUB 0, 32
IRQ_STUB 1, 33
IRQ_STUB 2, 34
IRQ_STUB 3, 35
IRQ_STUB 4, 36
IRQ_STUB 5, 37
IRQ_STUB 6, 38
IRQ_STUB 7, 39
IRQ_STUB 8, 40
IRQ_STUB 9, 41
IRQ_STUB 10, 42
IRQ_STUB 11, 43
IRQ_STUB 12, 44
IRQ_STUB 13, 45
IRQ_STUB 14, 46
IRQ_STUB 15, 47
IRQ_STUB 240, 240
IRQ_STUB 241, 241
IRQ_STUB 255, 255

irq_stub_table:
    dq irq_stub_0
    dq irq_stub_1
    dq irq_stub_2
    dq irq_stub_3
    dq irq_stub_4
    dq irq_stub_5
    dq irq_stub_6
    dq irq_stub_7
    dq irq_stub_8
    dq irq_stub_9
    dq irq_stub_10
    dq irq_stub_11
    dq irq_stub_12
    dq irq_stub_13
    dq irq_stub_14
    dq irq_stub_15

section .text

syscall_interrupt_stub:
    push 0                          ; 软中断没有 CPU 自动错误码，先补一个 0。
    push 128                        ; 再把向量号 0x80 也压进去，方便日志和后续统一扩展。
    ; 这里和普通异常最大的不同是：
    ; 我们希望 C++ syscall 层能看到“完整寄存器现场”，
    ; 所以后面会把通用寄存器全都按固定顺序压进去，拼成 RegisterInterruptFrame。
    push rax                        ; 从这里开始把通用寄存器全部保存起来。
    push rcx                        ; 当前 ABI 里第 4 个参数先约定放在 RCX。
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov r12, rsp
    and rsp, -16
    cld
    call kernel_gate_enter
    mov rdi, r12                    ; 第 1 个参数：整个 syscall 寄存器帧的起始地址。
    cld
    call kernel_handle_syscall
    test byte [r12 + 144], 3
    jz .kernel_return
    call kernel_gate_leave_user
.kernel_return:
    mov rsp, r12
    ; 注意这里不会像异常路径那样直接停机；
    ; syscall 处理完以后，目标就是恢复现场并继续回到触发 `int 0x80` 的下一条用户/内核指令。
    pop r15                         ; 处理完后按相反顺序恢复寄存器。
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    add rsp, 16                     ; 丢掉手工补的 error_code 和 vector。
    iretq                           ; 回到触发 `int 0x80` 的下一条指令继续执行。

isr_common:
    mov r12, rsp                    ; 异常处理不返回；先保存最小异常帧和 CR2。
    mov r13, cr2
    and rsp, -16
    cld
    call kernel_gate_enter
    mov rdi, r12
    mov rsi, r13
    call kernel_handle_exception

.halt:
    cli                             ; 如果处理函数返回了，说明我们仍然不打算恢复执行。
    hlt
    jmp .halt
