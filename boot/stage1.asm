bits 16
org 0x7c00

%define STACK_TOP          0x7a00
%define COM1_BASE          0x3f8
%define QEMU_DEBUG_EXIT    0x00f4

jmp start
nop

boot_drive db 0
message    db 'stage1 ok', 0

start:
    cli

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_TOP

    mov [boot_drive], dl

    call serial_init

    mov si, message
    call print_string_screen

    mov si, message
    call print_string_serial
    call print_crlf_serial

    ; QEMU exits with status ((value << 1) | 1) when isa-debug-exit is present.
    mov dx, QEMU_DEBUG_EXIT
    mov ax, 0x10
    out dx, ax

halt:
    cli
    hlt
    jmp halt

print_string_screen:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    mov bh, 0x00
    mov bl, 0x07
    int 0x10
    jmp print_string_screen
.done:
    ret

print_string_serial:
    lodsb
    test al, al
    jz .done
    call serial_write_char
    jmp print_string_serial
.done:
    ret

print_crlf_serial:
    mov al, 0x0d
    call serial_write_char
    mov al, 0x0a
    call serial_write_char
    ret

serial_init:
    mov dx, COM1_BASE + 1
    mov al, 0x00
    out dx, al

    mov dx, COM1_BASE + 3
    mov al, 0x80
    out dx, al

    mov dx, COM1_BASE + 0
    mov al, 0x03
    out dx, al

    mov dx, COM1_BASE + 1
    mov al, 0x00
    out dx, al

    mov dx, COM1_BASE + 3
    mov al, 0x03
    out dx, al

    mov dx, COM1_BASE + 2
    mov al, 0xc7
    out dx, al

    mov dx, COM1_BASE + 4
    mov al, 0x0b
    out dx, al
    ret

serial_write_char:
    push ax
    push dx
.wait:
    mov dx, COM1_BASE + 5
    in al, dx
    test al, 0x20
    jz .wait

    pop dx
    pop ax
    mov dx, COM1_BASE
    out dx, al
    ret

times 510 - ($ - $$) db 0
dw 0xaa55
