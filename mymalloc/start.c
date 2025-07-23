#include <mymalloc.h>

#ifdef FREESTANDING

// This checks whether your code works for
// freestanding environments.

void _start() {
}

int main() {
    return 0;
}

void *vmalloc(void *addr, size_t length){
    return NULL;
}

void vmfree(void *addr, size_t length){

}

#else

#include <sys/mman.h>

#define MAP_ANONYMOUS 0x20

void *vmalloc(void *addr, size_t length) {
    // length must be aligned to page size (4096).
    void *result = mmap(addr, length, PROT_READ | PROT_WRITE, 
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED) {
        return NULL;
    }
    return result;
}

void vmfree(void *addr, size_t length) {
    munmap(addr, length);
}

#endif
