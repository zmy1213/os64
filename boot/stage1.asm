bits 16                              ; 告诉 NASM：下面这段代码按 16 位实模式指令来汇编。
org 0x7c00                           ; 告诉汇编器：这段 boot sector 会被 BIOS 放到 0x7c00 执行。

%define STACK_TOP       0x7a00       ; 早期栈顶放在 0x7a00，避开 0x7c00 这块 boot sector 本身。
%define STAGE2_OFFSET   0x8000       ; stage2 被读入到物理地址 0x8000。
%define STAGE2_SECTORS  0x04         ; stage2 当前占 4 个扇区，也就是 4 * 512 = 2048 字节。
%define COM1_BASE       0x3f8        ; COM1 串口的标准 I/O 端口基址。
%define QEMU_DEBUG_EXIT 0x00f4       ; QEMU 的 isa-debug-exit 设备监听的 I/O 端口。

jmp start                             ; 先跳过后面的数据区，直接去真正入口。
nop                                   ; 填一个空操作字节，常见于 boot sector 开头布局。

boot_drive db 0                       ; 保存 BIOS 传进来的启动盘号，后面 int 13h 读盘要用。
message    db 'stage1 ok', 0          ; 第一阶段成功提示，0 结尾字符串。
disk_error db 'disk read error', 0    ; 读 stage2 失败时的提示信息。

start:                                ; BIOS 把控制权交给我们的第一个入口。
    cli                               ; 先关中断，避免初始化段寄存器和栈时被打断。

    xor ax, ax                        ; AX = 0。AX 是 16 位通用寄存器，这里用来准备零值。
    mov ds, ax                        ; DS = 0。DS 是数据段寄存器，字符串/数据默认从这里找。
    mov es, ax                        ; ES = 0。ES 常用作额外数据段，这里先清零，后面读盘要用。
    mov ss, ax                        ; SS = 0。SS 是栈段寄存器，决定栈在哪个段里。
    mov sp, STACK_TOP                 ; SP = 0x7a00。SP 是 16 位栈指针，配合 SS 形成栈顶地址。

    mov [boot_drive], dl              ; DL 里是 BIOS 传进来的启动盘号，先保存到内存。
                                       ; DL 是 DX 的低 8 位，DX 是 16 位通用寄存器。

    call serial_init                  ; 初始化 COM1 串口，后面把日志同时发到终端。

    mov si, message                   ; SI = message 地址。SI 常用作字符串“源指针”。
    call print_string_screen          ; 用 BIOS int 10h 把 "stage1 ok" 打到屏幕上。

    mov si, message                   ; 再把同一串字符串地址放进 SI。
    call print_string_serial          ; 通过串口把 "stage1 ok" 打到终端日志。
    call print_crlf_serial            ; 串口再补一个回车换行，方便日志阅读。

    call load_stage2                  ; 从磁盘把 stage2 读到 0x8000。
    jmp 0x0000:STAGE2_OFFSET          ; 远跳转到 0000:8000，也就是 stage2 的入口位置。
                                       ; 这里显式给出段:偏移，确保 CS 也切到我们期望的值。

halt:                                 ; 如果后面没有继续执行，就停在这里。
    cli                               ; 关中断，避免睡眠期间被唤醒。
    hlt                               ; 让 CPU 进入 halt 状态，节省忙等。
    jmp halt                          ; 如果被意外唤醒，就继续回到 halt 循环。

load_stage2:                          ; 读取第二阶段加载器。
    xor ax, ax                        ; AX = 0。
    mov es, ax                        ; ES = 0。BIOS int 13h 读盘的目标地址要放在 ES:BX。
    mov bx, STAGE2_OFFSET             ; BX = 0x8000。BX 是 16 位基址寄存器，这里当目标偏移。

    mov ah, 0x02                      ; AH = 0x02。int 13h 的功能号 02h 表示“读取扇区”。
    mov al, STAGE2_SECTORS            ; AL = 4。AL 是 AX 的低 8 位，表示要读多少个扇区。
    mov ch, 0x00                      ; CH = 0。柱面号低 8 位，这里从柱面 0 开始读。
    mov cl, 0x02                      ; CL = 2。扇区号从 1 开始，2 表示读第 2 个扇区。
    mov dh, 0x00                      ; DH = 0。磁头号，这里读第 0 个磁头。
    mov dl, [boot_drive]              ; DL = 启动盘号，告诉 BIOS 要从哪块盘读。
    int 0x13                          ; 调 BIOS 磁盘服务，按上面的 CHS 参数把 stage2 读进来。
    jc disk_read_failed               ; 如果 CF=1，说明读盘失败，跳去错误处理。
                                       ; jc = jump if carry，检测的是进位标志 CF。

    ret                               ; 读盘成功，返回给调用者。

