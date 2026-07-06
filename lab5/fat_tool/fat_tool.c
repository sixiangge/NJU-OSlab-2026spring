#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

// New Module Includes
#include "fat_types.h"
#include "fat.h"
#include "fat_io.h"
#include "fat_table.h"
#include "fat_utils.h"


/**
 * @brief Helper for confirmation prompt.
 */
static int fat_confirm(const char *msg) {
    printf("%s [y/N]: ", msg);
    fflush(stdout);
    char buf[64];
    if (fgets(buf, sizeof(buf), stdin) == NULL) return 0;
    if (tolower((unsigned char)buf[0]) == 'y') return 1;
    return 0;
}

// ====================================================================
// SECTION 1: FAT Tool Implementations
// ====================================================================

/**
 * @brief Creates a FAT12 disk image file (1.44MB).
 */
int fat_create(const char *image_path) {
    printf("Creating FAT12 disk image: %s (1.44MB)...\n", image_path);

    FILE *fp = fopen(image_path, "w+b");
    if (!fp) {
        perror("Error creating image file");
        return -1;
    }

    // 1. Initialize entire file with zeros
    unsigned char zero_sector[SECTOR_SIZE] = {0};
    for (int i = 0; i < TOTAL_SECTORS; i++) {
        if (write_sector(fp, i, zero_sector) != 0) {
            fclose(fp);
            return -1;
        }
    }

    // 2. Setup BPB (Sector 0)
    BPB bpb = {0};
    memcpy(bpb.BS_JmpBoot, "\xeb\x3c\x90", 3);
    memcpy(bpb.BS_OEMName, "MSWIN4.1", 8);
    bpb.BPB_BytsPerSec = SECTOR_SIZE;
    bpb.BPB_SecPerClus = 1;
    bpb.BPB_ResvdSecCnt = RESERVED_SECTORS;
    bpb.BPB_NumFATs = NUM_FATS;
    bpb.BPB_RootEntCnt = ROOT_ENTRIES;
    bpb.BPB_TotSec16 = TOTAL_SECTORS;
    bpb.BPB_Media = 0xF0; 
    bpb.BPB_FATSz16 = FAT_SIZE_SECTORS;
    bpb.BPB_SecPerTrk = 18;
    bpb.BPB_NumHeads = 2;
    bpb.BS_DrvNum = 0x80;
    bpb.BS_BootSig = 0x29;
    bpb.BS_VolID = (uint32_t)time(NULL);
    memcpy(bpb.BS_VolLab, "NO NAME    ", 11);
    memcpy(bpb.BS_FilSysType, "FAT12   ", 8);
    bpb.BS_SigWord = 0xAA55;

    if (fseek(fp, 0, SEEK_SET) != 0 || fwrite(&bpb, 1, SECTOR_SIZE, fp) != SECTOR_SIZE) {
        perror("Error writing BPB");
        fclose(fp);
        return -1;
    }

    // 3. Initialize FAT Tables (FAT1 and FAT2)
    unsigned char fat_sector_0[SECTOR_SIZE] = {0};
    fat_sector_0[0] = 0xF0; 
    fat_sector_0[1] = 0xFF; 
    fat_sector_0[2] = 0xFF; 
    
    if (write_sector(fp, FAT1_START_SECTOR, fat_sector_0) != 0 ||
        write_sector(fp, FAT2_START_SECTOR, fat_sector_0) != 0) {
        fclose(fp);
        return -1;
    }

    printf("Image created successfully.\n");
    fclose(fp);
    return 0;
}

/**
 * @brief Lists the contents of a directory (Supports root and level 1 subdirs).
 */
