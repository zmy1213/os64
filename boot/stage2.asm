bits 16                               ; stage2 刚被 stage1 跳过来时，CPU 仍在 16 位实模式。
org 0x8000                            ; 告诉汇编器：stage2 会被放在内存地址 0x8000。

%define STACK_TOP          0x9c00     ; stage2 在实模式下使用的栈顶位置。
%define PM_STACK_TOP       0x9f000    ; 切到 32 位保护模式后使用的栈顶位置。
%define COM1_BASE          0x3f8      ; COM1 串口基址。
%define QEMU_DEBUG_EXIT    0x00f4     ; QEMU debug-exit 端口。
%define MEMORY_MAP_MAX     16         ; 最多先缓存 16 条 E820 内存映射记录。
%define CODE_SELECTOR      0x08       ; GDT 里的代码段选择子：第 1 项 * 8。
%define DATA_SELECTOR      0x10       ; GDT 里的数据段选择子：第 2 项 * 8。

jmp start                             ; 跳过下面的数据区，直接去执行入口。
nop                                   ; 占位字节，让布局更清楚。

stage2_message         db 'stage2 ok', 0            ; 第二阶段成功进入时的提示。
a20_message            db 'a20 ok', 0               ; A20 成功开启时的提示。
e820_message           db 'e820 ok', 0              ; E820 内存映射成功获取时的提示。
protected_mode_message db 'protected mode ok', 0    ; 成功进入 32 位保护模式时的提示。
a20_error              db 'a20 failed', 0           ; A20 失败时的提示。
e820_error             db 'e820 failed', 0          ; E820 失败时的提示。

memory_map_count dw 0                  ; 保存已经拿到多少条 E820 记录。
memory_map times MEMORY_MAP_MAX * 24 db 0
                                       ; 预留内存区，按每条 24 字节缓存 E820 记录。

align 8                                ; GDT 按 8 字节对齐，更符合描述符布局。
gdt_start:
    dq 0x0000000000000000              ; 第 0 项必须是空描述符（null descriptor）。
    dq 0x00cf9a000000ffff              ; 第 1 项：32 位代码段，平坦模型，基址 0，界限最大。
    dq 0x00cf92000000ffff              ; 第 2 项：32 位数据段，平坦模型，基址 0，界限最大。
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1         ; GDT 大小减 1，供 lgdt 使用。
    dd gdt_start                       ; GDT 在内存中的线性地址。

start:                                 ; stage2 的 16 位实模式入口。
    cli                                ; 先关中断，避免切换环境时被打断。

    xor ax, ax                         ; AX = 0。
    mov ds, ax                         ; DS = 0，数据段从 0 段开始。
    mov es, ax                         ; ES = 0，额外数据段也先清零。
    mov ss, ax                         ; SS = 0，栈段放在 0 段。
    mov sp, STACK_TOP                  ; SP = 0x9c00，建立 stage2 自己的栈。

    call serial_init                   ; 初始化串口，保证后面的每个阶段都有日志。

    mov si, stage2_message             ; SI 指向 "stage2 ok"。
    call print_string_screen           ; 屏幕打印 second stage 成功提示。

    mov si, stage2_message             ; 再次把字符串地址放进 SI。
    call print_string_serial           ; 串口打印 second stage 成功提示。
    call print_crlf_serial             ; 串口换行。

    call enable_a20                    ; 先检查并开启 A20。
    jc stage2_a20_failed               ; 如果 CF=1，说明 A20 失败，跳错误处理。

    mov si, a20_message                ; SI 指向 "a20 ok"。
    call print_string_screen           ; 屏幕打印 A20 成功。
    mov si, a20_message                ; 同一字符串再次准备给串口。
    call print_string_serial           ; 串口打印 A20 成功。
    call print_crlf_serial             ; 串口换行。

    call collect_e820                  ; 获取 BIOS E820 内存映射。
    jc stage2_e820_failed              ; 如果 CF=1，说明 E820 失败。

    mov si, e820_message               ; SI 指向 "e820 ok"。
    call print_string_screen           ; 屏幕打印 E820 成功。
    mov si, e820_message               ; 串口也打印同样的里程碑。
    call print_string_serial
    call print_crlf_serial

    cli                                ; 切模式前再关一次中断，保证过程干净。
    lgdt [gdt_descriptor]              ; 把我们准备好的 GDT 装进 GDTR。

    mov eax, cr0                       ; EAX = CR0。CR0 是控制寄存器，控制保护模式等 CPU 状态。
    or eax, 0x00000001                 ; 把 bit0（PE, Protection Enable）置 1。
    mov cr0, eax                       ; 写回 CR0，正式打开保护模式开关。
    jmp CODE_SELECTOR:protected_mode_start
                                       ; 远跳转刷新 CS，并真正进入 32 位代码段。

