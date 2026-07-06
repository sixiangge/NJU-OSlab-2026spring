#ifndef FAT_TYPES_H
#define FAT_TYPES_H

#include <stdio.h>
#include <stdint.h>
#include <time.h>

// --- FAT12 1.44MB Floppy Constants ---
#define SECTOR_SIZE 512
#define TOTAL_SECTORS 2880
#define NUM_FATS 2
#define ROOT_ENTRIES 224
#define FAT_SIZE_SECTORS 9 // (1.44MB: 2880 sectors * 1.5 bytes/entry / 512 bytes/sector) -> 9 sectors
#define RESERVED_SECTORS 1

// Calculated offsets
#define FAT1_START_SECTOR RESERVED_SECTORS
#define FAT2_START_SECTOR (FAT1_START_SECTOR + FAT_SIZE_SECTORS)
#define ROOT_DIR_START_SECTOR (FAT2_START_SECTOR + FAT_SIZE_SECTORS)
#define ROOT_DIR_SECTORS ((ROOT_ENTRIES * 32 + SECTOR_SIZE - 1) / SECTOR_SIZE) // 14 sectors
#define DATA_START_SECTOR (ROOT_DIR_START_SECTOR + ROOT_DIR_SECTORS)
#define MAX_CLUSTERS (TOTAL_SECTORS - DATA_START_SECTOR) // Max usable clusters (2880 - 33 = 2847)

// Cluster to Sector mapping
#define CLUSTER_TO_SECTOR(cluster) (DATA_START_SECTOR + (cluster) - 2)

// --- FAT Attribute Flags ---
#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN 0x02
#define ATTR_SYSTEM 0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LFN 0x0F // Long File Name

// --- FAT Status Values (12-bit) ---
#define FAT_FREE 0x000
#define FAT_EOF 0xFFF
#define FAT_BAD_CLUSTER 0xFF7

// --- Structures (Must be packed for exact file system layout) ---

// 1. BIOS Parameter Block (BPB) and Boot Sector structure
typedef struct __attribute__((packed)) {
    uint8_t BS_JmpBoot[3];      
    uint8_t BS_OEMName[8];      
    uint16_t BPB_BytsPerSec;    
    uint8_t BPB_SecPerClus;     
    uint16_t BPB_ResvdSecCnt;   
    uint8_t BPB_NumFATs;        
    uint16_t BPB_RootEntCnt;    
    uint16_t BPB_TotSec16;      
    uint8_t BPB_Media;          
    uint16_t BPB_FATSz16;       
    uint16_t BPB_SecPerTrk;     
    uint16_t BPB_NumHeads;      
    uint32_t BPB_HiddenSec;     
    uint32_t BPB_TotSec32;      

    // FAT12/16 extended fields
    uint8_t BS_DrvNum;          
    uint8_t BS_Reserved1;       
    uint8_t BS_BootSig;         
    uint32_t BS_VolID;          
    uint8_t BS_VolLab[11];      
    uint8_t BS_FilSysType[8];   
    uint8_t BS_BootCode[448];   
    uint16_t BS_SigWord;        
} BPB;

// 2. Directory Entry (32 bytes)
typedef struct __attribute__((packed)) {
    uint8_t DIR_Name[11];       
    uint8_t DIR_Attr;           
    uint8_t DIR_NTRes;          
    uint8_t DIR_CrtTimeTenth;   
    uint16_t DIR_CrtTime;       
    uint16_t DIR_CrtDate;       
    uint16_t DIR_LstAccDate;    
    uint16_t DIR_FstClusHI;     
    uint16_t DIR_WrtTime;       
    uint16_t DIR_WrtDate;       
    uint16_t DIR_FstClusLO;     
    uint32_t DIR_FileSize;      
} DirEntry;

#endif // FAT_TYPES_H