int fat_ls(const char *image_path, const char *dir_path) {
    // No change to initial print for now, it's descriptive.
    FILE *fp = fopen(image_path, "r+b");
    if (!fp) {
        perror("Error opening image file");
        return -1;
    }

    DirEntry target_entry;
    int entry_offset, parent_cluster, entry_cluster;
    int result = find_entry(fp, dir_path, &target_entry, &parent_cluster, &entry_offset, &entry_cluster);
    
    if (result == -1) {
        fprintf(stderr, "Error: Path not found: %s\n", dir_path);
        fclose(fp);
        return -1;
    }
    
    int start_cluster = 0;
    if (result == 0) {
        if (!(target_entry.DIR_Attr & ATTR_DIRECTORY)) {
            fprintf(stderr, "Error: %s is a file, not a directory.\n", dir_path);
            fclose(fp);
            return -1;
        }
        start_cluster = target_entry.DIR_FstClusLO;
    } 

    printf("Attribute   Size      First Cluster  Name\n");
    printf("---------------------------------------------------\n");
    
    unsigned char sector_buffer[SECTOR_SIZE];
    
    if (start_cluster == 0) {
        // Root Directory
        for (size_t sector = 0; sector < ROOT_DIR_SECTORS; sector++) {
            if (read_sector(fp, ROOT_DIR_START_SECTOR + sector, sector_buffer) != 0) break;
            
            for (size_t i = 0; i < SECTOR_SIZE / sizeof(DirEntry); i++) {
                DirEntry *d_entry = (DirEntry*)(sector_buffer + i * sizeof(DirEntry));
                
                if (d_entry->DIR_Name[0] == 0x00) goto ls_end; 
                if (d_entry->DIR_Name[0] == 0xE5 || d_entry->DIR_Attr == ATTR_LFN) continue;

                char name[13] = {0}; 
                snprintf(name, 9, "%.8s", d_entry->DIR_Name);
                if (!(d_entry->DIR_Attr & ATTR_DIRECTORY)) {
                    snprintf(name + 8, 4, "%.3s", d_entry->DIR_Name + 8);
                } else {
                    for(int j = 7; j >= 0; j--) {
                        if (name[j] == ' ') name[j] = '\0';
                        else break;
                    }
                }
                
                printf("%-10s %-9u %-14d %s\n",
                       (d_entry->DIR_Attr & ATTR_DIRECTORY) ? "DIR" : "FILE",
                       d_entry->DIR_FileSize,
                       d_entry->DIR_FstClusLO,
                       name);
            }
        }
    } else {
        // Data Cluster Directory
        if (read_cluster(fp, start_cluster, sector_buffer) != 0) {
            fprintf(stderr, "Error reading directory cluster %d.\n", start_cluster);
            goto ls_end;
        }

        for (size_t i = 0; i < SECTOR_SIZE / sizeof(DirEntry); i++) {
            DirEntry *d_entry = (DirEntry*)(sector_buffer + i * sizeof(DirEntry));
            
            if (d_entry->DIR_Name[0] == 0x00) goto ls_end;
            if (d_entry->DIR_Name[0] == 0xE5 || d_entry->DIR_Attr == ATTR_LFN) continue;

            char name[13] = {0};
            snprintf(name, 9, "%.8s", d_entry->DIR_Name);
            
            if (i < 2 && (d_entry->DIR_Attr & ATTR_DIRECTORY) && d_entry->DIR_Name[0] == '.') {
                 if (d_entry->DIR_Name[1] == '.') strcpy(name, "..");
                 else strcpy(name, ".");
            } else if (!(d_entry->DIR_Attr & ATTR_DIRECTORY)) {
                snprintf(name + 8, 4, "%.3s", d_entry->DIR_Name + 8);
            } else {
                for(int j = 7; j >= 0; j--) {
                    if (name[j] == ' ') name[j] = '\0';
                    else break;
                }
            }
            
            printf("%-10s %-9u %-14d %s\n",
                   (d_entry->DIR_Attr & ATTR_DIRECTORY) ? "DIR" : "FILE",
                   d_entry->DIR_FileSize,
                   d_entry->DIR_FstClusLO,
                   name);
        }
    }
    
ls_end:
    printf("---------------------------------------------------\n");
    fclose(fp);
    return 0;
}

/**
 * @brief Creates a directory in the root.
 */
