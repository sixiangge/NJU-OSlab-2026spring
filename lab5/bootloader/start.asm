%define BOOT_STACK_TOP 0x9ffff

bits 16

global start
extern bootMain

start:
	jmp short actual_start
	nop

	; BPB
	db "MSWIN4.1"
	dw 512
	db 1
	dw 1
	db 2
	dw 224
	dw 2880
	db 0xf0
	dw 9
	dw 18
	dw 2
	dd 0
	dd 0
	db 0x80
	db 0
	db 0x29
	dd 0x12345678
	db "MYOS       "
	db "FAT12   "

actual_start:
	cli
	xor ax, ax
	mov ds, ax
	mov es, ax
	mov ss, ax
	mov sp, 0x7c00

	; Enable A20
	in al, 0x92
	or al, 0x02
	out 0x92, al

	; Load GDT
	lgdt [gdtDesc]

	; Enable protected mode
	mov eax, cr0
	or al, 1
	mov cr0, eax

	; Jump to 32-bit code
	jmp dword 0x08:start32

bits 32
start32:
	; Set segments
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov ss, ax
	mov ax, 0x18
	mov gs, ax

	; Init stack and call bootMain
	mov eax, BOOT_STACK_TOP
	mov esp, eax
	jmp bootMain

align 4
gdt:
	dw 0, 0
	db 0, 0, 0, 0

	dw 0xffff, 0
	db 0, 0x9a, 0xcf, 0

	dw 0xffff, 0
	db 0, 0x92, 0xcf, 0

	dw 0xffff, 0x8000
	db 0x0b, 0x92, 0xcf, 0

gdtDesc:
	dw gdtDesc - gdt - 1
	dd gdt
