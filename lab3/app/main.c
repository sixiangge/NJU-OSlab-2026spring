#include "myos.h"

/* TODO 1: 添加全局变量用于测试fork/vfork的内存共享特性
 * - 定义一个全局变量（如计数器）
 * - 在fork/vfork后，父子进程分别修改该变量
 * - 通过输出观察值的变化，验证fork为独立内存、vfork为共享内存
 */
int shared_counter = 0;

int main(void) {
    int my_pid = getpid();
    int my_ppid = getppid();
    printf("User App Started. My PID: %d, PPID: %d\r\n", my_pid, my_ppid);

    /* 保留父子进程共享变量验证，仅首个主实例执行，避免exec重启后反复冗余 */
    if (my_pid == 1) {
        printf("\r\nTesting fork memory isolation...\r\n");
        shared_counter = 100;
        int fmem = fork();
        if (fmem == 0) {
            printf("fork child (PID %d) before modify: %d\r\n", getpid(), shared_counter);
            shared_counter += 1000;
            printf("fork child (PID %d) after modify: %d\r\n", getpid(), shared_counter);
            exit();
        } else if (fmem > 0) {
            sleep(1);
            printf("fork parent (PID %d) sees value: %d (expect 100)\r\n", getpid(), shared_counter);
        } else {
            printf("fork memory test skipped: fork failed.\r\n");
        }

        printf("\r\nTesting vfork memory sharing...\r\n");
        shared_counter = 200;
        int vmem = vfork();
        if (vmem == 0) {
            printf("vfork child (PID %d) before modify: %d\r\n", getpid(), shared_counter);
            shared_counter += 2000;
            printf("vfork child (PID %d) after modify: %d\r\n", getpid(), shared_counter);
            exit();
        } else if (vmem > 0) {
            sleep(1);
            printf("vfork parent (PID %d) sees value: %d (expect 2200)\r\n", getpid(), shared_counter);
        } else {
            printf("vfork memory test skipped: vfork failed.\r\n");
        }
    }

    /* 以下流程按README示例风格：exec后新实例也会继续执行 */
    printf("\r\nTesting Fork Failure (exhausting slots)...\r\n");
    for (int i = 0; i < 6; i++) {
        int pid = fork();
        if (pid == 0) {
            int child_pid = getpid();
            printf("Child PID %d created. Sleeping...\r\n", child_pid);
            sleep(10);
            printf("Child PID %d exiting.\r\n", child_pid);
            exit();
        } else if (pid == -1) {
            printf("Fork failed as expected! (No more slots available)\r\n");
        } else {
            printf("Parent created child with PID %d\r\n", pid);
        }
    }

    sleep(12);

    printf("\r\nTesting vfork concurrency control...\r\n");
    int v1 = vfork();
    if (v1 == 0) {
        printf("vfork Child 1 (PID %d) created, sleeping before exec...\r\n", getpid());
        sleep(3);
        printf("vfork Child 1 calls exec to reload app...\r\n");
        exec(129, 128);
    } else if (v1 > 0) {
        printf("Parent: Created vfork child 1 (PID %d), attempting vfork 2...\r\n", v1);
        int v2 = vfork();
        if (v2 == -1) {
            printf("Parent: vfork 2 failed as expected (Concurrency Control Works!)\r\n");
        } else if (v2 == 0) {
            printf("vfork Child 2 (PID %d) - ERROR: This should not happen!\r\n", getpid());
            exit();
        } else {
            printf("Parent: vfork 2 succeeded with PID %d - ERROR: Concurrency control failed!\r\n", v2);
        }
    }

    printf("Parent PID %d going into main loop.\r\n", getpid());

    while (1) {
        int h, m, s;
        now(&h, &m, &s);
        printf("Parent (PID %d) Time: %2d:%2d:%2d\r\n", getpid(), h, m, s);
        sleep(5);
    }
}