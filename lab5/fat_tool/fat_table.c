#include <stdio.h>
#include <stdint.h>
#include "fat_table.h"
#include "fat_io.h" // For read/write_sector

/**
 * @brief Reads the 12-bit FAT entry for a given cluster.
 */
int get_fat_entry(FILE *fp, int cluster) {
    if (cluster < 2 || cluster >= MAX_CLUSTERS + 2) return -1;

    unsigned char fat_buffer[FAT_SIZE_SECTORS * SECTOR_SIZE];
    for (int i = 0; i < FAT_SIZE_SECTORS; i++) {
        if (read_sector(fp, FAT1_START_SECTOR + i, fat_buffer + i * SECTOR_SIZE) != 0) return -1;
    }

    long fat_offset = cluster * 3 / 2; 
    uint16_t entry = fat_buffer[fat_offset] | (fat_buffer[fat_offset + 1] << 8);

    if (cluster & 0x01) { // Odd cluster
        return entry >> 4; 
    } else { // Even cluster
        return entry & 0x0FFF; 
    }
}

void set_fat_entry(FILE *fp, int cluster, int value) {
    if (cluster < 2 || cluster >= MAX_CLUSTERS + 2) return;
    
    unsigned char fat_buffer[FAT_SIZE_SECTORS * SECTOR_SIZE];
    for (int i = 0; i < FAT_SIZE_SECTORS; i++) {
        if (read_sector(fp, FAT1_START_SECTOR + i, fat_buffer + i * SECTOR_SIZE) != 0) return;
    }

    long fat_offset = cluster * 3 / 2;
    uint16_t entry = fat_buffer[fat_offset] | (fat_buffer[fat_offset + 1] << 8);

    if (cluster & 0x01) { // Odd cluster
        entry = (entry & 0x000F) | (value << 4); 
    } else { // Even cluster
        entry = (entry & 0xF000) | (value & 0x0FFF); 
    }

    fat_buffer[fat_offset] = (uint8_t)(entry & 0xFF);
    fat_buffer[fat_offset + 1] = (uint8_t)(entry >> 8);

    for (int i = 0; i < NUM_FATS; i++) {
        int fat_start = (i == 0) ? FAT1_START_SECTOR : FAT2_START_SECTOR;
        for (int j = 0; j < FAT_SIZE_SECTORS; j++) {
            write_sector(fp, fat_start + j, fat_buffer + j * SECTOR_SIZE);
        }
    }
}

/**
 * @brief Finds the first available free cluster (value 0x000) in the FAT.
 */
int allocate_cluster(FILE *fp) {
    unsigned char fat_buffer[FAT_SIZE_SECTORS * SECTOR_SIZE];
    
    // Read the entire FAT1 table into memory
    for(int i = 0; i < FAT_SIZE_SECTORS; i++) {
        if (read_sector(fp, FAT1_START_SECTOR + i, fat_buffer + i * SECTOR_SIZE) != 0) {
            return -1;
        }
    }

    // Check cluster 2 up to MAX_CLUSTERS + 2
    for (int cluster = 2; cluster < MAX_CLUSTERS + 2; cluster++) {
        long fat_offset = cluster * 3 / 2;
        
        if (fat_offset + 1 >= FAT_SIZE_SECTORS * SECTOR_SIZE) break;

        uint16_t entry = fat_buffer[fat_offset] | (fat_buffer[fat_offset + 1] << 8);
        int value;

        if (cluster & 0x01) { // Odd cluster
            value = entry >> 4;
        } else { // Even cluster
            value = entry & 0x0FFF;
        }

        if (value == FAT_FREE) {
            return cluster;
        }
    }

    return -1; // No free clusters
}
