bits 16                               ; stage2 刚被 stage1 跳过来时，CPU 仍在 16 位实模式。
org 0x8000                            ; 告诉汇编器：stage2 会被放在内存地址 0x8000。

%define STACK_TOP            0x9c00   ; stage2 在实模式下使用的栈顶位置。
%define PM_STACK_TOP         0x9f000  ; 切到 32 位保护模式后使用的栈顶位置。
%define LM_STACK_TOP         0x9e000  ; 切到 64 位 long mode 后使用的栈顶位置。
                                       ; 这里特意放在 0xA0000 以下，因为 0xA0000~0xBFFFF 是传统显存/设备洞，
                                       ; 不是普通 RAM。栈如果放进那一段，call/ret 压回地址就可能直接坏掉。
%define COM1_BASE            0x3f8    ; COM1 串口基址。
%define QEMU_DEBUG_EXIT      0x00f4   ; QEMU debug-exit 端口。
%define MEMORY_MAP_MAX       16       ; 最多先缓存 16 条 E820 内存映射记录。
%define CODE32_SELECTOR      0x08     ; GDT 里的 32 位代码段选择子。
%define DATA32_SELECTOR      0x10     ; GDT 里的 32 位数据段选择子。
%define CODE64_SELECTOR      0x18     ; GDT 里的 64 位代码段选择子。
%define DATA64_SELECTOR      0x20     ; GDT 里的 64 位数据段选择子。
%define PML4_BASE            0x1000   ; 页表第 1 级：PML4 放在 0x1000。
%define PDPT_BASE            0x2000   ; 页表第 2 级：PDPT 放在 0x2000。
%define PD_BASE              0x3000   ; 页表第 3 级：PD   放在 0x3000。
%define PAGE_TABLE_BYTES     12288    ; 3 个页表页，一共 12 KiB。
%define EFER_MSR             0xC0000080 ; EFER 的 MSR 编号。
%define BOOT_INFO_MAGIC      0x3436534f ; 这 4 个字节按 ASCII 看就是 "OS64"。

%include "kernel_meta.inc"             ; 这个文件由构建脚本生成，里面放内核加载地址和扇区数。

jmp start                             ; 跳过下面的数据区，直接去执行入口。
nop                                   ; 占位字节，让布局更清楚。

stage2_message         db 'stage2 ok', 0             ; 第二阶段成功进入时的提示。
a20_message            db 'a20 ok', 0                ; A20 成功开启时的提示。
e820_message           db 'e820 ok', 0               ; E820 内存映射成功获取时的提示。
protected_mode_message db 'protected mode ok', 0     ; 成功进入 32 位保护模式时的提示。
paging_message         db 'paging ok', 0             ; 页表准备并启用分页前的提示。
long_mode_message      db 'long mode ok', 0          ; 成功进入 64 位 long mode 时的提示。
kernel_loaded_message  db 'kernel loaded ok', 0      ; 已经把内核从磁盘读入内存时的提示。
a20_error              db 'a20 failed', 0            ; A20 失败时的提示。
e820_error             db 'e820 failed', 0           ; E820 失败时的提示。
kernel_load_error      db 'kernel load failed', 0    ; 读内核失败时的提示。

boot_drive             db 0                          ; 保存 BIOS 传来的启动盘号，后面 int 13h 读内核要用。

memory_map_count dw 0                  ; 保存已经拿到多少条 E820 记录。
memory_map times MEMORY_MAP_MAX * 24 db 0
                                       ; 预留内存区，按每条 24 字节缓存 E820 记录。
                                       ; 每条记录的布局先记成：
                                       ; 0~7   字节 = base，表示这段内存从哪里开始。
                                       ; 8~15  字节 = length，表示这段内存有多长。
                                       ; 16~19 字节 = type，表示 usable / reserved 等类型。
                                       ; 20~23 字节 = extended attributes，扩展属性字段。

