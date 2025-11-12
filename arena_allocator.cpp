// TODO: There are a lot of comments, after the implementation and benchmarkin are done remove the unnecessary ones.

/*
   The main target for this exercise is to get a feel for arena-allocators. It seems like a framed/slotted
   arena-allocator would be a good fit for buffer pools.

   For use in a buffer pool, I need pages, fixed size. There may be two different sizes to consider since the log page
   and the actual data pages might differ in size but for now lets just consider one size fits all.

   So what should the Arena keep track of?
   Listing some of the things that come
   to mind:
     - The total size of the arena.
     - The starting point of the arena.
     - The fixed size of each page.
     - Some kind of mechanism that would let me know which page/slot is in use and which is not. This one keeps track of
       index.
     - Total slots in use currently. This is just a number.
   The data to be stores will be per byte basis so something like a u8, mostly some kind of container of u8.

   The whole thing will likely be shared across threads since the Buffer pool is initialized only once, when the DB is
   created. So to keep count of the number of currently allocated slots we'd need a type that supports atomic operations
   for thread safety.

   Thats everything I can think of now. The buffer pool will likely wrap the arena into a struct to keep trak of the
   areana and some of its own metadata. Something I will deal with down the line :shrug:.
   This implementation is just an exploratory one.
*/
/*
   You'll see things like :shrug:, :laugh:, etc, throughout the code, my editor does not support showing emotes I just
   like sprinking those around, makes coding FUN, especially when you revisit your code.
*/

#include "arena_allocator.h"

