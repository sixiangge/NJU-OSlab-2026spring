#include <stdio.h>
#include <errno.h>
#include "fat_io.h"

// Defined in fat.h: #define SECTOR_SIZE 512
// Defined in fat.h: #define CLUSTER_TO_SECTOR(cluster) (DATA_START_SECTOR + (cluster) - 2)

int read_sector(FILE *fp, int sector, unsigned char *buffer) {
    if (fseek(fp, (long)sector * SECTOR_SIZE, SEEK_SET) != 0) return -1;
    if (fread(buffer, 1, SECTOR_SIZE, fp) != SECTOR_SIZE) return -1;
    return 0;
}

int write_sector(FILE *fp, int sector, const unsigned char *buffer) {
    if (fseek(fp, (long)sector * SECTOR_SIZE, SEEK_SET) != 0) return -1;
    if (fwrite(buffer, 1, SECTOR_SIZE, fp) != SECTOR_SIZE) return -1;
    return 0;
}

int read_cluster(FILE *fp, int cluster, unsigned char *buffer) {
    if (cluster < 2 || cluster >= MAX_CLUSTERS + 2) return -1;
    return read_sector(fp, CLUSTER_TO_SECTOR(cluster), buffer);
}

int write_cluster(FILE *fp, int cluster, const unsigned char *buffer) {
    if (cluster < 2 || cluster >= MAX_CLUSTERS + 2) return -1;
    return write_sector(fp, CLUSTER_TO_SECTOR(cluster), buffer);
}
