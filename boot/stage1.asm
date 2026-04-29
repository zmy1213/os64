bits 16
org 0x7c00

; Keep the early stack below the boot sector so calls/returns have safe space.
%define STACK_TOP          0x7a00
; Stage2 is loaded one sector later into conventional memory at 0x8000.
%define STAGE2_OFFSET      0x8000
%define STAGE2_SECTORS     0x01
; COM1 lets us mirror messages to the terminal for automated tests.
%define COM1_BASE          0x3f8
; QEMU debug-exit gives us a simple way to signal success/failure to tests.
%define QEMU_DEBUG_EXIT    0x00f4

; Skip over inline data and continue at the real entry point.
jmp start
nop

; BIOS passes the boot drive in DL; stage1 saves it for later disk reads.
boot_drive db 0
message    db 'stage1 ok', 0
disk_error db 'disk read error', 0

start:
    ; Stage1 starts in 16-bit real mode with unknown segment state.
    cli

    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, STACK_TOP

    ; Preserve the BIOS boot drive because int 13h needs it.
    mov [boot_drive], dl

    ; Initialize serial first so every later milestone can be mirrored to logs.
    call serial_init

    ; Print the first-stage milestone to the BIOS text console.
    mov si, message
    call print_string_screen

    ; Mirror the same milestone to COM1 for non-GUI testing.
    mov si, message
    call print_string_serial
    call print_crlf_serial

    ; If the disk read succeeds, control transfers to the second-stage loader.
    call load_stage2
    jmp 0x0000:STAGE2_OFFSET

halt:
    cli
    hlt
    jmp halt

load_stage2:
    ; Read stage2 into physical address 0x8000 using BIOS CHS read.
    xor ax, ax
    mov es, ax
    mov bx, STAGE2_OFFSET

    ; int 13h AH=02h reads sectors from the boot disk.
    mov ah, 0x02
    mov al, STAGE2_SECTORS
    mov ch, 0x00
    mov cl, 0x02
    mov dh, 0x00
    mov dl, [boot_drive]
    int 0x13
    jc disk_read_failed

    ret

disk_read_failed:
    ; On failure, surface the problem on both screen and serial before halting.
    mov si, disk_error
    call print_string_screen
    mov si, disk_error
    call print_string_serial
    call print_crlf_serial

    mov dx, QEMU_DEBUG_EXIT
    mov ax, 0x11
    out dx, ax
    jmp halt

print_string_screen:
    ; BIOS teletype output prints one character at a time from DS:SI.
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
    ; Serial output uses the same zero-terminated string convention.
    lodsb
    test al, al
    jz .done
    call serial_write_char
    jmp print_string_serial
.done:
    ret

print_crlf_serial:
    ; Serial logs are easier to read when each milestone ends with CRLF.
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

; A BIOS boot sector must be exactly 512 bytes and end with 0xaa55.
times 510 - ($ - $$) db 0
dw 0xaa55