boot_info:
    dd BOOT_INFO_MAGIC                 ; 让 kernel_main 能先检查自己拿到的是不是我们约定的结构。
    dw 0                               ; 这里稍后会写入 memory_map_count。
    dw 24                              ; 每条 E820 记录 24 字节，直接告诉内核。
    dq memory_map                      ; 把 E820 数组起始地址传给内核。

align 8                                ; GDT 按 8 字节对齐，更符合描述符布局。
gdt_start:
    dq 0x0000000000000000              ; 第 0 项必须是空描述符（null descriptor）。
    dq 0x00cf9a000000ffff              ; 第 1 项：32 位代码段，平坦模型，基址 0，界限最大。
    dq 0x00cf92000000ffff              ; 第 2 项：32 位数据段，平坦模型，基址 0，界限最大。
    dq 0x00af9a000000ffff              ; 第 3 项：64 位代码段，L=1，供 long mode 使用。
    dq 0x00cf92000000ffff              ; 第 4 项：64 位数据段，仍然使用平坦数据段描述符。
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
    mov [boot_drive], dl               ; BIOS 把“从哪块盘启动”放在 DL，这里先保存起来。

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

    call load_kernel_from_disk         ; 还在实模式时，先用 BIOS 把 kernel.bin 读到固定内存地址。
    jc stage2_kernel_load_failed       ; 如果 CF=1，说明读内核失败，不能继续切模式。

    mov si, kernel_loaded_message      ; SI 指向 "kernel loaded ok"。
    call print_string_screen           ; 屏幕打印“内核已经在内存里了”。
    mov si, kernel_loaded_message      ; 串口也打印同样的提示。
    call print_string_serial
    call print_crlf_serial

    cli                                ; 切模式前再关一次中断，保证过程干净。
    lgdt [gdt_descriptor]              ; 把我们准备好的 GDT 装进 GDTR。

    mov eax, cr0                       ; EAX = CR0。CR0 是控制寄存器，控制保护模式等 CPU 状态。
    or eax, 0x00000001                 ; 把 bit0（PE, Protection Enable）置 1。
    mov cr0, eax                       ; 写回 CR0，正式打开保护模式开关。
    jmp CODE32_SELECTOR:protected_mode_start
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
    jmp stage2_fail_and_exit           ; 统一走失败退出。

stage2_kernel_load_failed:             ; 读内核失败时的错误分支。
    mov si, kernel_load_error          ; SI 指向错误字符串。
    call print_string_screen           ; 屏幕打印错误。
    mov si, kernel_load_error          ; 串口也打印错误。
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

load_kernel_from_disk:                 ; 用最基础的 BIOS int 13h 读盘，把 kernel.bin 读到固定地址。
                                       ; 这里故意继续用“最朴素的 CHS 读盘”，因为这一轮内核很小，
                                       ; 目标只是先把 bootloader -> C++ kernel 这条路打通。
    push ax                            ; 先保存会用到的寄存器，避免破坏调用者现场。
    push bx
    push cx
    push dx
    push di
    push si
    push ds
    push es

    mov ax, KERNEL_LOAD_SEGMENT        ; AX = 内核目标段地址，这里由构建脚本固定成 0x1000。
    mov es, ax                         ; ES:BX 就是 BIOS int 13h 的目标缓冲区地址。
    mov bx, KERNEL_LOAD_OFFSET         ; BX = 0，表示从 0x1000:0000 开始写，也就是线性地址 0x10000。
    mov ah, 0x02                       ; AH = 0x02，表示 BIOS 传统“读扇区”功能。
    mov al, KERNEL_SECTORS             ; AL = 要读多少个扇区。当前构建脚本会确保它不跨这条磁道。
    mov ch, 0x00                       ; CH = 0，柱面号还是第 0 柱面。
    mov cl, KERNEL_START_SECTOR        ; CL = 10，内核从第 10 个扇区开始，也就是紧跟在 stage2 后面。
    mov dh, 0x00                       ; DH = 0，仍然是第 0 个磁头。
    mov dl, [boot_drive]               ; DL = 启动盘号，告诉 BIOS 从哪块盘继续读。
    int 0x13                           ; 真正发起读盘。
    jc .failed                         ; 如果 CF=1，说明 BIOS 读内核失败。

    clc                                ; CF = 0，表示成功。
    jmp .done                          ; 统一走恢复寄存器的出口。

