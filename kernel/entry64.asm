bits 64                                ; 从这个文件开始，我们已经站在 64 位模式里了。

%define KERNEL_STACK_TOP  0x180000     ; 给真正的内核单独准备一块栈，和 stage2 的栈分开。
%define QEMU_DEBUG_EXIT   0x00f4       ; 和前面 boot 阶段一样，继续复用 QEMU 的 debug-exit 端口。

section .text.entry                    ; 单独放进 .text.entry，链接脚本会把它摆在最前面。

global kernel_entry                    ; 导出入口符号，链接脚本会把 ENTRY 指到这里。
extern kernel_main                     ; 告诉汇编器：kernel_main 在 C++ 文件里实现。

kernel_entry:
    mov rsp, KERNEL_STACK_TOP          ; RSP = 64 位内核自己的栈顶。
                                       ; 这样后面即使 stage2 的栈布局改了，也不会影响内核。
    xor rbp, rbp                       ; RBP 先清零，方便后面调试时一眼看出栈回溯从哪里断掉。

    call kernel_main                   ; 调用真正的 C++ 入口。
                                       ; stage2 已经把 BootInfo 指针放进 RDI，这里直接沿用。

    mov dx, QEMU_DEBUG_EXIT            ; 如果 kernel_main 返回了，就把成功码发给 QEMU。
    mov ax, 0x10                       ; 0x10 仍然表示“这条成功路径已经走通”。
    out dx, ax                         ; 这样自动测试和手工运行都能更快收尾。

.halt:
    cli                                ; 如果没挂 QEMU debug-exit 设备，就退化成普通停机循环。
    hlt
    jmp .halt