int fat_mkdir(const char *image_path, const char *dir_path) {
    printf("Creating directory %s in %s...\n", dir_path, image_path);

    FILE *fp = fopen(image_path, "r+b");
    if (!fp) {
        perror("Error opening image file");
        return -1;
    }

    char *path_copy = strdup(dir_path);
    char *token = strtok(path_copy, "/");
    char *dir_name = token;
    
    if (token == NULL || strtok(NULL, "/") != NULL) {
        fprintf(stderr, "Error: Invalid path. New directory must be /DIRNAME.\n");
        free(path_copy);
        fclose(fp);
        return -1;
    }
    
    if (find_entry(fp, dir_path, NULL, NULL, NULL, NULL) == 0) {
        fprintf(stderr, "Error: Directory already exists.\n");
        free(path_copy);
        fclose(fp);
        return -1;
    }
    
    DirEntry new_entry_placeholder = {0};
    int entry_offset, entry_cluster_unused;
    int free_entry_found = get_free_entry_offset(fp, 0, &new_entry_placeholder, &entry_offset, &entry_cluster_unused);

    if (free_entry_found == -1) {
        fprintf(stderr, "Error: Root directory is full.\n");
        free(path_copy);
        fclose(fp);
        return -1;
    }

    int new_cluster = allocate_cluster(fp);
    if (new_cluster == -1) {
        fprintf(stderr, "Error: Disk is full.\n");
        free(path_copy);
        fclose(fp);
        return -1;
    }

    unsigned char cluster_buffer[SECTOR_SIZE] = {0};
    DirEntry *dot_entry = (DirEntry*)cluster_buffer;
    DirEntry *dotdot_entry = (DirEntry*)(cluster_buffer + sizeof(DirEntry));

    uint16_t current_time = encode_time();
    uint16_t current_date = encode_date();

    memcpy(dot_entry->DIR_Name, ".          ", 11);
    dot_entry->DIR_Attr = ATTR_DIRECTORY;
    dot_entry->DIR_CrtTime = current_time;
    dot_entry->DIR_CrtDate = current_date;
    dot_entry->DIR_WrtTime = current_time;
    dot_entry->DIR_WrtDate = current_date;
    dot_entry->DIR_FstClusLO = (uint16_t)new_cluster;
    dot_entry->DIR_FileSize = 0; 

    memcpy(dotdot_entry->DIR_Name, "..         ", 11);
    dotdot_entry->DIR_Attr = ATTR_DIRECTORY;
    dotdot_entry->DIR_CrtTime = current_time;
    dotdot_entry->DIR_CrtDate = current_date;
    dotdot_entry->DIR_WrtTime = current_time;
    dotdot_entry->DIR_WrtDate = current_date;
    dotdot_entry->DIR_FstClusLO = 0; 
    dotdot_entry->DIR_FileSize = 0;

    if (write_cluster(fp, new_cluster, cluster_buffer) != 0) {
        fprintf(stderr, "Error writing new directory cluster.\n");
        free(path_copy);
        fclose(fp);
        return -1;
    }
    
    set_fat_entry(fp, new_cluster, FAT_EOF);

    DirEntry new_entry = {0};
    format_name(dir_name, new_entry.DIR_Name);
    new_entry.DIR_Attr = ATTR_DIRECTORY;
    new_entry.DIR_CrtTime = current_time;
    new_entry.DIR_CrtDate = current_date;
    new_entry.DIR_WrtTime = current_time;
    new_entry.DIR_WrtDate = current_date;
    new_entry.DIR_FstClusLO = (uint16_t)new_cluster;
    new_entry.DIR_FileSize = 0;
    
    long dir_entry_pos = (long)ROOT_DIR_START_SECTOR * SECTOR_SIZE + (long)entry_offset * sizeof(DirEntry);
    unsigned char sector_buffer_dir[SECTOR_SIZE];
    int sector_to_write = dir_entry_pos / SECTOR_SIZE;
    int offset_in_sector = dir_entry_pos % SECTOR_SIZE;
    
    if (read_sector(fp, sector_to_write, sector_buffer_dir) != 0) {
        free(path_copy);
        fclose(fp);
        return -1;
    }

    memcpy(sector_buffer_dir + offset_in_sector, &new_entry, sizeof(DirEntry));
    
    if (write_sector(fp, sector_to_write, sector_buffer_dir) != 0) {
        perror("Error writing directory sector");
        free(path_copy);
        fclose(fp);
        return -1;
    }

    printf("Successfully created directory %s.\n", dir_name);
    free(path_copy);
    fclose(fp);
    return 0;
}

