bits 16
org 0x8000

; Stage2 gets its own stack so it does not depend on stage1 stack contents.
%define STACK_TOP          0x9c00
%define COM1_BASE          0x3f8
%define QEMU_DEBUG_EXIT    0x00f4

; Jump over inline data to keep the entry point obvious.
jmp start
nop

message db 'stage2 ok', 0

start:
    ; Stage2 still runs in 16-bit real mode for now.
    cli

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_TOP

    ; Reinitialize COM1 because stage2 should be self-contained.
    call serial_init

    ; Print the second-stage milestone to the BIOS text console.
    mov si, message
    call print_string_screen

    ; Mirror the milestone to serial so automated tests can assert it.
    mov si, message
    call print_string_serial
    call print_crlf_serial

    ; Signal success to QEMU-based tests, then fall back to a halt loop.
    mov dx, QEMU_DEBUG_EXIT
    mov ax, 0x10
    out dx, ax

halt:
    cli
    hlt
    jmp halt

print_string_screen:
    ; BIOS teletype output prints one character at a time from DS:SI.
    lodsb
    test al, al
    jz .done
    mov ah, 0x0e
    mov bh, 0x00
    mov bl, 0x0a
    int 0x10
    jmp print_string_screen
.done:
    ret

print_string_serial:
    ; Serial output uses the same zero-terminated string convention.
    lodsb
    test al, al
    jz .done
    call serial_write_char
    jmp print_string_serial
.done:
    ret

print_crlf_serial:
    ; End the serial line cleanly so terminal logs stay readable.
    mov al, 0x0d
    call serial_write_char
    mov al, 0x0a
    call serial_write_char
    ret

serial_init:
    ; Configure COM1 for 38400 baud, 8 data bits, no parity, 1 stop bit.
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
    ; Wait until the UART transmit holding register is ready.
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

; Keep stage2 a single sector for the first two-stage loader milestone.
times 512 - ($ - $$) db 0
