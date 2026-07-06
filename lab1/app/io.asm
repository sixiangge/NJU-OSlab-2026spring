bits 32

global displayStr
global clearStr
global readRTC

; Display string at row/col
displayStr:
	push ebx
	push edi
	mov edx, [esp+12]
	mov eax, [esp+16]
	imul edx, edx, 80
	add edx, eax
	shl edx, 1
	mov edi, edx
	mov ebx, [esp+20]
	mov ecx, [esp+24]
	mov ah, 0x14
nextChar:
	mov al, [ebx]
	mov [gs:edi], ax
	add edi, 2
	inc ebx
	loop nextChar
	pop edi
	pop ebx
	ret

; Clear string at row/col
clearStr:
	push edi
	mov edx, [esp+8]
	mov eax, [esp+12]
	imul edx, edx, 80
	add edx, eax
	shl edx, 1
	mov edi, edx
	mov ecx, [esp+16]
	mov ax, 0x1f20
clearLoop:
	mov [gs:edi], ax
	add edi, 2
	loop clearLoop
	pop edi
	ret

; Read from RTC register
readRTC:
    mov eax, [esp+4]       ; 获取参数 reg
    out 0x70, al           ; 向端口 0x70 写入寄存器索引
    in al, 0x71            ; 从端口 0x71 读取数据
    ret                    ; 返回，返回值已在 al 中
	; TODO: 实现readRTC，函数定义见main.h
