#ifndef FAT_H
#define FAT_H

#include "fat_types.h"

// Public API prototypes
int fat_create(const char *image_path);
int fat_ls(const char *image_path, const char *dir_path);
int fat_mkdir(const char *image_path, const char *dir_path);
int fat_import(const char *image_path, const char *src_path, const char *dest_path);
int fat_export(const char *image_path, const char *src_path, const char *dest_path);
int fat_rm(const char *image_path, const char *target_path);

#endif // FAT_H
