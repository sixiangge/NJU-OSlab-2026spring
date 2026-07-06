#ifndef FAT_IO_H
#define FAT_IO_H

#include "fat_types.h"

// Public prototypes
int read_sector(FILE *fp, int sector, unsigned char *buffer);
int write_sector(FILE *fp, int sector, const unsigned char *buffer);
int read_cluster(FILE *fp, int cluster, unsigned char *buffer);
int write_cluster(FILE *fp, int cluster, const unsigned char *buffer);

#endif // FAT_IO_H
