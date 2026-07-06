[bits 32]
extern kEntry
global _start

_start:
	cld
    call kEntry
    jmp $ ; If kEntry returns, hang
