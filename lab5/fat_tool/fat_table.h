#ifndef FAT_TABLE_H
#define FAT_TABLE_H

#include "fat_types.h"

// Public prototypes
int get_fat_entry(FILE *fp, int cluster);
void set_fat_entry(FILE *fp, int cluster, int value);
int allocate_cluster(FILE *fp);

#endif // FAT_TABLE_H
