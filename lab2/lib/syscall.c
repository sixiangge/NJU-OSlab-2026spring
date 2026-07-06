#include "../kernel/include/common/stdarg.h"

// System Call Numbers
#define SYS_WRITE 0
#define SYS_NOW 2
#define SYS_ZERO_TIME_TICKS 3
#define SYS_GET_TIME_TICKS 4
#define MAX_BUFFER_SIZE 256

int syscall(int num, unsigned int a1, unsigned int a2, unsigned int a3, unsigned int a4, unsigned int a5);

// TODO 1: 实现 now() 函数
// - 参数为小时/分钟/秒的指针
void now(int *hh, int *mm, int *ss){
	syscall(SYS_NOW, (unsigned int)hh, (unsigned int)mm, (unsigned int)ss, 0, 0);
}

void zeroTimeTicks() {
	syscall(SYS_ZERO_TIME_TICKS, 0, 0, 0, 0, 0);
}

int getTimeTicks() {
	return syscall(SYS_GET_TIME_TICKS, 0, 0, 0, 0, 0);
}

// TODO 2: 实现 sleep() 函数
void sleep(int second) {
	int start = getTimeTicks();
	int target = second * 100;
	while (getTimeTicks() - start < target) {
	}
}

static int dec2Str(int n, char *buf, int count, int w) {
	char s[16];
	int i = 0, neg = n < 0;
	if (neg) n = -n;
	do { s[i++] = (n % 10) + '0'; n /= 10; } while (n > 0);
	if (neg) s[i++] = '-';
	while (i < w) s[i++] = '0';
	while (i > 0) {
		buf[count++] = s[--i];
		if (count == MAX_BUFFER_SIZE) {
			syscall(SYS_WRITE, (unsigned int)buf, (unsigned int)MAX_BUFFER_SIZE, 0, 0, 0);
			count = 0;
		}
	}
	return count;
}

// TODO 3: 实现 printf() 函数
void printf(const char *format,...){
	va_list ap;
	char buf[MAX_BUFFER_SIZE];
	int count = 0;

	va_start(ap, format);
	for (const char *p = format; *p; p++) {
		if (*p != '%') {
			buf[count++] = *p;
			if (count == MAX_BUFFER_SIZE) {
				syscall(SYS_WRITE, (unsigned int)buf, (unsigned int)MAX_BUFFER_SIZE, 0, 0, 0);
				count = 0;
			}
			continue;
		}

		p++;
		int width = 0;
		while (*p >= '0' && *p <= '9') {
			width = width * 10 + (*p - '0');
			p++;
		}

		if (*p == 'd') {
			count = dec2Str(va_arg(ap, int), buf, count, width);
		} else if (*p == 'x') {
			unsigned int val = va_arg(ap, unsigned int);
			char s[16];
			int i = 0;
			if (val == 0) {
				s[i++] = '0';
			} else {
				while (val > 0) {
					int digit = val & 0xf;
					s[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
					val >>= 4;
				}
			}
			while (i < width) {
				s[i++] = '0';
			}
			while (i > 0) {
				buf[count++] = s[--i];
				if (count == MAX_BUFFER_SIZE) {
					syscall(SYS_WRITE, (unsigned int)buf, (unsigned int)MAX_BUFFER_SIZE, 0, 0, 0);
					count = 0;
				}
			}
		} else if (*p == 's') {
			char *s = va_arg(ap, char *);
			if (s == 0) {
				s = "(null)";
			}
			while (*s) {
				buf[count++] = *s++;
				if (count == MAX_BUFFER_SIZE) {
					syscall(SYS_WRITE, (unsigned int)buf, (unsigned int)MAX_BUFFER_SIZE, 0, 0, 0);
					count = 0;
				}
			}
		} else if (*p == '%') {
			buf[count++] = '%';
			if (count == MAX_BUFFER_SIZE) {
				syscall(SYS_WRITE, (unsigned int)buf, (unsigned int)MAX_BUFFER_SIZE, 0, 0, 0);
				count = 0;
			}
		} else {
			buf[count++] = '%';
			if (count == MAX_BUFFER_SIZE) {
				syscall(SYS_WRITE, (unsigned int)buf, (unsigned int)MAX_BUFFER_SIZE, 0, 0, 0);
				count = 0;
			}
			if (*p) {
				buf[count++] = *p;
				if (count == MAX_BUFFER_SIZE) {
					syscall(SYS_WRITE, (unsigned int)buf, (unsigned int)MAX_BUFFER_SIZE, 0, 0, 0);
					count = 0;
				}
			}
		}
	}
	va_end(ap);

	if (count > 0) {
		syscall(SYS_WRITE, (unsigned int)buf, (unsigned int)count, 0, 0, 0);
	}
}