stage2_a20_failed:                     ; A20 失败时的错误分支。
    mov si, a20_error                  ; SI 指向错误字符串。
    call print_string_screen           ; 屏幕打印错误。
    mov si, a20_error                  ; 串口也打印错误。
    call print_string_serial
    call print_crlf_serial
    jmp stage2_fail_and_exit           ; 统一走失败退出。

stage2_e820_failed:                    ; E820 失败时的错误分支。
    mov si, e820_error                 ; SI 指向错误字符串。
    call print_string_screen           ; 屏幕打印错误。
    mov si, e820_error                 ; 串口打印错误。
    call print_string_serial
    call print_crlf_serial

stage2_fail_and_exit:                  ; 统一的失败退出点。
    mov dx, QEMU_DEBUG_EXIT            ; DX = QEMU debug-exit 端口。
    mov ax, 0x11                       ; AX = 失败码。
    out dx, ax                         ; 通知 QEMU 这是一条失败路径。

halt:                                  ; 如果没有继续执行，就停在这里。
    cli                                ; 关中断。
    hlt                                ; 让 CPU 停住。
    jmp halt                           ; 若被唤醒，回到同一个 halt 循环。

enable_a20:                            ; 封装 A20 开启逻辑。
    call check_a20                     ; 先检查 A20 现在是否已经开启。
    cmp al, 1                          ; AL = 1 表示已经开启。
    je .done                           ; 如果已经开了，就不用再碰硬件。

    in al, 0x92                        ; 从系统控制端口 0x92 读当前值。
    or al, 0x02                        ; 置位 bit1，打开 A20 gate。
    and al, 0xfe                       ; 清 bit0，避免触发快速复位。
    out 0x92, al                       ; 把修改后的值写回 0x92。

    call check_a20                     ; 再检查一次，确认真的生效了。
    cmp al, 1                          ; 看 AL 是否变成 1。
    jne .failed                        ; 不是 1 就认为失败。

.done:
    clc                                ; CF = 0，表示成功。
    ret                                ; 返回调用者。

.failed:
    stc                                ; CF = 1，表示失败。
    ret                                ; 返回调用者。

check_a20:                             ; 检查 A20 是否开启。
    push bx                            ; 保存 BX，避免破坏调用者现场。
    push ds                            ; 保存 DS。
    push es                            ; 保存 ES。
    push si                            ; 保存 SI。
    push di                            ; 保存 DI。

    xor ax, ax                         ; AX = 0。
    mov es, ax                         ; ES = 0，访问低地址段。
    mov di, 0x0500                     ; DI = 0x0500，低地址测试位置。

    mov ax, 0xffff                     ; AX = 0xffff。
    mov ds, ax                         ; DS = 0xffff，访问高地址段。
    mov si, 0x0510                     ; SI = 0x0510，和低地址形成 1MiB 差异的测试位置。

    mov bl, [es:di]                    ; BL 保存低地址原始字节。
    mov bh, [ds:si]                    ; BH 保存高地址原始字节。

    mov byte [es:di], 0x00             ; 低地址写入 0x00。
    mov byte [ds:si], 0xff             ; 高地址写入 0xff。

    mov al, [es:di]                    ; 再读低地址到 AL。
    cmp al, 0xff                       ; 如果低地址也变成 0xff，说明地址发生了回绕。
    jne .enabled                       ; 如果没有回绕，说明 A20 已开启。

    xor al, al                         ; AL = 0，表示 A20 未开启。
    jmp .restore                       ; 跳去恢复原始内存内容。

.enabled:
    mov al, 1                          ; AL = 1，表示 A20 已开启。

.restore:
    mov [ds:si], bh                    ; 恢复高地址原始字节。
    mov [es:di], bl                    ; 恢复低地址原始字节。

    pop di                             ; 恢复 DI。
    pop si                             ; 恢复 SI。
    pop es                             ; 恢复 ES。
    pop ds                             ; 恢复 DS。
    pop bx                             ; 恢复 BX。
    ret                                ; 返回，结果在 AL 里。

collect_e820:                          ; 调 BIOS int 15h/e820h 获取内存映射。
    xor ebx, ebx                       ; EBX = 0。E820 第一次调用时要求 EBX 从 0 开始。
    mov di, memory_map                 ; DI 指向缓存区起点。
    mov word [memory_map_count], 0     ; 先把记录条数清零。

