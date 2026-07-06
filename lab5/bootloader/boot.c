#include "boot.h"

#define KERNEL_START 0x10000
#define FAT_BUF      0x8000
#define ROOT_START   19
#define FAT_START    1
#define DATA_START   33

typedef struct {
    unsigned char name[11];
    unsigned char attr;
    unsigned char reserved[10];
    unsigned short wrtTime;
    unsigned short wrtDate;
    unsigned short firstCluster;
    unsigned int fileSize;
} __attribute__((packed)) DirEntry;

void bootMain(void) {
    DirEntry *dir = (DirEntry *)KERNEL_START;
    int firstCluster = -1;

    for (int i = 0; i < 14; i++) {
        readSect(dir, ROOT_START + i);
        for (int j = 0; j < 16; j++) {
            if (dir[j].name[0] == 'K' && dir[j].name[1] == 'E' && dir[j].name[2] == 'R' &&
                dir[j].name[8] == 'B' && dir[j].name[9] == 'I' && dir[j].name[10] == 'N') {
                firstCluster = dir[j].firstCluster;
                break;
            }
        }
        if (firstCluster != -1) break;
    }

    if (firstCluster == -1) while(1);

    for (int i = 0; i < 9; i++) readSect((void *)(FAT_BUF + i * 512), FAT_START + i);

    unsigned char *dst = (unsigned char *)KERNEL_START;
    int cluster = firstCluster;
    while (cluster >= 2 && cluster < 0xFF8) {
        // TODO: Load the current FAT cluster into dst, advance dst by one
        // sector, then read the 12-bit FAT entry for this cluster from FAT_BUF
        // and update cluster to the next cluster in the chain.
        readSect(dst, DATA_START + cluster - 2);
        dst += 512;

        int offset = cluster + (cluster / 2);
        unsigned short entry = *(unsigned short *)(FAT_BUF + offset);
        if (cluster & 1) cluster = entry >> 4;
        else cluster = entry & 0x0FFF;
    }

    ((void(*)(void))KERNEL_START)();
}
