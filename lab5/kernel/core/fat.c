#include "fat.h"
#include "proc.h"
#include "device.h"
#include "common.h"

struct FileDescription system_open_file_table[MAX_GLOBAL_FILES];
static uint8_t fat_cache[FAT_SIZE_SECTORS * SECTOR_SIZE];

void fat_init() {
    for (int i = 0; i < FAT_SIZE_SECTORS; i++) {
        // TODO: Preload FAT sector i into fat_cache. FAT1 starts at
        // FAT1_START_SECTOR, and each cached sector occupies SECTOR_SIZE bytes.
        readSect(fat_cache + i * SECTOR_SIZE, FAT1_START_SECTOR + i);
    }
}

static void to_fat_name(const char *src, uint8_t *dst) {
    int i, j;
    for (i = 0; i < 11; i++) dst[i] = ' ';
    for (i = 0, j = 0; i < 8 && src[j] && src[j] != '/' && src[j] != '.'; i++, j++) {
        dst[i] = (src[j] >= 'a' && src[j] <= 'z') ? src[j] - 'a' + 'A' : src[j];
    }
    while (src[j] && src[j] != '/' && src[j] != '.') j++;
    if (src[j] == '.') {
        j++;
        for (i = 8; i < 11 && src[j] && src[j] != '/'; i++, j++) {
            dst[i] = (src[j] >= 'a' && src[j] <= 'z') ? src[j] - 'a' + 'A' : src[j];
        }
    }
}

static uint32_t get_next_cluster(uint32_t cluster) {
    uint32_t offset = cluster + (cluster / 2);
    if (offset + 1 >= sizeof(fat_cache)) return 0xFFF;
    
    uint16_t entry = fat_cache[offset] | (fat_cache[offset + 1] << 8);
    return (cluster & 1) ? (entry >> 4) : (entry & 0x0FFF);
}

int fat_open(const char *path) {
    // fd_idx will hold the index of a free fd slot in the current process.
    int fd_idx = -1;

    // TODO: Find the first empty slot in current->fds[] and store its index in
    // fd_idx. If the current process has no free fd slot, return -1.
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (current->fds[i] == NULL) {
            fd_idx = i;
            break;
        }
    }
    if (fd_idx == -1) return -1;

    // g_idx will hold the index of a free entry in the system-wide open file table.
    int g_idx = -1;

    // TODO: Find the first unused entry in system_open_file_table[] and store
    // its index in g_idx. If the global open file table is full, return -1.
    for (int i = 0; i < MAX_GLOBAL_FILES; i++) {
        if (!system_open_file_table[i].used) {
            g_idx = i;
            break;
        }
    }
    if (g_idx == -1) return -1;

    const char *p = path;
    if (*p == '/') p++;

    uint32_t curr_cluster = 0; // 0 means Root Directory
    uint8_t fat_name[11];
    DirEntry dir[SECTOR_SIZE / sizeof(DirEntry)];

    while (*p) {
        to_fat_name(p, fat_name);
        int found = 0;

        int has_slash = 0;
        const char *q = p;
        while (*q && *q != '/') q++;
        if (*q == '/') has_slash = 1;

        if (curr_cluster == 0) {
            // Search Root Directory
            for (int i = 0; i < ROOT_DIR_SECTORS; i++) {
                readSect(dir, ROOT_DIR_START_SECTOR + i);
                for (int j = 0; j < SECTOR_SIZE / sizeof(DirEntry); j++) {
                    if (dir[j].DIR_Name[0] == 0) break;
                    if (dir[j].DIR_Name[0] == 0xE5) continue;
                    
                    int match = 1;
                    for (int k = 0; k < 11; k++) if (dir[j].DIR_Name[k] != fat_name[k]) { match = 0; break; }
                    
                    if (match) {
                        curr_cluster = dir[j].DIR_FstClusLO;
                        if (!has_slash) { // Last component
                            // TODO: Initialize system_open_file_table[g_idx] for
                            // the opened file. Mark used as 1, set ref_count to 1
                            // because only current owns this open file entry now,
                            // copy the file's start cluster and size from this
                            // root-directory entry, set curr_cluster to the start
                            // cluster, set curr_pos to 0, then store
                            // &system_open_file_table[g_idx] in current->fds[fd_idx].
                            system_open_file_table[g_idx].used = 1;
                            system_open_file_table[g_idx].ref_count = 1;
                            system_open_file_table[g_idx].start_cluster = dir[j].DIR_FstClusLO;
                            system_open_file_table[g_idx].curr_cluster = dir[j].DIR_FstClusLO;
                            system_open_file_table[g_idx].curr_pos = 0;
                            system_open_file_table[g_idx].file_size = dir[j].DIR_FileSize;
                            current->fds[fd_idx] = &system_open_file_table[g_idx];
                            return fd_idx;
                        }
                        found = 1;
                        break;
                    }
                }
                if (found) break;
            }
        } else {
            // Search Subdirectory
            uint32_t c = curr_cluster;
            while (c >= 2 && c < 0xFF8) {
                readSect(dir, DATA_START_SECTOR + c - 2);
                for (int j = 0; j < SECTOR_SIZE / sizeof(DirEntry); j++) {
                    if (dir[j].DIR_Name[0] == 0) break;
                    if (dir[j].DIR_Name[0] == 0xE5) continue;
                    
                    int match = 1;
                    for (int k = 0; k < 11; k++) if (dir[j].DIR_Name[k] != fat_name[k]) { match = 0; break; }
                    
                    if (match) {
                        curr_cluster = dir[j].DIR_FstClusLO;
                        if (!has_slash) {
                            // TODO: Initialize system_open_file_table[g_idx] for
                            // the opened file. Mark used as 1, set ref_count to 1
                            // because only current owns this open file entry now,
                            // copy the file's start cluster and size from this
                            // subdirectory entry, set curr_cluster to the start
                            // cluster, set curr_pos to 0, then store
                            // &system_open_file_table[g_idx] in current->fds[fd_idx].
                            system_open_file_table[g_idx].used = 1;
                            system_open_file_table[g_idx].ref_count = 1;
                            system_open_file_table[g_idx].start_cluster = dir[j].DIR_FstClusLO;
                            system_open_file_table[g_idx].curr_cluster = dir[j].DIR_FstClusLO;
                            system_open_file_table[g_idx].curr_pos = 0;
                            system_open_file_table[g_idx].file_size = dir[j].DIR_FileSize;
                            current->fds[fd_idx] = &system_open_file_table[g_idx];
                            return fd_idx;
                        }
                        found = 1;
                        break;
                    }
                }
                if (found) break;
                c = get_next_cluster(c);
            }
        }

        if (!found) return -1;
        
        while (*p && *p != '/') p++;
        if (*p == '/') p++;
    }

    return -1;
}

