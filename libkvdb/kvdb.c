#include "kvdb.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

// 键值对条目结构
struct entry {
    char *key;
    char *value;
    size_t key_len;
    size_t value_len;
};



// 精确读取辅助函数
static ssize_t read_exact(int fd, void *buf, size_t count) {
    size_t bytes_read = 0;
    char *p = buf;
    while (bytes_read < count) {
        ssize_t n = read(fd, p + bytes_read, count - bytes_read);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break; // EOF
        bytes_read += n;
    }
    return bytes_read;
}

// 刷新缓冲区到磁盘
static int flush_buffer(struct kvdb_t *db) {
    if (db->buffer.size == 0) {
        return 0; // 空缓冲区无需刷新
    }
    
    // 写入数据
    ssize_t written = write(db->fd, db->buffer.data, db->buffer.size);
    if (written != (ssize_t)db->buffer.size) {
        return -1;
    }
    
    // 重置缓冲区
    db->buffer.size = 0;
    
    // 数据落盘
    return fdatasync(db->fd);
}

// 扩展缓冲区
static int expand_buffer(struct write_buffer *buf, size_t min_capacity) {
    size_t new_capacity = buf->capacity ? buf->capacity * 2 : 4096;
    if (new_capacity < min_capacity) {
        new_capacity = min_capacity;
    }
    
    char *new_data = realloc(buf->data, new_capacity);
    if (!new_data) {
        return -1;
    }
    
    buf->data = new_data;
    buf->capacity = new_capacity;
    return 0;
}

// 追加数据到缓冲区
static int append_to_buffer(struct write_buffer *buf, const void *data, size_t len) {
    size_t needed = buf->size + len;
    if (needed > buf->capacity) {
        if (expand_buffer(buf, needed) < 0) {
            return -1;
        }
    }
    
    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
    return 0;
}

int kvdb_open(struct kvdb_t *db, const char *path) {
    // 初始化缓冲区
    memset(&db->buffer, 0, sizeof(db->buffer));
    
    // 打开数据库文件
    int fd = open(path, O_RDWR | O_CREAT, 0666);
    if (fd < 0) return -1;
    
    // 复制路径字符串
    char *path_copy = strdup(path);
    if (!path_copy) {
        close(fd);
        return -1;
    }
    
    db->fd = fd;
    db->path = path_copy;
    return 0;
}

int kvdb_put(struct kvdb_t *db, const char *key, const char *value) {
    // 获取键值长度
    uint32_t key_len = strlen(key);
    uint32_t value_len = strlen(value);
    
    // 检查缓冲区是否有足够空间
    size_t total_size = sizeof(key_len) + sizeof(value_len) + key_len + value_len;
    
    // 如果超过阈值或空间不足，刷新缓冲区
    if (db->buffer.size + total_size > db->buffer.capacity || 
        db->buffer.size > 8192) { // 8KB刷新阈值
        if (flush_buffer(db) < 0) {
            return -1;
        }
    }
    
    // 追加数据到缓冲区
    if (append_to_buffer(&db->buffer, &key_len, sizeof(key_len)) ||
        append_to_buffer(&db->buffer, &value_len, sizeof(value_len)) ||
        append_to_buffer(&db->buffer, key, key_len) ||
        append_to_buffer(&db->buffer, value, value_len)) {
        return -1;
    }
    
    return 0;
}

int kvdb_flush(struct kvdb_t *db) {
    return flush_buffer(db);
}

int kvdb_get(struct kvdb_t *db, const char *key, char *buf, size_t length) {
    // 首先刷新缓冲区确保最新数据可见
    if (flush_buffer(db) < 0) {
        return -1;
    }
    
    // 定位到文件头
    if (lseek(db->fd, 0, SEEK_SET) < 0) {
        return -1;
    }
    
    struct entry *entries = NULL;
    size_t num_entries = 0;
    ssize_t ret = -1;
    
    // 读取并解析所有记录
    while (1) {
        uint32_t key_len, value_len;
        
        // 读取键长
        if (read_exact(db->fd, &key_len, sizeof(key_len)) != sizeof(key_len)) 
            break;
        
        // 读取值长
        if (read_exact(db->fd, &value_len, sizeof(value_len)) != sizeof(value_len)) 
            break;
        
        // 分配键缓冲区
        char *key_buf = malloc(key_len + 1);
        if (!key_buf) break;
        
        // 读取键
        if (read_exact(db->fd, key_buf, key_len) != key_len) {
            free(key_buf);
            break;
        }
        key_buf[key_len] = '\0';
        
        // 分配值缓冲区
        char *value_buf = malloc(value_len + 1);
        if (!value_buf) {
            free(key_buf);
            break;
        }
        
        // 读取值
        if (read_exact(db->fd, value_buf, value_len) != value_len) {
            free(key_buf);
            free(value_buf);
            break;
        }
        value_buf[value_len] = '\0';
        
        // 更新或添加条目
        int found = 0;
        for (size_t i = 0; i < num_entries; i++) {
            if (strcmp(entries[i].key, key_buf) == 0) {
                free(entries[i].value);
                entries[i].value = value_buf;
                entries[i].value_len = value_len;
                found = 1;
                free(key_buf);
                break;
            }
        }
        
        if (!found) {
            struct entry *new_entries = realloc(entries, (num_entries + 1) * sizeof(struct entry));
            if (!new_entries) {
                free(key_buf);
                free(value_buf);
                break;
            }
            entries = new_entries;
            entries[num_entries].key = key_buf;
            entries[num_entries].value = value_buf;
            entries[num_entries].key_len = key_len;
            entries[num_entries].value_len = value_len;
            num_entries++;
        }
    }
    
    // 查找目标键
    for (size_t i = 0; i < num_entries; i++) {
        if (strcmp(entries[i].key, key) == 0) {
            size_t to_copy = entries[i].value_len;
            if (to_copy > length - 1) 
                to_copy = length - 1;
            memcpy(buf, entries[i].value, to_copy);
            buf[to_copy] = '\0';
            ret = to_copy;
            break;
        }
    }
    
    // 清理资源
    for (size_t i = 0; i < num_entries; i++) {
        free(entries[i].key);
        free(entries[i].value);
    }
    free(entries);
    
    return ret;
}

int kvdb_close(struct kvdb_t *db) {
    // 刷新剩余数据
    flush_buffer(db);
    
    // 释放缓冲区
    free(db->buffer.data);
    
    // 释放路径字符串
    free(db->path);
    db->path = NULL;
    
    // 关闭文件
    if (close(db->fd) < 0) 
        return -1;
    return 0;
}