.failed:
    stc                                ; CF = 1，表示失败。

.done:
    pop es                             ; 按相反顺序恢复寄存器。
    pop ds
    pop si                             ; 按相反顺序恢复寄存器。
    pop di
    pop dx
    pop cx
    pop bx
    pop ax
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
                                       ; 这段代码的目标很简单：
                                       ; 一条一条地问 BIOS："这台机器的内存地图是什么？"
    xor ebx, ebx                       ; EBX = 0。第一次调用 E820 时，BIOS 规定 EBX 必须从 0 开始。
                                       ; 你可以把 EBX 理解成“继续往后取下一条记录的游标”。
    mov di, memory_map                 ; DI = memory_map。DI 指向我们自己的缓存区起点。
                                       ; BIOS 这次返回的数据，会直接写到 ES:DI 指向的这块内存里。
    mov word [memory_map_count], 0     ; 先把“已经拿到几条记录”清零，表示这是一次全新的枚举。

.next:                                 ; 从这里开始，请求“下一条”E820 记录。
    mov dword [es:di + 20], 1          ; 先把当前输出槽位的第 20~23 字节写成 1。
                                       ; 这 4 字节是 24 字节版 E820 结构里的扩展属性字段。
                                       ; 很多 BIOS/教程都会这样先初始化，兼容性更稳。
    mov eax, 0xe820                    ; EAX = 0xE820。告诉 BIOS：我要调用 int 15h 的 E820 功能。
    mov edx, 0x534d4150                ; EDX = 'SMAP'。这是 E820 协议要求的签名，必须传。
                                       ; 0x53 4D 41 50 这四个 ASCII 字节正好就是 S M A P。
    mov ecx, 24                        ; ECX = 24。告诉 BIOS：我这次希望返回 24 字节长的记录。
                                       ; 也就是说，我们想拿到 base + length + type + ext attrs 全部字段。
    int 0x15                           ; 真正调用 BIOS。输入参数已经放在 EAX/EBX/ECX/EDX/ES:DI 里了。
    jc .failed                         ; 如果 CF = 1，说明 BIOS 这次调用失败，直接走失败返回。

    cmp eax, 0x534d4150                ; BIOS 成功返回时，EAX 也应该还是 'SMAP'。
                                       ; 这是我们对返回值做的一层自检，确认协议没跑偏。
    jne .failed                        ; 如果回来的不是 'SMAP'，就把这次结果当成无效数据。

    inc word [memory_map_count]        ; 既然这条记录有效，就把记录条数 +1。
    add di, 24                         ; DI 往后移动 24 字节，指向下一个输出槽位。
                                       ; 下一次 int 15h/e820 返回的新记录，就会写到这里。

    cmp word [memory_map_count], MEMORY_MAP_MAX
    jae .done                          ; 如果已经达到缓存上限，就先停下，避免把后面的内存写坏。

    test ebx, ebx                      ; BIOS 会把“还有没有下一条记录”放在 EBX 里返回给我们。
                                       ; 如果 EBX = 0，表示已经没有下一条了。
                                       ; 如果 EBX != 0，表示还有下一条，下次要带着这个 EBX 继续调用。
    jnz .next                          ; EBX != 0，就继续回到 .next，再向 BIOS 要一条记录。

.done:                                 ; 走到这里，表示我们已经把能拿的记录都拿完了。
    clc                                ; CF = 0。约定：返回给调用者时用 CF=0 表示成功。
    ret                                ; 返回。此时 memory_map 里已经存着 BIOS 给的内存地图。