// TODO: Understand the impact and significance of alignment.
// Well be going the route of mmap for this one: https://stackoverflow.com/questions/45972/mmap-vs-reading-blocks
// Since the buffer_pool would live for as long as the DB instance does, and we'll be accessing randomly, that seems
// to make more sense. I'll benchmark the difference b/w malloc and mmap once I'm done with the initialization.
// Arena will default to a 20MB region with a page size of 4KB.
//
// The capacity will be adjusted so that it is an exact multiple of page_size.
// Ideally the page_size will be a power of 2 for good memory alignment.
// For the bitmap implementation the number of slots in the arena need to be a multiple of 64.
Arena::Arena(size_t capacity, size_t page_size) {
    // Calculate initial number of slots that would fit
    size_t num_slots = (capacity + page_size - 1) / page_size;

    // Ensure minimum of 64 slots, or round up to next multiple of 64
    if (num_slots < 64) {
        num_slots = 64;
    } else {
        // Bit manipulation way of getting to the next multiple of 64.
        // Neat trick, we add a 63 to get the current atleast to the next multiple of 64 then just logical & with
        // the compliemnt of 63 meaning we make the number divisible by 64. In this case it sets the las 5 bits to 0
        // which ensures that the number is divisible by 64.
        num_slots = (num_slots + 63) & ~63;
    }

    // Capacity is adjusted to be an exact multiple of page_size and slot count
    this->capacity = num_slots * page_size;
    this->slot_size = page_size;

    // For the buffer pool we'll have to read and write the pages, the memory is not backed by a file, and it's
    // private to our process.
    void* arena_start = mmap(NULL, this->capacity, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    // TODO: Double check if panicking is the right thing to do.
    // Not sure if we should panic but it'll do for now.
    if (arena_start == MAP_FAILED) {
        perror("mmap failed");
        exit(EXIT_FAILURE);
    }

    this->bitmap = new Bitmap(num_slots);
    this->base = (char*)arena_start;
    this->slots_in_use.store(0);
}

Arena::~Arena() {
    int unmapped = munmap(base, capacity);

    if (unmapped == -1) {
        perror("munmap failed");
        exit(EXIT_FAILURE);
    }

    delete bitmap;
}

char* Arena::allocate(size_t size) {
    if (size == 0) {
        return NULL;
    }
    size_t slots_required = (size + slot_size - 1) / slot_size;
    char* allocation_base = NULL;

    std::lock_guard<std::mutex> lock(bitmap_mutex);

    if (slots_required == 1) {
        int slot_idx = bitmap->allocate_one();
        if (slot_idx != -1) {
            allocation_base = base + slot_size * slot_idx;
            slots_in_use.fetch_add(1, std::memory_order_relaxed);
        }
    }

    return allocation_base;
}

void Arena::free(char* ptr, size_t size) {
    if (ptr == NULL || size == 0) {
        return;
    }

    if (ptr < base || ptr >= base + capacity) {
        return;
    }

    size_t start_slot = (ptr - base) / slot_size;
    size_t slots_to_free = (size + slot_size - 1) / slot_size;

    std::lock_guard<std::mutex> lock(bitmap_mutex);

    for (size_t i = start_slot; i < start_slot + slots_to_free && i < bitmap->num_slots; ++i) {
        bitmap->free_slot(i);
    }

    // If I use a lock free implementation would this work?
    // Likely yes since even if this is not mutex protected, the operation is still atomic. But I'll need to think
    // if there is a scenario where there can be inconsistencies in the snapshot in time.
    slots_in_use.fetch_sub(slots_to_free, std::memory_order_relaxed);
}

// Lock-free arena allocator using BitmapLockFree for thread-safe allocation without mutexes.
// This implementation uses atomic operations and compare-and-swap for all bitmap operations.
// Single-slot allocation only (same as Arena for now).
// Lock-free arena constructor - same initialization as Arena but uses BitmapLockFree.
// The capacity will be adjusted so that it is an exact multiple of page_size.
// Ideally the page_size will be a power of 2 for good memory alignment.
// For the bitmap implementation the number of slots in the arena need to be a multiple of 64.
ArenaLockFree::ArenaLockFree(size_t capacity, size_t page_size) {
    // Calculate initial number of slots that would fit
    size_t num_slots = (capacity + page_size - 1) / page_size;

    // Ensure minimum of 64 slots, or round up to next multiple of 64
    if (num_slots < 64) {
        num_slots = 64;
    } else {
        // Bit manipulation way of getting to the next multiple of 64.
        // Neat trick, we add a 63 to get the current atleast to the next multiple of 64 then just logical & with
        // the compliemnt of 63 meaning we make the number divisible by 64. In this case it sets the las 5 bits to 0
        // which ensures that the number is divisible by 64.
        num_slots = (num_slots + 63) & ~63;
    }

    // Capacity is adjusted to be an exact multiple of page_size and slot count
    this->capacity = num_slots * page_size;
    this->slot_size = page_size;

    // For the buffer pool we'll have to read and write the pages, the memory is not backed by a file, and it's
    // private to our process.
    void* arena_start = mmap(NULL, this->capacity, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);

    // TODO: Double check if panicking is the right thing to do.
    // Not sure if we should panic but it'll do for now.
    if (arena_start == MAP_FAILED) {
        perror("mmap failed");
        exit(EXIT_FAILURE);
    }

    this->bitmap = new BitmapLockFree(num_slots);
    this->base = (char*)arena_start;
    this->slots_in_use.store(0);
}

ArenaLockFree::~ArenaLockFree() {
    int unmapped = munmap(base, capacity);

    if (unmapped == -1) {
        perror("munmap failed");
        exit(EXIT_FAILURE);
    }

    delete bitmap;
}

// Lock-free single-slot allocation.
// Uses BitmapLockFree::allocate_one() which performs atomic CAS operations.
// Currently only supports single-slot allocations (slots_required == 1).
char* ArenaLockFree::allocate(size_t size) {
    if (size == 0) {
        return NULL;
    }
    size_t slots_required = (size + slot_size - 1) / slot_size;
    char* allocation_base = NULL;

    // No mutex needed - bitmap operations are lock-free with atomic CAS
    if (slots_required == 1) {
        int slot_idx = bitmap->allocate_one();
        if (slot_idx != -1) {
            allocation_base = base + slot_size * slot_idx;
            // Atomic increment without mutex - safe because it's atomic
            slots_in_use.fetch_add(1, std::memory_order_relaxed);
        }
    }

    return allocation_base;
}

// Lock-free deallocation.
// Uses BitmapLockFree::free_slot() which performs atomic fetch_or operations.
void ArenaLockFree::free(char* ptr, size_t size) {
    if (ptr == NULL || size == 0) {
        return;
    }

    if (ptr < base || ptr >= base + capacity) {
        return;
    }

    size_t start_slot = (ptr - base) / slot_size;
    size_t slots_to_free = (size + slot_size - 1) / slot_size;

    // No mutex needed - all operations are lock-free
    for (size_t i = start_slot; i < start_slot + slots_to_free && i < bitmap->num_slots; ++i) {
        bitmap->free_slot(i);
    }

    // TODO: This might not be how we want to do stuff. :sweating:
    // Atomic decrement - safe without mutex protection.
    // The count may be slightly inconsistent in the moment if concurrent frees occur,
    // but will eventually be consistent since each free atomically decrements.
    slots_in_use.fetch_sub(slots_to_free, std::memory_order_relaxed);
}
