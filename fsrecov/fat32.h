#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;

// FAT32 引导扇区/BPB (BIOS Parameter Block) 结构
struct fat32hdr {
    u8  BS_jmpBoot[3];        // 跳转指令 (EB 58 90 或类似)
    u8  BS_OEMName[8];        // 格式化该卷的OEM名称（字符串）
    u16 BPB_BytsPerSec;       // 每扇区字节数（通常为512）
    u8  BPB_SecPerClus;       // 每簇扇区数（1,2,4,8,16,32,64,128）
    u16 BPB_RsvdSecCnt;       // 保留扇区数（包括引导扇区）
    u8  BPB_NumFATs;          // FAT表的数量（通常为2）
    u16 BPB_RootEntCnt;       // FAT32中此值必须为0
    u16 BPB_TotSec16;         // 总扇区数（16位，若为0则使用BPB_TotSec32）
    u8  BPB_Media;            // 存储介质类型（0xF8=硬盘）
    u16 BPB_FATSz16;          // FAT32中此值必须为0
    u16 BPB_SecPerTrk;        // 每磁道扇区数（磁盘几何参数）
    u16 BPB_NumHeads;         // 磁头数（磁盘几何参数）
    u32 BPB_HiddSec;          // 隐藏扇区数（分区前的扇区数）
    u32 BPB_TotSec32;         // 总扇区数（32位）
    u32 BPB_FATSz32;          // 单个FAT表占用的扇区数（FAT32关键字段）
    u16 BPB_ExtFlags;         // 扩展标志（位7-0：活动FAT索引，位15：镜像禁用）
    u16 BPB_FSVer;            // 文件系统版本（高字节=主版本，低字节=次版本）
    u32 BPB_RootClus;         // 根目录起始簇号（FAT32关键字段）
    u16 BPB_FSInfo;           // FSInfo结构在保留区的扇区号
    u16 BPB_BkBootSec;        // 引导记录备份位置扇区号
    u8  BPB_Reserved[12];     // 保留字段（用于扩展）
    u8  BS_DrvNum;            // 驱动器号（INT 13h使用）
    u8  BS_Reserved1;         // 保留（WindowsNT使用）
    u8  BS_BootSig;           // 扩展引导签名（0x29表示后三个字段存在）
    u32 BS_VolID;             // 卷序列号
    u8  BS_VolLab[11];        // 卷标（字符串）
    u8  BS_FilSysType[8];     // 文件系统类型（"FAT32"字符串）
    u8  __padding_1[420];     // 引导代码和填充字节
    u16 Signature_word;       // 引导扇区结束标志（0xAA55）
} __attribute__((packed));

// FAT32 目录项结构（32字节）
struct fat32dent {
    u8  DIR_Name[11];         // 8.3格式文件名（空格填充）
    u8  DIR_Attr;             // 文件属性（位掩码）
    u8  DIR_NTRes;            // NT保留字节
    u8  DIR_CrtTimeTenth;     // 创建时间的10毫秒单位（0-199）
    u16 DIR_CrtTime;          // 创建时间（小时5位/分6位/秒5位）
    u16 DIR_CrtDate;          // 创建日期（年7位/月4位/日5位）
    u16 DIR_LastAccDate;      // 最后访问日期
    u16 DIR_FstClusHI;        // 起始簇号的高16位（FAT32关键字段）
    u16 DIR_WrtTime;          // 最后写入时间
    u16 DIR_WrtDate;          // 最后写入日期
    u16 DIR_FstClusLO;        // 起始簇号的低16位（FAT32关键字段）
    u32 DIR_FileSize;         // 文件大小字节数（目录时为0）
} __attribute__((packed));

#define CLUS_INVALID   0xffffff7

#define ATTR_READ_ONLY 0x01
#define ATTR_HIDDEN    0x02
#define ATTR_SYSTEM    0x04
#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE   0x20