int fat_read(int fd, void *buf, uint32_t count) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || current->fds[fd] == NULL) return -1;
    struct FileDescription *f = current->fds[fd];

    if (f->curr_pos >= f->file_size) return 0;
    if (f->curr_pos + count > f->file_size) count = f->file_size - f->curr_pos;
    
    uint32_t total_read = 0;
    uint8_t sector_buf[SECTOR_SIZE];
    
    while (total_read < count) {
        // TODO: Compute how far curr_pos is into the current 512-byte sector,
        // how many bytes remain in that sector, and how many bytes this loop
        // iteration should copy without crossing the sector boundary.
        uint32_t offset_in_cluster = f->curr_pos % SECTOR_SIZE;
        uint32_t bytes_left_in_sector = SECTOR_SIZE - offset_in_cluster;
        uint32_t to_read = count - total_read;
        if (to_read > bytes_left_in_sector) to_read = bytes_left_in_sector;

        // TODO: Read the sector for f->curr_cluster into sector_buf. FAT data
        // clusters start at DATA_START_SECTOR, and cluster number 2 maps to the
        // first data sector.
        readSect(sector_buf, DATA_START_SECTOR + f->curr_cluster - 2);

        // TODO: Copy to_read bytes from sector_buf + offset_in_cluster into the
        // user buffer at buf + total_read.
        uint8_t *dst = (uint8_t *)buf + total_read;
        for (uint32_t i = 0; i < to_read; i++) {
            dst[i] = sector_buf[offset_in_cluster + i];
        }

        // TODO: Advance both counters after the copy: total_read records how many
        // bytes this syscall has returned, while f->curr_pos is the shared file
        // offset stored in the open file table.
        total_read += to_read;
        f->curr_pos += to_read;

        // TODO: If the read reached the end of the current sector and more bytes
        // are still needed, follow the FAT chain to the next cluster. Stop if the
        // next cluster is an end-of-chain marker.
        if (offset_in_cluster + to_read >= SECTOR_SIZE && total_read < count) {
            uint32_t next = get_next_cluster(f->curr_cluster);
            if (next >= 0xFF8) break;
            f->curr_cluster = next;
        }
    }

    return total_read;
}

int fat_close(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES || current->fds[fd] == NULL) return -1;

    // TODO: Close current->fds[fd]: decrement the shared FileDescription's
    // ref_count, mark it unused when the count reaches zero, then clear the
    // current process fd slot.
    struct FileDescription *f = current->fds[fd];
    f->ref_count--;
    if (f->ref_count <= 0) {
        f->used = 0;
        f->ref_count = 0;
    }
    current->fds[fd] = NULL;
    return 0;
}