disk_read_failed:                     ; 读盘失败时的统一错误处理。
    mov si, disk_error                ; SI 指向错误字符串。
    call print_string_screen          ; 屏幕打印错误。
    mov si, disk_error                ; 再次把错误字符串地址放进 SI。
    call print_string_serial          ; 串口打印错误。
    call print_crlf_serial            ; 串口补回车换行。

    mov dx, QEMU_DEBUG_EXIT           ; DX = debug-exit 端口。DX 常作为 I/O 端口寄存器使用。
    mov ax, 0x11                      ; AX = 失败码。QEMU 会根据这个值计算退出状态。
    out dx, ax                        ; 向 I/O 端口写出 AX，通知 QEMU 这是失败路径。
    jmp halt                          ; 然后停机，避免继续执行未知状态代码。

print_string_screen:                  ; 用 BIOS 视频中断打印 0 结尾字符串。
    lodsb                             ; 从 DS:SI 读 1 个字节到 AL，然后 SI 自动加 1。
    test al, al                       ; 检查 AL 是否为 0。test 不改值，只改标志位。
    jz .done                          ; 如果 AL == 0，说明字符串结束，跳到 .done 返回。
    mov ah, 0x0e                      ; AH = 0x0e。int 10h 的 teletype 输出功能号。
    mov bh, 0x00                      ; BH = 0。页号，通常保持 0 即可。
    mov bl, 0x07                      ; BL = 0x07。字符属性/颜色，浅灰字黑底。
    int 0x10                          ; 调 BIOS 视频中断，把 AL 里的字符打印出来。
    jmp print_string_screen           ; 继续打印下一个字符。
.done:
    ret                               ; 字符串结束，返回。

print_string_serial:                  ; 通过 COM1 打印 0 结尾字符串。
    lodsb                             ; 从 DS:SI 取一个字符到 AL，并把 SI 向后移动。
    test al, al                       ; 看是不是字符串结束符 0。
    jz .done                          ; 如果是 0，就返回。
    call serial_write_char            ; 否则把 AL 里的字符写到串口。
    jmp print_string_serial           ; 继续处理下一个字符。
.done:
    ret                               ; 字符串发完，返回。

print_crlf_serial:                    ; 串口打印 CRLF，让日志换到新的一行。
    mov al, 0x0d                      ; AL = 0x0d，也就是回车 CR。
    call serial_write_char            ; 发送回车。
    mov al, 0x0a                      ; AL = 0x0a，也就是换行 LF。
    call serial_write_char            ; 发送换行。
    ret                               ; 返回。

serial_init:                          ; 初始化 COM1：38400 波特率，8N1。
    mov dx, COM1_BASE + 1             ; DX = 0x3f9，先关串口中断。
    mov al, 0x00                      ; AL = 0。
    out dx, al                        ; 向端口写 0，禁用所有串口中断。

    mov dx, COM1_BASE + 3             ; DX = 0x3fb，线路控制寄存器 LCR。
    mov al, 0x80                      ; AL = 0x80，设置 DLAB=1，允许配置波特率除数。
    out dx, al                        ; 写入 LCR。

    mov dx, COM1_BASE + 0             ; DX = 0x3f8，除数低字节。
    mov al, 0x03                      ; AL = 3。除数 3 对应 38400 波特率。
    out dx, al                        ; 写入低字节。

    mov dx, COM1_BASE + 1             ; DX = 0x3f9，除数高字节。
    mov al, 0x00                      ; 高字节为 0。
    out dx, al                        ; 写入高字节。

    mov dx, COM1_BASE + 3             ; 回到 LCR。
    mov al, 0x03                      ; 0x03 = 8 位数据位、无校验、1 位停止位，同时清掉 DLAB。
    out dx, al                        ; 应用 8N1 配置。

    mov dx, COM1_BASE + 2             ; DX = 0x3fa，FIFO 控制寄存器。
    mov al, 0xc7                      ; 开 FIFO，清空收发 FIFO，并设置阈值。
    out dx, al                        ; 写入 FCR。

    mov dx, COM1_BASE + 4             ; DX = 0x3fc，调制解调器控制寄存器。
    mov al, 0x0b                      ; 打开 IRQ/RTS/DTR 所需位，常见初始化值。
    out dx, al                        ; 写入 MCR。
    ret                               ; 初始化结束。

serial_write_char:                    ; 把 AL 里的一个字符发到 COM1。
    push ax                           ; 先保存 AX，因为轮询状态寄存器会改 AL。
    push dx                           ; 再保存 DX，因为后面会切换不同串口端口。
.wait:
    mov dx, COM1_BASE + 5             ; DX = 0x3fd，线路状态寄存器 LSR。
    in al, dx                         ; 从 LSR 读当前状态到 AL。
    test al, 0x20                     ; 检查 bit5：发送保持寄存器 THR 是否空。
    jz .wait                          ; 如果还没空，就继续等。

    pop dx                            ; 恢复之前保存的 DX。
    pop ax                            ; 恢复之前要发送的字符到 AL。
    mov dx, COM1_BASE                 ; DX = 0x3f8，串口发送数据端口。
    out dx, al                        ; 把 AL 中的字符写到串口数据寄存器。
    ret                               ; 一个字符发送完毕。

times 510 - ($ - $$) db 0             ; 用 0 填满到第 510 字节，保证整个扇区正好 512 字节。
dw 0xaa55                             ; BIOS 启动签名，低地址字节序看起来会是 55 aa。
