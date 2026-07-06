[bits 32]
global initTimer

; TODO: 配置8253定时器，产生周期性中断
; - 向端口0x43写入控制字（模式3，波特率发生器）
; - 向端口0x40写入分频值
; - 计算：输入频率1193182Hz / 目标频率100Hz = 11932
; - 实现10ms产生一次时钟中断（用于sleep精度）
initTimer:
    mov dx, 0x43
    mov al, 0x36
    out dx, al

    mov dx, 0x40
    mov ax, 11932
    out dx, al
    mov al, ah
    out dx, al

    ret