/**
 * @brief Imports an external file.
 */
int fat_import(const char *image_path, const char *src_path, const char *dest_path) {
    printf("Importing %s into %s...\n", src_path, dest_path);

    FILE *fp = fopen(image_path, "r+b");
    if (!fp) {
        perror("Error opening image file");
        return -1;
    }
    FILE *src_fp = fopen(src_path, "rb");
    if (!src_fp) {
        perror("Error opening source file");
        fclose(fp);
        return -1;
    }

    struct stat st;
    if (stat(src_path, &st) != 0) {
        perror("Error getting source file size");
        fclose(fp);
        fclose(src_fp);
        return -1;
    }
    uint32_t file_size = (uint32_t)st.st_size;

    char *dest_path_copy = strdup(dest_path);
    char *dir_name = dest_path_copy;
    char *file_name = NULL;
    
    char *last_slash = strrchr(dir_name, '/');
    if (last_slash) {
        *last_slash = '\0'; 
        file_name = last_slash + 1;
        if (strlen(dir_name) == 0) dir_name = "/";
    } else {
        fprintf(stderr, "Error: Destination path must contain a filename.\n");
        free(dest_path_copy);
        fclose(fp);
        fclose(src_fp);
        return -1;
    }
    
    if (strlen(file_name) == 0) {
        fprintf(stderr, "Error: Invalid destination file name.\n");
        free(dest_path_copy);
        fclose(fp);
        fclose(src_fp);
        return -1;
    }

    DirEntry parent_entry;
    int entry_offset, parent_cluster, entry_cluster;
    
    int result = find_entry(fp, dir_name, &parent_entry, &parent_cluster, NULL, NULL);
    
    if (result == -1 || (result == 0 && !(parent_entry.DIR_Attr & ATTR_DIRECTORY))) {
        fprintf(stderr, "Error: Destination directory not found or is a file.\n");
        free(dest_path_copy);
        fclose(fp);
        fclose(src_fp);
        return -1;
    }
    
    DirEntry conflict_entry;
    if (find_entry(fp, dest_path, &conflict_entry, NULL, NULL, NULL) == 0) {
        fprintf(stderr, "Error: File already exists at %s.\n", dest_path);
        free(dest_path_copy);
        fclose(fp);
        fclose(src_fp);
        return -1;
    }
    
    int dir_start_cluster = (result == 1) ? 0 : parent_entry.DIR_FstClusLO;

    DirEntry new_entry_placeholder = {0};
    int free_entry_found = get_free_entry_offset(fp, dir_start_cluster, &new_entry_placeholder, &entry_offset, &entry_cluster);

    if (free_entry_found == -1) {
        fprintf(stderr, "Error: Destination directory full.\n");
        free(dest_path_copy);
        fclose(fp);
        fclose(src_fp);
        return -1;
    }
    
    int current_cluster = -1, start_cluster = -1, prev_cluster = -1;
    unsigned char sector_buffer[SECTOR_SIZE];
    size_t bytes_read;
    
    printf("Allocating clusters...\n");
    
    do {
        current_cluster = allocate_cluster(fp);
        if (current_cluster == -1) {
            fprintf(stderr, "Error: Disk is full.\n");
            goto import_error;
        }
        
        // Mark as EOF immediately so allocate_cluster won't find it again
        set_fat_entry(fp, current_cluster, FAT_EOF);

        if (start_cluster == -1) start_cluster = current_cluster;
        if (prev_cluster != -1) set_fat_entry(fp, prev_cluster, current_cluster);

        bytes_read = fread(sector_buffer, 1, SECTOR_SIZE, src_fp);
        if (write_cluster(fp, current_cluster, sector_buffer) != 0) goto import_error;
        
        prev_cluster = current_cluster;
        memset(sector_buffer, 0, SECTOR_SIZE);

    } while (bytes_read == SECTOR_SIZE);

    if (prev_cluster != -1) set_fat_entry(fp, prev_cluster, FAT_EOF);
    else start_cluster = 0;
    
    DirEntry new_entry = {0};
    format_name(file_name, new_entry.DIR_Name);
    new_entry.DIR_Attr = ATTR_ARCHIVE;
    uint16_t current_time = encode_time();
    uint16_t current_date = encode_date();
    new_entry.DIR_CrtTime = current_time;
    new_entry.DIR_CrtDate = current_date;
    new_entry.DIR_WrtTime = current_time;
    new_entry.DIR_WrtDate = current_date;
    new_entry.DIR_FstClusLO = (uint16_t)start_cluster;
    new_entry.DIR_FileSize = file_size;

    unsigned char sector_buffer_dir[SECTOR_SIZE];
    if (dir_start_cluster == 0) {
        long dir_entry_pos = (long)ROOT_DIR_START_SECTOR * SECTOR_SIZE + (long)entry_offset * sizeof(DirEntry);
        int sector_to_write = dir_entry_pos / SECTOR_SIZE;
        int offset_in_sector = dir_entry_pos % SECTOR_SIZE;
        if (read_sector(fp, sector_to_write, sector_buffer_dir) != 0) goto import_error;
        memcpy(sector_buffer_dir + offset_in_sector, &new_entry, sizeof(DirEntry));
        if (write_sector(fp, sector_to_write, sector_buffer_dir) != 0) goto import_error;
    } else {
        if (read_cluster(fp, entry_cluster, sector_buffer_dir) != 0) goto import_error;
        memcpy(sector_buffer_dir + entry_offset * sizeof(DirEntry), &new_entry, sizeof(DirEntry));
        if (write_cluster(fp, entry_cluster, sector_buffer_dir) != 0) goto import_error;
    }
    
    printf("Successfully imported %s.\n", file_name);
    free(dest_path_copy);
    fclose(fp);
    fclose(src_fp);
    return 0;

import_error:
    fprintf(stderr, "Import failed.\n");
    free(dest_path_copy);
    fclose(fp);
    fclose(src_fp);
    return -1;
}

