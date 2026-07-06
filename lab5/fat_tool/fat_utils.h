#ifndef FAT_UTILS_H
#define FAT_UTILS_H

#include "fat_types.h"

// Public prototypes
#ifndef strdup
char *strdup(const char *s);
#endif

void format_name(const char *filename, uint8_t *fat_name);
uint16_t encode_time();
uint16_t encode_date();
int find_entry(FILE *fp, const char *path, DirEntry *entry_out, int *parent_cluster_out, int *entry_offset_out, int *entry_cluster_out);
int get_free_entry_offset(FILE *fp, int dir_cluster, DirEntry *empty_entry_out, int *entry_offset_out, int *entry_cluster_out);

#endif // FAT_UTILS_H