.next:
    mov dword [es:di + 20], 1          ; 按 ACPI/E820 约定，先把扩展属性字段设成 1。
    mov eax, 0xe820                    ; EAX = 0xe820，请求 BIOS 返回内存映射。
    mov edx, 0x534d4150                ; EDX = 'SMAP' 签名，E820 必填。
    mov ecx, 24                        ; ECX = 24，请求每条记录返回 24 字节。
    int 0x15                           ; 调 BIOS 内存映射服务。
    jc .failed                         ; 如果 CF=1，说明 BIOS 返回失败。

    cmp eax, 0x534d4150                ; BIOS 必须把 'SMAP' 原样回给我们。
    jne .failed                        ; 不是就认为这次调用无效。

    inc word [memory_map_count]        ; 记录数 +1。
    add di, 24                         ; DI 指向下一个缓存槽位。

    cmp word [memory_map_count], MEMORY_MAP_MAX
    jae .done                          ; 达到缓存上限就先停，避免写越界。

    test ebx, ebx                      ; E820 通过 EBX 返回“是否还有下一条”。
    jnz .next                          ; 如果 EBX != 0，就继续取下一条。

.done:
    clc                                ; CF = 0，表示成功。
    ret                                ; 返回。

.failed:
    stc                                ; CF = 1，表示失败。
    ret                                ; 返回。

print_string_screen:                   ; 实模式下用 BIOS int 10h 打印 0 结尾字符串。
    lodsb                              ; 从 DS:SI 取 1 个字节到 AL，然后 SI 自动后移。
    test al, al                        ; 看 AL 是否是 0。
    jz .done                           ; 是 0 就说明字符串结束。
    mov ah, 0x0e                       ; AH = 0x0e，BIOS teletype 打印功能。
    mov bh, 0x00                       ; BH = 页号 0。
    mov bl, 0x0a                       ; BL = 颜色属性，绿色系。
    int 0x10                           ; 调 BIOS 视频服务打印当前字符。
    jmp print_string_screen            ; 继续打印下一个字符。
.done:
    ret                                ; 返回。

print_string_serial:                   ; 实模式下通过串口打印 0 结尾字符串。
    lodsb                              ; 从 DS:SI 取下一个字符。
    test al, al                        ; 判断是否到字符串末尾。
    jz .done                           ; 到末尾就返回。
    call serial_write_char             ; 否则发送一个字符。
    jmp print_string_serial            ; 继续处理下一个字符。
.done:
    ret                                ; 返回。

print_crlf_serial:                     ; 串口发送回车换行。
    mov al, 0x0d                       ; AL = 回车 CR。
    call serial_write_char             ; 发回车。
    mov al, 0x0a                       ; AL = 换行 LF。
    call serial_write_char             ; 发换行。
    ret                                ; 返回。

serial_init:                           ; 初始化 COM1：38400 波特率，8N1。
    mov dx, COM1_BASE + 1              ; DX = 0x3f9，串口中断使能寄存器 IER。
    mov al, 0x00                       ; AL = 0，禁用所有串口中断。
    out dx, al                         ; 写 IER。

    mov dx, COM1_BASE + 3              ; DX = 0x3fb，线路控制寄存器 LCR。
    mov al, 0x80                       ; AL = 0x80，置 DLAB=1 以设置波特率除数。
    out dx, al                         ; 写 LCR。

    mov dx, COM1_BASE + 0              ; DX = 0x3f8，除数低字节寄存器。
    mov al, 0x03                       ; AL = 3，对应 38400 波特率。
    out dx, al                         ; 写除数低字节。

    mov dx, COM1_BASE + 1              ; DX = 0x3f9，除数高字节寄存器。
    mov al, 0x00                       ; 高字节为 0。
    out dx, al                         ; 写除数高字节。

    mov dx, COM1_BASE + 3              ; 回到 LCR。
    mov al, 0x03                       ; 8 位数据位、无校验、1 位停止位，同时关 DLAB。
    out dx, al                         ; 写 LCR。

    mov dx, COM1_BASE + 2              ; DX = 0x3fa，FIFO 控制寄存器 FCR。
    mov al, 0xc7                       ; 开 FIFO，清 FIFO，并设置阈值。
    out dx, al                         ; 写 FCR。

    mov dx, COM1_BASE + 4              ; DX = 0x3fc，调制解调器控制寄存器 MCR。
    mov al, 0x0b                       ; 常见初始化值：打开 DTR/RTS/OUT2。
    out dx, al                         ; 写 MCR。
    ret                                ; 初始化完成。

serial_write_char:                     ; 实模式下把 AL 里的一个字节发到 COM1。
    push ax                            ; 保存 AX，因为轮询状态会改 AL。
    push dx                            ; 保存 DX，因为后面会切换端口。