/**
 * @brief Exports a file from the disk image to the host file system.
 */
int fat_export(const char *image_path, const char *src_path, const char *dest_path) {
    printf("Exporting %s from %s to %s...\n", src_path, image_path, dest_path);

    FILE *fp = fopen(image_path, "r+b");
    if (!fp) {
        perror("Error opening image file");
        return -1;
    }
    FILE *dest_fp = fopen(dest_path, "wb"); 
    if (!dest_fp) {
        perror("Error creating destination file");
        fclose(fp);
        return -1;
    }

    // 1. Find source file entry
    DirEntry target_entry;
    int parent_cluster, entry_offset, entry_cluster; 

    char *target_file_name = strdup(src_path);
    int result = find_entry(fp, target_file_name, &target_entry, &parent_cluster, &entry_offset, &entry_cluster);
    free(target_file_name);

    if (result != 0) {
        fprintf(stderr, "Error: Source file not found or path is invalid.\n");
        fclose(fp);
        fclose(dest_fp);
        return -1;
    }
    
    // Check if it's a file
    if (target_entry.DIR_Attr & ATTR_DIRECTORY) {
        fprintf(stderr, "Error: Cannot export a directory.\n");
        fclose(fp);
        fclose(dest_fp);
        return -1;
    }

    uint32_t file_size = target_entry.DIR_FileSize;
    int current_cluster = target_entry.DIR_FstClusLO;
    unsigned char cluster_buffer[SECTOR_SIZE];
    uint32_t bytes_exported = 0;

    printf("File Size: %u bytes. Starting Cluster: %d\n", file_size, current_cluster);

    // 2. Handle 0-byte file case
    if (file_size == 0) {
        printf("Successfully exported 0 bytes.\n");
        fclose(fp);
        fclose(dest_fp);
        return 0;
    }
    
    // 3. Traverse cluster chain and write data
    while (current_cluster != FAT_EOF && current_cluster != FAT_FREE && current_cluster != FAT_BAD_CLUSTER) {
        if (read_cluster(fp, current_cluster, cluster_buffer) != 0) {
            fprintf(stderr, "Error reading cluster %d during export.\n", current_cluster);
            goto export_error;
        }

        uint32_t bytes_remaining = file_size - bytes_exported;
        size_t bytes_to_write = (bytes_remaining < SECTOR_SIZE) ? (size_t)bytes_remaining : SECTOR_SIZE;

        if (fwrite(cluster_buffer, 1, bytes_to_write, dest_fp) != bytes_to_write) {
            perror("Error writing to destination file");
            goto export_error;
        }

        bytes_exported += bytes_to_write;
        
        if (bytes_exported >= file_size) break;

        current_cluster = get_fat_entry(fp, current_cluster);
    }
    
    printf("Successfully exported %u bytes to %s.\n", bytes_exported, dest_path);

    fclose(fp);
    fclose(dest_fp);
    return 0;

export_error:
    fprintf(stderr, "Export failed.\n");
    fclose(fp);
    fclose(dest_fp);
    return -1;
}