.failed:                               ; 走到这里，表示 int 15h/e820 某一步失败了。
    stc                                ; CF = 1。约定：返回给调用者时用 CF=1 表示失败。
    ret                                ; 返回，让上层去打印 "e820 failed"。

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
    mov ax, DATA32_SELECTOR            ; AX = 0x10，选中 GDT 里的 32 位数据段描述符。
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

    call setup_page_tables             ; 构造最小页表，把低 2 MiB 做恒等映射。

    mov esi, paging_message            ; ESI 指向 "paging ok"。
    mov edi, 0xb8000 + 320             ; 第三行开始显示分页准备提示。
    call print_string_pm_screen        ; 直接写 VGA 文本显存。
    mov esi, paging_message            ; 串口也打印分页准备成功提示。
    call print_string_pm_serial
    call print_crlf_pm_serial

    call enable_long_mode              ; 打开 PAE、LME 和分页，准备进入 64 位模式。
    jmp CODE64_SELECTOR:long_mode_start
                                       ; 远跳转到 64 位代码段，正式进入 long mode。

setup_page_tables:                     ; 构造最小 4 级页表结构。
    cld                                ; 确保字符串指令按地址递增方向工作。
    mov edi, PML4_BASE                 ; EDI 指向页表起始地址 0x1000。
    xor eax, eax                       ; EAX = 0，用来把页表区域清零。
    mov ecx, PAGE_TABLE_BYTES / 4      ; ECX = 12288 / 4 = 3072，表示要清多少个双字。
    rep stosd                          ; 从 ES:EDI 开始连续写 0，清空 3 页页表内存。

    mov dword [PML4_BASE], PDPT_BASE | 0x03
                                       ; PML4[0] 指向 PDPT，0x03 表示 present + writable。
    mov dword [PML4_BASE + 4], 0x00    ; 高 32 位清零，因为低地址足够。

    mov dword [PDPT_BASE], PD_BASE | 0x03
                                       ; PDPT[0] 指向 PD，仍然标记 present + writable。
    mov dword [PDPT_BASE + 4], 0x00    ; 高 32 位清零。

    mov dword [PD_BASE], 0x00000083    ; PD[0] 映射一个 2 MiB 大页。
                                       ; 0x80 = PS(大页)，0x03 = present + writable。
    mov dword [PD_BASE + 4], 0x00      ; 高 32 位清零。
    ret                                ; 页表准备完成。

enable_long_mode:                      ; 打开 long mode 需要的所有 CPU 状态位。
    mov eax, PML4_BASE                 ; EAX = PML4 的物理地址。
    mov cr3, eax                       ; CR3 指向 PML4，告诉 CPU 页表根在哪里。

    mov eax, cr4                       ; 读取 CR4。
    or eax, 0x00000020                 ; 置位 bit5，也就是 PAE。
    mov cr4, eax                       ; 写回 CR4，打开 PAE。

    mov ecx, EFER_MSR                  ; ECX = EFER 的 MSR 编号。
    rdmsr                              ; 从 MSR 读 EFER 到 EDX:EAX。
    or eax, 0x00000100                 ; 置位 bit8，也就是 LME(Long Mode Enable)。
    wrmsr                              ; 把修改后的 EFER 写回去。

    mov eax, cr0                       ; 读取 CR0。
    or eax, 0x80000000                 ; 置位 bit31，也就是 PG(Paging Enable)。
    mov cr0, eax                       ; 打开分页，配合前面的 LME 才能真正进入 long mode。
    ret                                ; 返回，下一步通过 far jump 进入 64 位代码段。

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

bits 64                                ; 从这里开始，后面的代码按 64 位 long mode 指令编码。

