#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "fat32.h"

// 调试输出控制
static bool logging_active = true;


#define BM_HEADER 0x4D42 // 'BM'

#define TEMPORARY_FILE_PATTERN "/tmp/frec_XXXXXX"

#define LONG_NAME_FLAGS (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)
#define FINAL_LONG_ENTRY 0x40

// ====== 数据结构定义 ======
struct lfn_record {
    u8  sequence;
    u16 name_part1[5];
    u8  attributes;
    u8  record_type;
    u8  checksum_val;
    u16 name_part2[6];
    u16 cluster_low;
    u16 name_part3[2];
} __attribute__((packed));

struct recovered_file {
    char* filename;
    u8* data_start;
    u32 data_size;
};

struct entry_segment {
    void* entry_ptr;
    int entry_count;
};

struct unmatched_entries {
    struct entry_segment prefixes[10], suffixes[10];
    int prefix_cnt, suffix_cnt;
};

// ====== 全局变量 ======
struct fat32hdr* disk_header;
u8* disk_start;
u8* disk_finish;
int data_start_sector;
int total_cluster_count;
int dir_entry_size = sizeof(struct fat32dent);
struct unmatched_entries pending = {0};  // 跨簇待处理的目录项

// ====== 函数声明 ======
void* load_disk_image(const char*);
void perform_full_scan(void);
void analyze_entry(u8* entry_ptr, int entry_count);

int entry_main(int param_count, char* param_values[])
{
    if (param_count < 2) {
        fprintf(stderr, "Usage: %s disk-image\n", param_values[0]);
        exit(1);
    }

    setbuf(stdout, NULL);

    // 验证结构大小
    if (sizeof(struct fat32hdr) != 512) exit(2);
    if (sizeof(struct fat32dent) != 32) exit(3);

    disk_start = load_disk_image(param_values[1]);
    disk_header = (struct fat32hdr*)disk_start;

    disk_finish = disk_start + disk_header->BPB_TotSec32 * disk_header->BPB_BytsPerSec - 1;
    data_start_sector = disk_header->BPB_RsvdSecCnt + disk_header->BPB_NumFATs * disk_header->BPB_FATSz32;
    total_cluster_count = (disk_header->BPB_TotSec32 - data_start_sector) / disk_header->BPB_SecPerClus;

    perform_full_scan();

    size_t disk_size = disk_header->BPB_TotSec32 * disk_header->BPB_BytsPerSec;
    munmap(disk_header, disk_size);

    return 0;
}

void* load_disk_image(const char* filename)
{
    int file_desc = open(filename, O_RDONLY);
    if (file_desc < 0) {
        perror("open");
        exit(4);
    }

    off_t img_size = lseek(file_desc, 0, SEEK_END);
    if (img_size < 0) {
        perror("lseek");
        close(file_desc);
        exit(5);
    }

    void* mapped = mmap(NULL, img_size, PROT_READ, MAP_PRIVATE, file_desc, 0);
    close(file_desc);
    
    if (mapped == MAP_FAILED) {
        perror("mmap");
        exit(6);
    }

    struct fat32hdr* h = (struct fat32hdr*)mapped;
    if (h->Signature_word != 0xaa55) {
        fprintf(stderr, "Invalid boot signature\n");
        munmap(mapped, img_size);
        exit(7);
    }

    if (h->BPB_TotSec32 * h->BPB_BytsPerSec != img_size) {
        fprintf(stderr, "Size mismatch\n");
        munmap(mapped, img_size);
        exit(8);
    }

    return mapped;
}