.wait:
    mov dx, COM1_BASE + 5              ; DX = 0x3fd，线路状态寄存器 LSR。
    in al, dx                          ; 读 LSR 到 AL。
    test al, 0x20                      ; 检查 bit5：THR 是否空。
    jz .wait                           ; 如果没空，就继续等。

    pop dx                             ; 恢复 DX。
    pop ax                             ; 恢复原来要发送的字符到 AL。
    mov dx, COM1_BASE                  ; DX = 0x3f8，串口数据端口。
    out dx, al                         ; 把 AL 里的字符发出去。
    ret                                ; 返回。

bits 32                                ; 从这里开始，后面的代码按 32 位指令编码。

protected_mode_start:                  ; 远跳转进入保护模式后的第一个 32 位入口。
    mov ax, DATA_SELECTOR              ; AX = 0x10，选中 GDT 里的数据段描述符。
    mov ds, ax                         ; DS 改成平坦数据段。
    mov es, ax                         ; ES 改成平坦数据段。
    mov fs, ax                         ; FS 改成平坦数据段。
    mov gs, ax                         ; GS 改成平坦数据段。
    mov ss, ax                         ; SS 改成平坦数据段。
    mov esp, PM_STACK_TOP              ; ESP = 0x9f000。ESP 是 32 位栈指针。

    mov esi, protected_mode_message    ; ESI 指向 32 位阶段要打印的字符串。
    mov edi, 0xb8000 + 160             ; EDI 指向 VGA 文本模式第二行起始位置。
    call print_string_pm_screen        ; 直接写 VGA 缓冲区打印 "protected mode ok"。

    mov esi, protected_mode_message    ; 再次把字符串地址放进 ESI。
    call print_string_pm_serial        ; 串口打印保护模式成功提示。
    call print_crlf_pm_serial          ; 串口换行。

    mov dx, QEMU_DEBUG_EXIT            ; DX = debug-exit 端口。
    mov ax, 0x10                       ; AX = 成功码。
    out dx, ax                         ; 通知 QEMU 这条链路已经成功。

pm_halt:                               ; 保护模式下的停机循环。
    cli                                ; 关中断。
    hlt                                ; 停住 CPU。
    jmp pm_halt                        ; 如果被唤醒，继续停机。

print_string_pm_screen:                ; 32 位保护模式下直接写 VGA 文本显存。
    lodsb                              ; 从 DS:ESI 取 1 个字符到 AL，并让 ESI 后移。
    test al, al                        ; 看是不是字符串结束符 0。
    jz .done                           ; 是 0 就结束。
    mov [edi], al                      ; 把字符本身写到 VGA 单元的低字节。
    mov byte [edi + 1], 0x0f           ; 把颜色属性写到 VGA 单元的高字节，白字黑底。
    add edi, 2                         ; VGA 每个字符占 2 字节，所以指针前进 2。
    jmp print_string_pm_screen         ; 继续打印下一个字符。
.done:
    ret                                ; 返回。

print_string_pm_serial:                ; 32 位保护模式下通过 COM1 打印字符串。
    lodsb                              ; 从 DS:ESI 取 1 个字符到 AL。
    test al, al                        ; 判断是否结束。
    jz .done                           ; 是 0 就返回。
    call serial_write_pm_char          ; 发送这个字符。
    jmp print_string_pm_serial         ; 继续处理下一个字符。
.done:
    ret                                ; 返回。

print_crlf_pm_serial:                  ; 32 位保护模式下发送 CRLF。
    mov al, 0x0d                       ; AL = 回车。
    call serial_write_pm_char          ; 发回车。
    mov al, 0x0a                       ; AL = 换行。
    call serial_write_pm_char          ; 发换行。
    ret                                ; 返回。

serial_write_pm_char:                  ; 32 位保护模式下把 AL 里的字符发到 COM1。
    push eax                           ; 保存 EAX，因为轮询状态会改 AL。
    push edx                           ; 保存 EDX，因为后面要切换端口。
.wait:
    mov dx, COM1_BASE + 5              ; DX = 0x3fd，LSR。
    in al, dx                          ; 读串口状态到 AL。
    test al, 0x20                      ; 检查发送保持寄存器是否为空。
    jz .wait                           ; 没空就继续等。

    pop edx                            ; 恢复 EDX。
    pop eax                            ; 恢复要发送的字符到 AL。
    mov dx, COM1_BASE                  ; DX = 0x3f8，数据端口。
    out dx, al                         ; 发出字符。
    ret                                ; 返回。

times 2048 - ($ - $$) db 0             ; 把 stage2 填满到 2048 字节，也就是正好 4 个扇区。
