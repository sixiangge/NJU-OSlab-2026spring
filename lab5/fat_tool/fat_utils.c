#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "fat_utils.h"
#include "fat_io.h"     // For read_sector, read_cluster
#include "fat_table.h"  // For get_fat_entry

#ifndef strdup

char *strdup(const char *s) {
	if (!s) return NULL;
	size_t len = strlen(s) + 1;
	char *copy = malloc(len);
	if (copy) memcpy(copy, s, len);
	return copy;
}
#endif


/**
 * @brief Converts a long filename into the 8.3 FAT format.
 */
void format_name(const char *filename, uint8_t *fat_name) {
    memset(fat_name, ' ', 11);
    
    const char *dot = strrchr(filename, '.');
    size_t name_len;
    size_t ext_len = 0;

    if (dot) {
        name_len = dot - filename;
        ext_len = strlen(dot + 1);
    } else {
        name_len = strlen(filename);
    }

    // Copy name part (max 8 chars)
    for (size_t i = 0; i < 8 && i < name_len; i++) {
        fat_name[i] = toupper((unsigned char)filename[i]);
    }

    // Copy extension part (max 3 chars, starts at byte 8)
    if (ext_len > 0) {
        for (size_t i = 0; i < 3 && i < ext_len; i++) {
            fat_name[8 + i] = toupper((unsigned char)dot[1 + i]);
        }
    }
}

/**
 * @brief Encodes current time into FAT format (16-bit time)
 */
uint16_t encode_time() {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    return (uint16_t)((tm->tm_hour << 11) | (tm->tm_min << 5) | (tm->tm_sec / 2));
}

/**
 * @brief Encodes current date into FAT format (16-bit date)
 */
uint16_t encode_date() {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    uint16_t year = tm->tm_year + 1900 - 1980;
    return (uint16_t)((year << 9) | ((tm->tm_mon + 1) << 5) | (tm->tm_mday));
}

/**
 * @brief Searches a directory area (Root or Data Cluster) for a specific entry name.
 * @param fp File pointer.
 * @param start_cluster 0 for Root Dir, > 0 for a data cluster.
 * @param target_fat_name 11-byte FAT name to search for.
 * @param found_entry_out Pointer to store the found DirEntry.
 * @param entry_idx_out Pointer to store the entry's index within the directory.
 * @return > 0 (cluster of found entry's parent), 0 (entry not found), -1 (error).
 */
static int search_dir(FILE *fp, int start_cluster, const uint8_t *target_fat_name, DirEntry *found_entry_out, int *entry_idx_out, int *entry_cluster_out) {
    unsigned char sector_buffer[SECTOR_SIZE];
    int entry_idx = 0;

    if (start_cluster == 0) {
        // Search Root Directory
        for (size_t sector = 0; sector < ROOT_DIR_SECTORS; sector++) {
            if (read_sector(fp, ROOT_DIR_START_SECTOR + sector, sector_buffer) != 0) return -1;
            
            for (size_t i = 0; i < SECTOR_SIZE / sizeof(DirEntry); i++) {
                DirEntry *d_entry = (DirEntry*)(sector_buffer + i * sizeof(DirEntry));
                
                if (d_entry->DIR_Name[0] == 0x00) return 0; // End of entries
                if (d_entry->DIR_Name[0] == 0xE5 || d_entry->DIR_Attr == ATTR_LFN) {
                    entry_idx++;
                    continue;
                }

                if (memcmp(d_entry->DIR_Name, target_fat_name, 11) == 0) {
                    if (found_entry_out) *found_entry_out = *d_entry;
                    if (entry_idx_out) *entry_idx_out = entry_idx;
                    // For root, the parent cluster is 0.
                    if (entry_cluster_out) *entry_cluster_out = 0; 
                    return 1; // Found in Root
                }
                entry_idx++;
            }
        }
        return 0; // Not found in Root Dir

    } else {
        // Search Data Cluster Directory (Prototype only supports single cluster for subdirs)
        if (read_cluster(fp, start_cluster, sector_buffer) != 0) return -1;

        for (size_t i = 0; i < SECTOR_SIZE / sizeof(DirEntry); i++) {
            DirEntry *d_entry = (DirEntry*)(sector_buffer + i * sizeof(DirEntry));

            if (d_entry->DIR_Name[0] == 0x00) return 0; // End of entries
            if (d_entry->DIR_Name[0] == 0xE5 || d_entry->DIR_Attr == ATTR_LFN) {
                entry_idx++;
                continue;
            }

            if (memcmp(d_entry->DIR_Name, target_fat_name, 11) == 0) {
                if (found_entry_out) *found_entry_out = *d_entry;
                if (entry_idx_out) *entry_idx_out = entry_idx;
                if (entry_cluster_out) *entry_cluster_out = start_cluster; 
                return start_cluster; // Found in this cluster
            }
            entry_idx++;
        }
        return 0; // Not found in Data Dir
    }
}

/**
 * @brief Finds a file or directory entry by path (supports /FILE or /DIR/FILE).
 * 
 * @param path The path to search for (e.g., "/dir1/file.txt").
 * @param entry_out Pointer to store the found DirEntry.
 * @param parent_cluster_out Pointer to store the starting cluster of the parent directory (0 for root).
 * @param entry_offset_out Pointer to store the index of the DirEntry within its directory.
 * @param entry_cluster_out Pointer to store the cluster number where the entry itself resides (for non-root entries).
 * @return 0 on success (entry found), -1 if not found, 1 for root directory (path is "/").
 */
