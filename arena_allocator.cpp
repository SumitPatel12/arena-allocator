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

#include <atomic>
#include <cstddef>
#include <mutex>
#include <sys/mman.h>
#include <vector>

// TODO: Understand the impact and significance of alignment.
// Framed/Slotted arena, all allocation will be of the same size.
struct Arena {
    // Capacity of the arena
    size_t capacity;
    // Pointer to the base of the arena.
    char* base;
    // The frame/slot size.
    size_t slot_size;
    // Index of slots in use.
    // FIXME: Use a BitMap with a SpinLock: https://forum.osdev.org/viewtopic.php?t=29520, bool is too much overhead,
    // and it seems like bitmap operations are quite fast.
    // Actually I'm not sure if its should be a spinlock or mutex :shrug:.
    std::vector<bool> used_slots_map;
    // Mutex to protect used_slots_map for thread safety.
    std::mutex slots_map_mutex;
    // Planning on the max arena size being 20MB and the page size being 4KB which means I can a maximum of 5120 slots.
    // A 16 bit unsigned integer should suffice for that purpose. A 32 bit unsigned integer might be better for
    // alignment I don't know yet.
    // TODO: After implementation check with a 32 bit integer.
    // TODO: Turns out for MacOS (which I use) the page size is 16KB. Test the alignment and allocation with 4KB, 8KB,
    // and 16KB.
    // Total number of slots being used at any given point. Needs to be thread safe.
    std::atomic_int16_t slots_in_use;

    // Well be going the route of mmap for this one: https://stackoverflow.com/questions/45972/mmap-vs-reading-blocks
    // Since the buffer_pool would live for as long as the DB instance does, and we'll be accessing randomly, that seems
    // to make more sense. I'll benchmark the difference b/w malloc and mmap once I'm done with the initialization.
    // Arena will default to a 20MB region with a page size of 4KB.
    //
    // The capacity will be adjusted so that it is an exact multiple of page_size.
    // Ideally the page_size will be a power of 2 for good memory alignment.
    // For the bitmap implementation the number of slots in the arena need to be a multiple of 64.
    Arena(size_t capacity, size_t page_size) {
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
            num_slots = (num_slots + 63) & !63;
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

        this->used_slots_map.resize(num_slots, false);
        this->base = (char*)arena_start;
        this->slots_in_use.store(0);
    }
};

Arena* arena_create(size_t capacity = 1024 * 1024 * 8 * 20, size_t page_size = 1024 * 8 * 4) {
    if (capacity == 0) {
        return NULL;
    }

    Arena* arena = new Arena(capacity, page_size);
    return arena;
};

void arena_destroy(Arena* arena) {
    if (arena != NULL) {
        int unmapped = munmap(arena->base, arena->capacity);

        // Panicking here is mighty incorrect I think. But once again I'll let it slide for now.
        if (unmapped == -1) {
            perror("mmap failed");
            exit(EXIT_FAILURE);
        }

        delete arena;
    }
}

// TODO: Think of how the allocation strategy should work. The size of page requests are going to be a multiple of the
// arena page_size. To avoid fragmentation and have some space for contiguous blocks a better strategy would be needed,
// for now we'll just go with allocating by iterating over the vector and finding the first match.
char* arena_allocate(Arena* arena, size_t size) {
    if (size == 0 || arena == NULL) {
        return NULL;
    }
    size_t slots_required = (size + arena->slot_size - 1) / arena->slot_size;
    char* allocation_base = NULL;

    // Neat wrapper right here, it makes sure the lock is released even if the function that locked it thorws an
    // exception.
    // TODO: Check if this would incur any overhead and if manual operations are preferred.
    std::lock_guard<std::mutex> lock(arena->slots_map_mutex);

    // This is likely once again not the best way to do things.
    // TODO: Optmize this.
    for (int i = 0; i < arena->used_slots_map.size(); ++i) {
        if (slots_required == 1 && !arena->used_slots_map[i]) {
            arena->used_slots_map[i] = true;
            allocation_base = arena->base + arena->slot_size * i;
            arena->slots_in_use.fetch_add(1);
            break;
        }

        if (!arena->used_slots_map[i]) {
            int j = i;
            // Check for contigious blocks only, so we short-circuit if we find any occupied slots before we reach our
            // required count.
            while (j < arena->used_slots_map.size() && !arena->used_slots_map[j] && (j - i + 1) != slots_required) {
                ++j;
            }

            // Need to recheck the condition again cause anything could be wrong here.
            if (j < arena->used_slots_map.size() && !arena->used_slots_map[j] && (j - i + 1) == slots_required) {
                for (int k = i; k <= j; ++k) {
                    arena->used_slots_map[k] = true;
                }

                allocation_base = arena->base + arena->slot_size * i;
                arena->slots_in_use.fetch_add(slots_required);
                break;
            }
            // Since we either reached the end or j was at a slot that was occupied we'd rather skip that index.
            i = j + 1;
        }
    }

    return allocation_base;
}

// Finally a one that's simple :wiping_sweat:
void arena_free(Arena* arena, char* ptr, size_t size) {
    if (arena == NULL || ptr == NULL || size == 0) {
        return;
    }

    if (ptr < arena->base || ptr >= arena->base + arena->capacity) {
        return;
    }

    // TODO: Check if this needs to be ceiled, probably yes.
    // I see why OS always talks about never trusting the user.
    size_t start_slot = (ptr - arena->base) / arena->slot_size;
    size_t slots_to_free = (size + arena->slot_size - 1) / arena->slot_size;

    std::lock_guard<std::mutex> lock(arena->slots_map_mutex);

    for (size_t i = start_slot; i < start_slot + slots_to_free && i < arena->used_slots_map.size(); ++i) {
        arena->used_slots_map[i] = false;
    }

    arena->slots_in_use.fetch_sub(slots_to_free);
}

int main() {
    return 0;
}