bool validate_short_entry(struct fat32dent* entry)
{
    // 检查空条目
    if (entry->DIR_Name[0] == 0x00) return false;
    
    // 检查保留位
    if ((entry->DIR_Attr & 0xC0) != 0) return false;  // 0b11000000
    if (entry->DIR_NTRes != 0) return false;
    
    // 特殊名称检查
    if (entry->DIR_Name[0] == '.' || entry->DIR_Name[0] == 0xE5) {
        return true;
    }
    
    // 验证簇号范围
    u32 cluster_num = (entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;
    if (cluster_num < 2 || cluster_num > total_cluster_count + 1) {
        return false;
    }
    
    // 检查文件大小合理性
    if (entry->DIR_FileSize > (64 << 20)) {  // 64MB
        return false;
    }
    
    return true;
}

bool validate_long_entry(struct lfn_record* lfn)
{
    int seq_num = lfn->sequence & ~FINAL_LONG_ENTRY;
    if (seq_num == 0 || seq_num > 20) return false;
    if (lfn->attributes != LONG_NAME_FLAGS) return false;
    if (lfn->record_type != 0) return false;
    if (lfn->cluster_low != 0) return false;
    return true;
}

bool is_directory_cluster(u8* cluster_ptr)
{
    // 快速检查可能的目录簇
    return validate_short_entry((struct fat32dent*)cluster_ptr) ||
           validate_long_entry((struct lfn_record*)cluster_ptr);
}

u8* get_cluster_base(int cluster_num)
{
    int sector_offset = data_start_sector + (cluster_num - 2) * disk_header->BPB_SecPerClus;
    return disk_start + sector_offset * disk_header->BPB_BytsPerSec;
}

u8 calculate_name_checksum(const u8* name_field)
{
    u8 chk = 0;
    for (int i = 0; i < 11; i++) {
        chk = ((chk & 1) << 7) | (chk >> 1);
        chk += name_field[i];
    }
    return chk;
}

void decode_long_name(struct lfn_record* lfn, char* buffer)
{
    int idx = 0;
    for (int i = 0; i < 5 && lfn->name_part1[i]; i++) 
        buffer[idx++] = lfn->name_part1[i];
    for (int i = 0; i < 6 && lfn->name_part2[i]; i++) 
        buffer[idx++] = lfn->name_part2[i];
    for (int i = 0; i < 2 && lfn->name_part3[i]; i++) 
        buffer[idx++] = lfn->name_part3[i];
    buffer[idx] = '\0';
}

bool entries_compatible(struct lfn_record* first, struct lfn_record* second)
{
    // 通过校验和验证条目关联性
    if (validate_long_entry(second)) {
        return first->checksum_val == second->checksum_val;
    }
    if (validate_short_entry((struct fat32dent*)second)) {
        return first->checksum_val == calculate_name_checksum(((struct fat32dent*)second)->DIR_Name);
    }
    return false;
}

void resolve_pending_entries()
{
    u8* merged_entries;
    for (int i = 0; i < pending.prefix_cnt; i++) {
        u8* prefix = pending.prefixes[i].entry_ptr;
        int prefix_len = pending.prefixes[i].entry_count;
        for (int j = 0; j < pending.suffix_cnt; j++) {
            if (!pending.suffixes[j].entry_ptr) continue;
            
            u8* suffix = pending.suffixes[j].entry_ptr;
            int suffix_len = pending.suffixes[j].entry_count;
            
            if (entries_compatible((struct lfn_record*)prefix, (struct lfn_record*)suffix)) {
                //log_debug("Matched: %tx - %tx\n", prefix - disk_start, suffix - disk_start);
                size_t total_size = (prefix_len + suffix_len) * dir_entry_size;
                merged_entries = malloc(total_size);
                // 复制数据
                memcpy(merged_entries, prefix, prefix_len * dir_entry_size);
                memcpy(merged_entries + prefix_len * dir_entry_size, suffix, suffix_len * dir_entry_size);
                // 处理合并后的条目
                analyze_entry(merged_entries, prefix_len + suffix_len);
                pending.suffixes[j].entry_ptr = NULL;
                free(merged_entries);
            }
        }
    }

    // 处理未匹配的短条目
    for (int j = 0; j < pending.suffix_cnt; j++) {
        if (pending.suffixes[j].entry_ptr && pending.suffixes[j].entry_count == 1) {
            analyze_entry(pending.suffixes[j].entry_ptr, 1);
        }
    }
}

void save_and_hash(struct recovered_file file_info)
{
    char tmp_path[64];
    strcpy(tmp_path, TEMPORARY_FILE_PATTERN);
    int tmp_fd = mkstemp(tmp_path);
    if (tmp_fd < 0) return;
    
    write(tmp_fd, file_info.data_start, file_info.data_size);
    close(tmp_fd);

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "sha1sum %s", tmp_path);
    FILE* sha_pipe = popen(cmd, "r");
    char hash[64];
    int len = strlen(file_info.filename);
    for (int i=len - 1;i>=0;i--){
        if (file_info.filename[i] == 'p'){
            file_info.filename[i+1] = '\0';
        }
    }
    if (fscanf(sha_pipe, "%63s", hash) == 1) {
        printf("%s  %s\n", hash, file_info.filename);

        //printf("damn\n");
    }
    pclose(sha_pipe);
    unlink(tmp_path);
}