int find_entry(FILE *fp, const char *path, DirEntry *entry_out, int *parent_cluster_out, int *entry_offset_out, int *entry_cluster_out) {
    if (parent_cluster_out) *parent_cluster_out = 0;
    if (entry_offset_out) *entry_offset_out = 0;
    if (entry_cluster_out) *entry_cluster_out = 0; // If found in root, entry is in sector 19

    char *path_copy = strdup(path);
    char *token1 = strtok(path_copy, "/");
    
    // Case 1: Path is "/"
    if (token1 == NULL) {
        free(path_copy);
        return 1; // Root Directory
    }

    char *token2 = strtok(NULL, "/");
    uint8_t fat_name1[11];
    format_name(token1, fat_name1);
    
    // Search Root Dir for token1
    DirEntry entry1;
    int entry_idx1;
    int root_search_result = search_dir(fp, 0, fat_name1, &entry1, &entry_idx1, NULL);

    if (root_search_result <= 0) {
        free(path_copy);
        return -1; // token1 not found
    }

    if (token2 == NULL) {
        // Case 2: Path is "/DIR" or "/FILE" (token1 is the target)
        if (entry_out) *entry_out = entry1;
        if (parent_cluster_out) *parent_cluster_out = 0; // Parent is root
        if (entry_offset_out) *entry_offset_out = entry_idx1;
        
        // entry_cluster_out is not meaningful here since DirEntries are in sectors, not clusters
        // For simplicity, we just return the index and parent cluster (0)
        
        free(path_copy);
        return 0;

    } else {
        // Case 3: Path is "/DIR/FILE" (token2 is the target)
        if (!(entry1.DIR_Attr & ATTR_DIRECTORY)) {
            fprintf(stderr, "Error: Path segment '%s' is not a directory.\n", token1);
            free(path_copy);
            return -1;
        }

        uint8_t fat_name2[11];
        format_name(token2, fat_name2);
        
        int subdir_cluster = entry1.DIR_FstClusLO;
        DirEntry entry2;
        int entry_idx2;
        int entry_cluster_val;
        
        // Search the subdirectory (only supports single cluster subdir in prototype)
        int subdir_search_result = search_dir(fp, subdir_cluster, fat_name2, &entry2, &entry_idx2, &entry_cluster_val);

        if (subdir_search_result > 0) {
            if (entry_out) *entry_out = entry2;
            if (parent_cluster_out) *parent_cluster_out = subdir_cluster; // Parent is the subdir's start cluster
            if (entry_offset_out) *entry_offset_out = entry_idx2;
            if (entry_cluster_out) *entry_cluster_out = entry_cluster_val;
            free(path_copy);
            return 0; // Found entry2
        } else {
            free(path_copy);
            return -1; // token2 not found
        }
    }
}

/**
 * @brief Finds a free directory entry in a given directory (supports Root Dir or single Data Cluster)
 */
int get_free_entry_offset(FILE *fp, int dir_cluster, DirEntry *empty_entry_out, int *entry_offset_out, int *entry_cluster_out) {
    unsigned char buffer[SECTOR_SIZE];
    int entry_idx = 0;
    
    if (entry_cluster_out) *entry_cluster_out = 0;
    
    if (dir_cluster == 0) {
        // --- Root Directory Search ---
        for (size_t sector = 0; sector < ROOT_DIR_SECTORS; sector++) {
            if (read_sector(fp, ROOT_DIR_START_SECTOR + sector, buffer) != 0) return -1;
            
            for (size_t i = 0; i < SECTOR_SIZE / sizeof(DirEntry); i++) {
                DirEntry *d_entry = (DirEntry*)(buffer + i * sizeof(DirEntry));
                
                if (d_entry->DIR_Name[0] == 0x00 || d_entry->DIR_Name[0] == 0xE5) {
                    if (empty_entry_out) *empty_entry_out = *d_entry;
                    *entry_offset_out = entry_idx;
                    // fseek is useful for fat_import direct write (though sector write is safer)
                    fseek(fp, (long)(ROOT_DIR_START_SECTOR + sector) * SECTOR_SIZE + i * sizeof(DirEntry), SEEK_SET);
                    return 1; // Success in Root Dir
                }
                entry_idx++;
            }
        }
        return -1; // No free entry found in Root

    } else {
        // --- Data Cluster Directory Search (Single cluster only) ---
        if (read_cluster(fp, dir_cluster, buffer) != 0) return -1;

        for (size_t i = 0; i < SECTOR_SIZE / sizeof(DirEntry); i++) {
            DirEntry *d_entry = (DirEntry*)(buffer + i * sizeof(DirEntry));

            if (d_entry->DIR_Name[0] == 0x00 || d_entry->DIR_Name[0] == 0xE5) {
                if (empty_entry_out) *empty_entry_out = *d_entry;
                *entry_offset_out = entry_idx;
                if (entry_cluster_out) *entry_cluster_out = dir_cluster;
                
                // fseek is not reliable here as we use sector write. Just return cluster info.
                
                return 0; // Success in Data Dir
            }
            entry_idx++;
        }
        
        // NOTE: If directory is full, a real FAT system would allocate a new cluster here.
        return -1; // No free entry found in Data Dir
    }
}
