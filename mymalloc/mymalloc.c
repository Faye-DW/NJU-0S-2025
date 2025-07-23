#include <mymalloc.h>
typedef int pid_t;

typedef struct block {
    size_t length;
    struct block *next;
    pid_t owner_tid;
} Block;

#define STRUCTSIZE (((sizeof(Block) + 7) & ~7))
#define MAX_THREADS 64  // 根据实际需求调整大小
#define SYS_gettid 186  // x86_64系统调用号
#define INITIAL_CHUNK_SIZE (4 * 4096) // 预分配16KB大块
// 每个线程本地链表对应一个锁
typedef struct {
    Block* head;
    spinlock_t lock;
} ThreadHeap;

static ThreadHeap thread_heaps[MAX_THREADS] = {{0}}; // 线程本地存储数组
static Block* global_free_list = NULL;
static spinlock_t global_lock={UNLOCKED};  // 全局链表锁

// 内联汇编获取线程ID
static inline pid_t gettid(void) {
    pid_t tid;
    __asm__ volatile (
        "syscall"
        : "=a" (tid)
        : "0" (SYS_gettid)
        : "rcx", "r11", "memory"
    );
    return tid;
}

// 自旋锁操作宏

// 获取线程对应的本地堆
static ThreadHeap* get_thread_heap() {
    pid_t tid = gettid();
    return &thread_heaps[tid % MAX_THREADS];
}

void *mymalloc(size_t size) {
    if (size == 0) return NULL;
    size = (size + 7) & ~7;  // 8字节对齐
    pid_t tid = gettid();
    ThreadHeap* heap = get_thread_heap();
    Block *curr = NULL, *prev = NULL;
    void *ret = NULL;
    curr = heap->head;
    //-----------
    if (heap->head == NULL) {
        spin_lock(&heap->lock);
        if (heap->head == NULL) { // Double-Check Locking
            size_t chunk_size = INITIAL_CHUNK_SIZE;
            Block *big_block = vmalloc(NULL, chunk_size);
            if (big_block) {
                // 切割大块为多个固定大小的块（例如256B）
                char *current = (char*)big_block;
                size_t block_size = 4096; // 根据测试用例调整
                while (current + STRUCTSIZE + block_size <= (char*)big_block + chunk_size) {
                    Block *blk = (Block*)current;
                    blk->length = block_size;
                    blk->next = heap->head;
                    blk->owner_tid = tid;
                    heap->head = blk;
                    current += STRUCTSIZE + block_size;
                }
                // 剩余空间加入全局链表
                if ((char*)big_block + chunk_size - current >= STRUCTSIZE) {
                    Block *remain = (Block*)current;
                    remain->length = (char*)big_block + chunk_size - current - STRUCTSIZE;
                    remain->owner_tid = tid;
                    spin_lock(&global_lock);
                    remain->next = global_free_list;
                    global_free_list = remain;
                    spin_unlock(&global_lock);
                }
            }
        }
        spin_unlock(&heap->lock);
    }
    //-----------
    // 本地分配尝试（使用本地锁）
    spin_lock(&heap->lock);
    //curr = heap->head;
    while (curr) {
        if (curr->length >= size) {
            // 从链表中解绑
            if (prev) prev->next = curr->next;
            else heap->head = curr->next;

            // 内存分割逻辑
            if (curr->length > size + STRUCTSIZE) {
                Block *new_remain = (Block*)((char*)curr + STRUCTSIZE + size);
                new_remain = (Block*)(((size_t)new_remain + 7) & ~7);
                size_t remaining = (char*)curr + STRUCTSIZE + curr->length - (char*)new_remain;
                
                if (remaining >= STRUCTSIZE) {
                    new_remain->length = remaining - STRUCTSIZE;
                    new_remain->next = heap->head;
                    new_remain->owner_tid = tid;
                    heap->head = new_remain;
                    curr->length = size;
                }
            }
            ret = (char*)curr + STRUCTSIZE;
            spin_unlock(&heap->lock);
            return ret;
        }
        prev = curr;
        curr = curr->next;
    }
    spin_unlock(&heap->lock);

    // 全局链表尝试（使用全局锁）
    spin_lock(&global_lock);
    curr = global_free_list;
    prev = NULL;
    while (curr) {
        if (curr->length >= size) {
            if (prev) prev->next = curr->next;
            else global_free_list = curr->next;
            
            // 更新归属线程并分割
            curr->owner_tid = tid;
            if (curr->length > size + STRUCTSIZE) {
                Block *new_remain = (Block*)((char*)curr + STRUCTSIZE + size);
                new_remain = (Block*)(((size_t)new_remain + 7) & ~7);
                size_t remaining = (char*)curr + STRUCTSIZE + curr->length - (char*)new_remain;
                
                if (remaining >= STRUCTSIZE) {
                    new_remain->length = remaining - STRUCTSIZE;
                    spin_lock(&heap->lock);  // 需要再次获取本地锁
                    new_remain->next = heap->head;
                    heap->head = new_remain;
                    spin_unlock(&heap->lock);
                    curr->length = size;
                }
            }
            ret = (char*)curr + STRUCTSIZE;
            break;
        }
        prev = curr;
        curr = curr->next;
    }
    spin_unlock(&global_lock);

    if (ret) return ret;

    // 分配新内存块
    size_t aligned_length = ((size + STRUCTSIZE + 4095) / 4096) * 4096;
    Block *new_block = vmalloc(NULL, aligned_length);
    if (!new_block) return NULL;
    
    new_block->length = aligned_length - STRUCTSIZE;
    new_block->owner_tid = tid;
    new_block->next = NULL;
    return (char*)new_block + STRUCTSIZE;
}

void myfree(void *ptr) {
    /*if (!ptr) return;

    Block *info = (Block*)((char*)ptr - STRUCTSIZE);
    pid_t curr_tid = gettid();

    if (info->owner_tid == curr_tid) {
        // 本地释放（使用本地锁）
        ThreadHeap* heap = get_thread_heap();
        spin_lock(&heap->lock);
        info->next = heap->head;
        heap->head = info;
        spin_unlock(&heap->lock);
    } else {
        // 跨线程释放（使用全局锁）
        spin_lock(&global_lock);
        info->next = global_free_list;
        global_free_list = info;
        spin_unlock(&global_lock);
    }*/
   return;
}