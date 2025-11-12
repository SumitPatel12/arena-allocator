#ifndef ARENA_ALLOCATOR_H
#define ARENA_ALLOCATOR_H

#include "bitmap.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <sys/mman.h>

// Framed/Slotted arena, all allocation will be of the same size.
struct Arena {
    size_t capacity;
    char* base;
    size_t slot_size;
    Bitmap* bitmap;
    std::mutex bitmap_mutex;
    std::atomic_int16_t slots_in_use;

    Arena(size_t capacity, size_t page_size);
    ~Arena();
    char* allocate(size_t size);
    void free(char* ptr, size_t size);
};

// Lock-free arena allocator using BitmapLockFree for thread-safe allocation without mutexes.
struct ArenaLockFree {
    size_t capacity;
    char* base;
    size_t slot_size;
    BitmapLockFree* bitmap;
    std::atomic_int16_t slots_in_use;

    ArenaLockFree(size_t capacity, size_t page_size);
    ~ArenaLockFree();
    char* allocate(size_t size);
    void free(char* ptr, size_t size);
    uint64_t get_cas_retries() const {
        return bitmap->get_cas_retries();
    }
};

#endif // ARENA_ALLOCATOR_H
