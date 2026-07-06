#include "main.h"

// Simple delay function
void delay() {
	volatile int i;
	for (i = 0; i < 100000; i++);
}

// Convert BCD to string
int bcdToStr(char bcd, char *buf) {
	// TODO: 把bcd转换为string并存入buf中
	// 将高4位（十位）转换为字符
    buf[0] = ((bcd >> 4) & 0x0F) + '0';
    // 将低4位（个位）转换为字符
    buf[1] = (bcd & 0x0F) + '0';
	return 2;
}

// Main program: display RTC time
int main(void) {
	char rtc_time_str[8] = "00:00:00";
	int row = 13;
	int col = (80 - 8) / 2;
	char hint_str[] = "Current Time:";
	displayStr(row - 1, (80 - 13) / 2, hint_str, 13);
	
	while (1) {
		// TODO: 实现展示时间的功能
		// 读取时、分、秒
        bcdToStr(readRTC(0x04), &rtc_time_str[0]);   // 小时
        bcdToStr(readRTC(0x02), &rtc_time_str[3]);   // 分钟
        bcdToStr(readRTC(0x00), &rtc_time_str[6]);   // 秒

        // 显示时间字符串
        displayStr(row, col, rtc_time_str, 8);

        delay();
	}
	return 0;
}
