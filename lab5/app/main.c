#include "myos.h"

int main(void) {
    if (getpid() == 1) {
        printf("--- Lab 5 File System Verification Suite ---\n");

        // --- Shared File Pointer Test ---
        printf("\nTesting Shared File Pointer with /data/test.txt:\n");
        int fd = -1;
        // TODO: Open /data/test.txt once before fork(). The parent and child
        // should share this same fd after fork, so both processes refer to the
        // same kernel FileDescription and therefore the same file offset.
        fd = open("/data/test.txt");
        if (fd >= 0) {
            printf("Parent opened /data/test.txt (fd=%d)\n", fd);
            
            if (fork() == 0) {
                // Child
                char buf[11];
                sleep(1); // Wait for parent to read first
                int n = 0;
                // TODO: Read 10 bytes from the inherited shared fd. Because the
                // parent reads the first 10 bytes before the child runs, this read
                // should continue from the shared offset and return KLMNOPQRST.
                n = read(fd, buf, 10);
                buf[n] = '\0';
                printf("Child read str: %s (Expected: KLMNOPQRST)\n", buf);
                exit();
            } else {
                // Parent
                char buf[11];
                int n = 0;
                // TODO: Read the first 10 bytes from the shared fd. This should
                // advance the shared file offset seen later by the child process.
                n = read(fd, buf, 10);
                buf[n] = '\0';
                printf("Parent read str: %s (Expected: ABCDEFGHIJ)\n", buf);
                wait();
                // TODO: Close the shared fd in the parent after the child exits.
                close(fd);
            }
        } else {
            printf("Failed to open /data/test.txt\n");
        }

        // --- Independent File Pointer Test ---
        printf("\nTesting Independent File Pointers with /data/test.txt:\n");
        if (fork() == 0) {
            // Child: opens separately
            int fd_child = -1;
            // TODO: Open /data/test.txt separately in the child. This should
            // create a different FileDescription from the parent's open, so the
            // child starts reading from offset 0 independently.
            fd_child = open("/data/test.txt");
            if (fd_child >= 0) {
                char buf[11];
                int n = 0;
                // TODO: Read 10 bytes from the child's independent fd. Since this
                // fd was opened separately, the expected result starts at ABCDEFGHIJ.
                n = read(fd_child, buf, 10);
                buf[n] = '\0';
                printf("Child (independent) read str: %s (Expected: ABCDEFGHIJ)\n", buf);
                // TODO: Close the child-owned independent fd.
                close(fd_child);
            }
            exit();
        } else {
            // Parent: opens separately
            int fd_parent = -1;
            // TODO: Open /data/test.txt separately in the parent. This should not
            // share the child process's file offset because it is a separate open.
            fd_parent = open("/data/test.txt");
            if (fd_parent >= 0) {
                char buf[11];
                int n = 0;
                // TODO: Read 10 bytes from the parent's independent fd. It should
                // also start at offset 0 and produce ABCDEFGHIJ.
                n = read(fd_parent, buf, 10);
                buf[n] = '\0';
                printf("Parent (independent) read str: %s (Expected: ABCDEFGHIJ)\n", buf);
                // TODO: Close the parent-owned independent fd.
                close(fd_parent);
            }
            wait();
        }
        
        printf("\n--- Lab 5 Verification Complete ---\n");
    }

    // All processes enter idle loop
    while(1) {
        sleep(10);
    }
}
