#include "common.h"
#include "x86.h"
#include "device.h"

#define SYS_WRITE 0
#define SYS_NOW 2
#define SYS_ZERO_TIME_TICKS 3
#define SYS_GET_TIME_TICKS 4

static int bcdToBin(uint8_t value) {
	return (value & 0x0f) + ((value >> 4) * 10);
}

static uint32_t userPtr(struct TrapFrame *tf, uint32_t offset) {
	return getSegBase(tf->ds) + offset;
}

static int rtcReadHour(void) {
	uint8_t regB = readRTC(0x0b);
	uint8_t hour = readRTC(0x04);
	int binaryMode = regB & 0x04;
	int is24Hour = regB & 0x02;

	if (!binaryMode) {
		if (!is24Hour) {
			int isPm = hour & 0x80;
			hour &= 0x7f;
			hour = bcdToBin(hour);
			if (isPm && hour < 12) hour += 12;
			if (!isPm && hour == 12) hour = 0;
			return hour;
		}
		return bcdToBin(hour);
	}

	if (!is24Hour) {
		int isPm = hour & 0x80;
		hour &= 0x7f;
		if (isPm && hour < 12) hour += 12;
		if (!isPm && hour == 12) hour = 0;
	}
	return hour;
}

static void GProtectFaultHandle(struct TrapFrame *tf);
static void syscallHandle(struct TrapFrame *tf);
static void syscallWrite(struct TrapFrame *tf);
static void syscallNow(struct TrapFrame *tf);
static void timerHandle(struct TrapFrame *tf);
static void syscallZeroTimeTicks(struct TrapFrame *tf);
static void syscallGetTimeTicks(struct TrapFrame *tf);

void irqHandle(struct TrapFrame *tf) {
	switch(tf->irq) {
		case -1:
			if (tf->err_code != 0) {
				GProtectFaultHandle(tf);
			}
			break;
		case 0xd:
			GProtectFaultHandle(tf);
			break;
		case 0x20:
			timerHandle(tf);
			break;
		case 0x80:
			syscallHandle(tf);
			break;
		default:
			kprintf("Unhandled IRQ %x, ERR %x\n", tf->irq, tf->err_code);
			assert(0);
	}
}

static void GProtectFaultHandle(struct TrapFrame *tf){
	kprintf("\n--- General Protection Fault Caught by Kernel ---\n");
	kprintf("Application tried to access restricted memory or performed invalid operation.\n");
	kprintf("Trap Frame Info:\n");
	kprintf("  IRQ: %x, Error Code: %x\n", tf->irq, tf->err_code);
	kprintf("  EIP: %x, CS: %x, CPL: %d\n", tf->eip, tf->cs, tf->cs & 3);
	kprintf("  DS: %x, ES: %x, SS: %x\n", tf->ds, tf->es, tf->ss_user);
	
	if ((tf->cs & 3) == 3) {
		kprintf("Confirmed: Exception occurred in USER MODE (CPL 3).\n");
		kprintf("Segmentation protection is ACTIVE.\n");
	}

	kprintf("Halting system.\n");
	while(1);
}

// TODO 1: 实现 syscallHandle 系统调用分发函数
static void syscallHandle(struct TrapFrame *tf) {
	switch (tf->eax) {
		case SYS_WRITE:
			syscallWrite(tf);
			break;
		case SYS_NOW:
			syscallNow(tf);
			break;
		case SYS_ZERO_TIME_TICKS:
			syscallZeroTimeTicks(tf);
			tf->eax = 0;
			break;
		case SYS_GET_TIME_TICKS:
			syscallGetTimeTicks(tf);
			break;
		default:
			kprintf("Unknown syscall %d\n", tf->eax);
			assert(0);
	}
}

// TODO 2: 实现 syscallWrite 写屏功能
static void syscallWrite(struct TrapFrame *tf) {
	uint32_t offset = tf->ecx;
	uint32_t len = tf->edx;
	char *buf;
	uint32_t remaining;

	if (offset >= 0x20000 || len == 0) {
		tf->eax = 0;
		return;
	}
	if (len > 0x20000 - offset) {
		len = 0x20000 - offset;
	}

	buf = (char *)userPtr(tf, offset);
	remaining = len;
	while (remaining > 0) {
		char ch = *buf++;

		if (ch == '\r') {
			displayCol = 0;
			updateCursor(displayRow, displayCol);
			remaining--;
			continue;
		}

		if (ch == '\n') {
			displayCol = 0;
			displayRow++;
			if (displayRow >= 25) {
				scrollScreen();
				displayRow = 24;
			}
			updateCursor(displayRow, displayCol);
			remaining--;
			continue;
		}

		((volatile uint16_t *)0xb8000)[displayRow * 80 + displayCol] = ((uint16_t)0x0c << 8) | (uint8_t)ch;
		displayCol++;
		if (displayCol >= 80) {
			displayCol = 0;
			displayRow++;
			if (displayRow >= 25) {
				scrollScreen();
				displayRow = 24;
			}
		}
		updateCursor(displayRow, displayCol);
		remaining--;
	}

	tf->eax = len;
}

// TODO 3: 实现 syscallNow 获取实时时钟
static void syscallNow(struct TrapFrame *tf){
	int *hh = (int *)userPtr(tf, tf->ecx);
	int *mm = (int *)userPtr(tf, tf->edx);
	int *ss = (int *)userPtr(tf, tf->ebx);

	if (tf->ecx > 0x1fffc || tf->edx > 0x1fffc || tf->ebx > 0x1fffc) {
		tf->eax = 0;
		return;
	}

	*ss = bcdToBin(readRTC(0x00));
	*mm = bcdToBin(readRTC(0x02));
	*hh = rtcReadHour();
	tf->eax = 0;
}

static int ticks = 0;
static int tickCount = 0;

// TODO 4: 实现 timerHandle 时钟中断处理
static void timerHandle(struct TrapFrame *tf) {
	(void)tf;
	ticks++;
	tickCount++;
	if (tickCount >= 100) {
		tickCount = 0;
	}
}

static void syscallZeroTimeTicks(struct TrapFrame *tf) {
	(void)tf;
	// kprintf("Syscall: ZeroTimeTicks\n");
    ticks = 0;
}

static void syscallGetTimeTicks(struct TrapFrame *tf) {
    tf->eax = ticks;
}
