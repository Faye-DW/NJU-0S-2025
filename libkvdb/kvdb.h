#ifndef KVDB_H
#define KVDB_H

#include <stddef.h>
#include <stdint.h>
#include <sys/file.h>

// 写入缓冲区结构
struct write_buffer {
    char *data;         // 缓冲区数据指针
    size_t size;        // 当前数据大小
    size_t capacity;    // 缓冲区容量
};

struct kvdb_t {
    char *path;         // 数据库文件路径
    int fd;             // 文件描述符
    struct write_buffer buffer; // 写入缓冲区
};

// 打开/创建数据库
int kvdb_open(struct kvdb_t *db, const char *path);

// 存储键值对
int kvdb_put(struct kvdb_t *db, const char *key, const char *value);

// 获取键值对
int kvdb_get(struct kvdb_t *db, const char *key, char *buf, size_t length);

// 手动刷新缓冲区到磁盘
int kvdb_flush(struct kvdb_t *db);

// 关闭数据库
int kvdb_close(struct kvdb_t *db);

#endif // KVDB_H