long_mode_start:                       ; 进入 64 位 long mode 后的第一个入口。
    mov ax, DATA64_SELECTOR            ; AX = 64 位数据段选择子。
    mov ds, ax                         ; DS 指向平坦数据段。
    mov es, ax                         ; ES 指向平坦数据段。
    mov fs, ax                         ; FS 指向平坦数据段。
    mov gs, ax                         ; GS 指向平坦数据段。
    mov ss, ax                         ; SS 指向平坦数据段。
    mov rsp, LM_STACK_TOP              ; RSP = 64 位栈顶。这里必须放在真正的 RAM 里，
                                       ; 不能图省事放进 0xA0000 以上那段传统 PC 设备洞。

    mov rsi, long_mode_message         ; RSI 指向 "long mode ok"。
    mov rdi, 0xb8000 + 480             ; RDI 指向 VGA 第四行起始位置。
    call print_string_lm_screen        ; 直接写 VGA 文本显存，打印 64 位成功提示。

    mov rsi, long_mode_message         ; 串口也打印一份 "long mode ok"。
    call print_string_lm_serial
    call print_crlf_lm_serial

    mov ax, [abs memory_map_count]     ; AX = E820 记录条数。
    mov [abs boot_info + 4], ax        ; 把条数写进 BootInfo，方便 C++ 内核直接读取。
    mov edi, boot_info                 ; 第一个参数 RDI = BootInfo 指针，按 x86_64 调用约定传给内核。
    mov eax, KERNEL_LOAD_ADDR          ; RAX = 内核入口地址。这个地址由链接脚本固定到当前内核装载位置。
    jmp rax                            ; 直接跳到 kernel_entry，从这里开始进入真正的 64 位内核代码。

lm_halt:                               ; 64 位阶段的停机循环。
    cli                                ; 关中断。
    hlt                                ; 停住 CPU。
    jmp lm_halt                        ; 如果被唤醒，就继续停住。

print_string_lm_screen:                ; 64 位模式下直接写 VGA 文本显存。
    lodsb                              ; 从 DS:RSI 取 1 个字符到 AL，并让 RSI 后移。
    test al, al                        ; 看是不是字符串结束符 0。
    jz .done                           ; 是 0 就结束。
    mov [rdi], al                      ; 把字符本身写到 VGA 单元低字节。
    mov byte [rdi + 1], 0x0f           ; 把颜色属性写到 VGA 单元高字节。
    add rdi, 2                         ; 每个字符占 2 字节，所以前进 2。
    jmp print_string_lm_screen         ; 继续写下一个字符。
.done:
    ret                                ; 返回。

print_string_lm_serial:                ; 64 位模式下通过 COM1 打印字符串。
    lodsb                              ; 从 DS:RSI 取 1 个字符到 AL。
    test al, al                        ; 看是不是结束。
    jz .done                           ; 是 0 就返回。
    call serial_write_lm_char          ; 发这个字符。
    jmp print_string_lm_serial         ; 继续处理下一个字符。
.done:
    ret                                ; 返回。

print_crlf_lm_serial:                  ; 64 位模式下发送 CRLF。
    mov al, 0x0d                       ; AL = 回车。
    call serial_write_lm_char          ; 发回车。
    mov al, 0x0a                       ; AL = 换行。
    call serial_write_lm_char          ; 发换行。
    ret                                ; 返回。

serial_write_lm_char:                  ; 64 位模式下把 AL 里的字符发到 COM1。
    push rax                           ; 保存 RAX，因为轮询状态会改 AL。
    push rdx                           ; 保存 RDX，因为后面要切换端口。
.wait:
    mov dx, COM1_BASE + 5              ; DX = 0x3fd，LSR。
    in al, dx                          ; 读串口状态到 AL。
    test al, 0x20                      ; 检查发送保持寄存器是否为空。
    jz .wait                           ; 没空就继续等。

    pop rdx                            ; 恢复 RDX。
    pop rax                            ; 恢复要发送的字符到 AL。
    mov dx, COM1_BASE                  ; DX = 0x3f8，数据端口。
    out dx, al                         ; 发出字符。
    ret                                ; 返回。

times 4096 - ($ - $$) db 0             ; 把 stage2 填满到 4096 字节，也就是正好 8 个扇区。