void analyze_entry(u8* entries, int count)
{
    struct fat32dent* main_entry = (struct fat32dent*)(entries + (count - 1) * dir_entry_size);
    char* base_name = (char*)main_entry->DIR_Name;
    u32 cluster_num = (main_entry->DIR_FstClusHI << 16) | main_entry->DIR_FstClusLO;

    // 跳过无效或目录条目
    if (cluster_num == 0 || cluster_num > total_cluster_count + 1 ||
        main_entry->DIR_Attr & ATTR_DIRECTORY ||
        main_entry->DIR_Name[0] == 0xE5) {
        return;
    }
    
    u8* cluster_base = get_cluster_base(cluster_num);
    if (*(u16*)cluster_base != BM_HEADER) {
        return;
    }

    // 构建长文件名
    char full_name[256] = {0};
    for (int i = count - 2; i >= 0; i--) {
        struct lfn_record* lfn = (struct lfn_record*)(entries + i * dir_entry_size);
        char name_part[14];
        decode_long_name(lfn, name_part);
        strcat(full_name, name_part);
    }
    
    // 使用短文件名如果没有长文件名
    if (full_name[0] == '\0') {
        strncpy(full_name, base_name, 11);
        full_name[11] = '\0';
    }

    // 计算实际文件大小
    u32 file_size = main_entry->DIR_FileSize;
    if (cluster_base + file_size > disk_finish) {
        file_size = disk_finish - cluster_base;
    }

    struct recovered_file file_data = {
        .filename = full_name,
        .data_start = cluster_base,
        .data_size = file_size
    };
    save_and_hash(file_data);
}

void scan_directory_cluster(u8* cluster_base, int cluster_num)
{
    u8* current = cluster_base;
    
    // 处理可能的跨簇条目
    if (validate_long_entry((struct lfn_record*)current)) {
        struct lfn_record* lfn = (struct lfn_record*)current;
        if (!(lfn->sequence & FINAL_LONG_ENTRY)) {
            pending.suffixes[pending.suffix_cnt++] = (struct entry_segment) {
                .entry_ptr = current,
                .entry_count = (lfn->sequence & 0x1F) + 1
            };
            current += dir_entry_size * ((lfn->sequence & 0x1F) + 1);
        }
    } else if (validate_short_entry((struct fat32dent*)current)) {
        pending.suffixes[pending.suffix_cnt++] = (struct entry_segment) {
            .entry_ptr = current,
            .entry_count = 1
        };
        current += dir_entry_size;
    }

    //log_debug("Cluster %d: %tx\n", cluster_num, current - disk_start);

    u8* group_start = current;
    int group_count = 0;
    size_t cluster_size = disk_header->BPB_BytsPerSec * disk_header->BPB_SecPerClus;
    
    while (current < cluster_base + cluster_size) {
        if (validate_short_entry((struct fat32dent*)current)) {
            analyze_entry(group_start, group_count + 1);
            current += dir_entry_size;
            group_start = current;
            group_count = 0;
        } else if (validate_long_entry((struct lfn_record*)current)) {
            current += dir_entry_size;
            group_count++;
        } else {
            //log_debug("Break at %tx\n", current - disk_start);
            break;
        }
    }
    
    if (group_count > 0) {
        pending.prefixes[pending.prefix_cnt++] = (struct entry_segment) {
            .entry_ptr = group_start,
            .entry_count = group_count
        };
    }
}

void perform_full_scan()
{
    const int start_cluster = 2;
    for (int cluster = start_cluster; cluster < total_cluster_count + start_cluster; cluster++) {
        u8* cluster_base = get_cluster_base(cluster);
        if (is_directory_cluster(cluster_base)) {
            scan_directory_cluster(cluster_base, cluster);
        }
    }
    resolve_pending_entries();
}

// 保持函数名兼容
int main(int argc, char* argv[]) {
    return entry_main(argc, argv);
}