// Helper to recursively delete contents
static int fat_recursive_delete_contents(FILE *fp, int dir_cluster) {
    if (dir_cluster == 0) return 0;

    unsigned char cluster_buffer[SECTOR_SIZE];
    if (read_cluster(fp, dir_cluster, cluster_buffer) != 0) return -1;

    for (size_t i = 0; i < SECTOR_SIZE / sizeof(DirEntry); i++) {
        DirEntry *d_entry = (DirEntry*)(cluster_buffer + i * sizeof(DirEntry));

        if (d_entry->DIR_Name[0] == 0x00) break;
        if (d_entry->DIR_Name[0] == 0xE5 || d_entry->DIR_Attr == ATTR_LFN) continue;
        if (d_entry->DIR_Name[0] == '.' && (d_entry->DIR_Name[1] == ' ' || d_entry->DIR_Name[1] == '.')) continue;
        
        int entry_start_cluster = d_entry->DIR_FstClusLO;

        if (d_entry->DIR_Attr & ATTR_DIRECTORY) {
            if (fat_recursive_delete_contents(fp, entry_start_cluster) != 0) return -1; 
        }

        if (entry_start_cluster != 0) {
            int current_c = entry_start_cluster;
            int next_c;
            while (current_c != FAT_EOF && current_c != FAT_FREE && current_c != FAT_BAD_CLUSTER) {
                next_c = get_fat_entry(fp, current_c);
                set_fat_entry(fp, current_c, FAT_FREE);
                current_c = next_c;
            }
        }
        d_entry->DIR_Name[0] = 0xE5;
    }
    
    if (write_cluster(fp, dir_cluster, cluster_buffer) != 0) return -1;
    return 0;
}

/**
 * @brief Deletes a file or directory (supports root and level 1 subdirs, recursive).
 */
int fat_rm(const char *image_path, const char *target_path) {
    printf("Removing %s from %s...\n", target_path, image_path);
    FILE *fp = fopen(image_path, "r+b");
    if (!fp) {
        perror("Error opening image file");
        return -1;
    }

    DirEntry target_entry;
    int parent_cluster, entry_offset, entry_cluster; 

    char *target_file_name = strdup(target_path);
    int result = find_entry(fp, target_file_name, &target_entry, &parent_cluster, &entry_offset, &entry_cluster);
    free(target_file_name);

    if (result == -1) {
        fprintf(stderr, "Error: Target not found: %s\n", target_path);
        fclose(fp);
        return -1;
    }
    
    if (result == 1) {
        fprintf(stderr, "Error: Cannot delete the root directory (/).\n");
        fclose(fp);
        return -1;
    }
    
    if (target_entry.DIR_Attr & ATTR_DIRECTORY) {
        char msg[256];
        snprintf(msg, sizeof(msg), "WARNING: You are about to delete directory %s and all its contents. Proceed?", target_path);
        if (!fat_confirm(msg)) {
            printf("Deletion cancelled.\n");
            fclose(fp);
            return 0;
        }
        int dir_start_cluster = target_entry.DIR_FstClusLO;
        if (dir_start_cluster != 0) {
            if (fat_recursive_delete_contents(fp, dir_start_cluster) != 0) {
                fprintf(stderr, "Error: Failed to recursively delete directory contents.\n");
                fclose(fp);
                return -1;
            }
        }
    }

    int current_cluster = target_entry.DIR_FstClusLO;
    if (current_cluster != 0) {
        int next_cluster;
        while (current_cluster != FAT_EOF && current_cluster != FAT_FREE) {
            next_cluster = get_fat_entry(fp, current_cluster);
            set_fat_entry(fp, current_cluster, FAT_FREE);
            current_cluster = next_cluster;
        }
    }
    
    unsigned char sector_buffer[SECTOR_SIZE];
    if (parent_cluster == 0) {
        long dir_entry_pos = (long)ROOT_DIR_START_SECTOR * SECTOR_SIZE + (long)entry_offset * sizeof(DirEntry);
        int sector_to_write = dir_entry_pos / SECTOR_SIZE;
        int offset_in_sector = dir_entry_pos % SECTOR_SIZE;
        if (read_sector(fp, sector_to_write, sector_buffer) != 0) { fclose(fp); return -1; }
        sector_buffer[offset_in_sector] = 0xE5; 
        if (write_sector(fp, sector_to_write, sector_buffer) != 0) { fclose(fp); return -1; }
    } else {
        if (read_cluster(fp, entry_cluster, sector_buffer) != 0) { fclose(fp); return -1; }
        DirEntry *d_entry = (DirEntry*)(sector_buffer + entry_offset * sizeof(DirEntry));
        d_entry->DIR_Name[0] = 0xE5;
        if (write_cluster(fp, entry_cluster, sector_buffer) != 0) { fclose(fp); return -1; }
    }

    printf("Successfully removed %s.\n", target_path);
    fclose(fp);
    return 0;
}

// ====================================================================
// SECTION 2: Main Entry Point
// ====================================================================

void usage(const char *prog_name) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s create <image_path>\n", prog_name);
    fprintf(stderr, "  %s mkdir <image_path> <dir_path>\n", prog_name);
    fprintf(stderr, "  %s ls <image_path> <dir_path>\n", prog_name);
    fprintf(stderr, "  %s import <image_path> <src_path> <dest_path>\n", prog_name);
    fprintf(stderr, "  %s export <image_path> <src_path> <dest_path>\n", prog_name);
    fprintf(stderr, "  %s rm <image_path> <target_path>\n", prog_name);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "create") == 0) {
        if (argc != 3) { usage(argv[0]); return 1; }
        return fat_create(argv[2]);
    } else if (strcmp(argv[1], "mkdir") == 0) {
        if (argc != 4) { usage(argv[0]); return 1; }
        return fat_mkdir(argv[2], argv[3]);
    } else if (strcmp(argv[1], "ls") == 0) {
        if (argc != 4) { usage(argv[0]); return 1; }
        return fat_ls(argv[2], argv[3]);
    } else if (strcmp(argv[1], "import") == 0) {
        if (argc != 5) { usage(argv[0]); return 1; }
        return fat_import(argv[2], argv[3], argv[4]);
    } else if (strcmp(argv[1], "export") == 0) {
        if (argc != 5) { usage(argv[0]); return 1; }
        return fat_export(argv[2], argv[3], argv[4]);
    } else if (strcmp(argv[1], "rm") == 0) {
        if (argc != 4) { usage(argv[0]); return 1; }
        return fat_rm(argv[2], argv[3]);
    } else {
        usage(argv[0]);
        return 1;
    }
    return 0;
}
