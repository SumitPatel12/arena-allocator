// We'll have a bitmap that keeps track of the slot allocation state from the arena allocator.
// Ok, so from searching around a bit, bitmaps are generally a collection of cache words or just words. Words being 64
// or 32 bit in size depending on the machine architecture. We'll just consider 64 bit for this implementation since
// that's what most modern machines pack.
//
// Oh and of course Bitmaps require bit manipulation and I suck at bit manipulation. :pulling_out_hair:

#include <atomic>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>

// BitMap the track the usage of slots in the arena_allocator.
// 1 means free, 0 means allocated.
// I was actually going to go for 1 as free and 0 as allocated, but it turns out that we've got hardware support for
// finding out which bit is set in a given set of bits which makes the allocation of single slots a lot easier.
struct Bitmap {
    std::uint32_t WORD_SHIFT = 6;
    std::uint32_t WORD_LENGTH = 64;
    std::uint32_t WORD_MASK = 63;
    std::uint64_t FULLY_ALLOCATED = 0;
    std::uint64_t FULLY_FREE = UINT64_MAX;
    // I could likely resuse the mask since it's the same value but that wouldn't be very readable.
    std::uint64_t MAX_IDX = 63;

    uint32_t num_slots;
    std::vector<uint64_t> words;

    explicit Bitmap(uint32_t num_slots) : num_slots(num_slots) {
        if (num_slots % WORD_LENGTH != 0) {
            perror("number of slots must be a multiple of 64");
            exit(EXIT_FAILURE);
        }

        // Since each word is 64 bits long we just get the number by dividing slots by 64.
        size_t num_words = num_slots / WORD_LENGTH;
        // Initially all of the pages are free.
        words.assign(num_words, FULLY_FREE);
    }

    std::pair<size_t, uint32_t> get_word_and_bit_index_from_slot_index(uint32_t slot_idx) const;
    size_t get_slot_index_from_word_and_bit_index(size_t word_idx, uint32_t bit_idx) const;
    int allocate_one();
    int allocate_many(uint32_t num_slots);
    int free_slot(uint32_t slot_idx);
};

/// Returns the word and bit index given the slot index.
inline std::pair<size_t, uint32_t> Bitmap::get_word_and_bit_index_from_slot_index(uint32_t slot_idx) const {
    // for unsigned ints >> is equivalent to divide by power of 2 so thats pretty much it for the first one.
    // Since each number is to map to a word, we're only interested in the last 6 bits (i.e. the remainder after
    // dividing by 64) since that would give us the index in the word.
    return {slot_idx >> WORD_SHIFT, slot_idx & WORD_MASK};
}

/// Returns the slot_index given the word, and bit index
inline size_t Bitmap::get_slot_index_from_word_and_bit_index(size_t word_idx, uint32_t bit_idx) const {
    return word_idx << WORD_SHIFT | bit_idx;
}

// Just scanning and allocating the first free slot is likely going to result in internal fragmentation. Will do for the
// first pass.
/// Allocates one free slot from the bitmap, returns the index of the bitmap if allocation was successful, -1 otherwise.
inline int Bitmap::allocate_one() {
    for (size_t word_idx = 0; word_idx < words.size(); ++word_idx) {
        uint64_t word = words[word_idx];
        if (word != FULLY_ALLOCATED) {
            // Count trailing zeros, subtract from 63 since we're counting from the back.
            // For example consider the second bit in the 3rd word.
            // W0 W1 W2 0100... -> W3a          (Wlen - bit_idx)
            // Then the index should be 3 * 64 + (63 - 1) = 254 Which is correct, since slot index themsef start at 0.
            int bit_idx = MAX_IDX - std::countl_zero(word);
            // Since we're allocating the slot, we need to set it to 0.
            words[word_idx] &= ~(1ULL << bit_idx);
            return get_slot_index_from_word_and_bit_index(word_idx, bit_idx);
        }
    }
    return -1;
}

inline int Bitmap::allocate_many(uint32_t num_slots) {
    return -1;
}

inline int Bitmap::free_slot(uint32_t slot_idx) {
    size_t word_idx, bit_idx;
    std::tie(word_idx, bit_idx) = get_word_and_bit_index_from_slot_index(slot_idx);
    words[word_idx] |= (1ULL << bit_idx);
    return 0;
}

// Lock-free bitmap using atomic operations and compare-and-swap for thread-safe allocation.
// 1 means free, 0 means allocated (same convention as Bitmap).
struct BitmapLockFree {
    std::uint32_t WORD_SHIFT = 6;
    std::uint32_t WORD_LENGTH = 64;
    std::uint32_t WORD_MASK = 63;
    std::uint64_t FULLY_ALLOCATED = 0;
    std::uint64_t FULLY_FREE = UINT64_MAX;
    std::uint64_t MAX_IDX = 63;

    uint32_t num_slots;
    std::deque<std::atomic<std::uint64_t>> words;

    explicit BitmapLockFree(uint32_t num_slots) : num_slots(num_slots) {
        if (num_slots % WORD_LENGTH != 0) {
            perror("number of slots must be a multiple of 64");
            exit(EXIT_FAILURE);
        }

        size_t num_words = num_slots / WORD_LENGTH;
        for (size_t i = 0; i < num_words; ++i) {
            words.emplace_back(FULLY_FREE);
        }
    }

    std::pair<size_t, uint32_t> get_word_and_bit_index_from_slot_index(uint32_t slot_idx) const;
    size_t get_slot_index_from_word_and_bit_index(size_t word_idx, uint32_t bit_idx) const;
    int allocate_one();
    int free_slot(uint32_t slot_idx);
};

/// Returns the word and bit index given the slot index.
inline std::pair<size_t, uint32_t> BitmapLockFree::get_word_and_bit_index_from_slot_index(uint32_t slot_idx) const {
    return {slot_idx >> WORD_SHIFT, slot_idx & WORD_MASK};
}

/// Returns the slot_index given the word, and bit index
inline size_t BitmapLockFree::get_slot_index_from_word_and_bit_index(size_t word_idx, uint32_t bit_idx) const {
    return (word_idx << WORD_SHIFT) | bit_idx;
}

/// Lock-free single-slot allocation using atomic compare-and-swap (CAS).
///
/// Algorithm:
/// 1. Scan through words to find one that has at least one free bit (1).
/// 2. For each word with free bits, enter a CAS retry loop:
///    - Pick one free bit from the locally observed word value
///    - Compute new_word by clearing that bit (1 -> 0)
///    - Attempt CAS to atomically update from observed to new_word
///    - If CAS succeeds, return the slot index
///    - If CAS fails, the observed value is updated to current word value; retry with new value
/// 3. If all words are fully allocated, return -1
///
/// Correctness guarantees:
/// - Only one thread can successfully claim a specific bit via CAS, preventing double allocation
/// - CAS failure means another thread modified the word; we retry with the updated value
/// - Never clear a bit that wasn't 1 in the value we observed (CAS ensures atomicity)
/// - Memory ordering (acquire/release) ensures proper synchronization with free_slot
///
/// Lock-free progress:
/// - At least one competing thread will succeed in its CAS if free slots exist
/// - Failed threads observe progress (word changes) and retry with updated state
/// - No thread can block others indefinitely (no locks, no waiting)
inline int BitmapLockFree::allocate_one() {
    // Scan all words looking for one with free bits
    for (size_t word_idx = 0; word_idx < words.size(); ++word_idx) {
        // Load the current word value with acquire semantics to see any prior frees
        uint64_t observed = words[word_idx].load(std::memory_order_acquire);

        // CAS retry loop for this word
        while (observed != FULLY_ALLOCATED) {
            // Find the highest free bit (1) in the observed word
            // countl_zero returns number of leading zeros, subtract from 63 to get bit index
            int bit_idx = static_cast<int>(MAX_IDX - std::countl_zero(observed));
            uint64_t bit_mask = (1ULL << bit_idx);

            // Compute the new word value with the chosen bit cleared (allocated)
            uint64_t new_word = observed & ~bit_mask;

            // Attempt to atomically claim the bit
            // compare_exchange_weak returns true if CAS succeeded, false otherwise
            // On failure, observed is updated to the current word value
            if (words[word_idx].compare_exchange_weak(
                    observed,                  // expected value (updated on failure)
                    new_word,                  // desired new value
                    std::memory_order_acq_rel, // success: acquire+release for synchronization
                    std::memory_order_acquire  // failure: acquire to see concurrent updates
                    )) {
                // CAS succeeded: we atomically claimed the slot
                return static_cast<int>(
                    get_slot_index_from_word_and_bit_index(word_idx, static_cast<uint32_t>(bit_idx)));
            }

            // CAS failed due to contention or spurious failure
            // observed now contains the updated word value, loop to try again
            // If another thread allocated our target bit, observed may still have other free bits
            // If another thread allocated all bits, observed == FULLY_ALLOCATED and we exit loop
        }
        // This word is now fully allocated, move to next word
    }
    // No free slots found in any word
    return -1;
}

/// Free a previously allocated slot, making it available for reuse.
/// Uses atomic fetch_or with release semantics to ensure proper memory ordering.
inline int BitmapLockFree::free_slot(uint32_t slot_idx) {
    size_t word_idx;
    uint32_t bit_idx;
    std::tie(word_idx, bit_idx) = get_word_and_bit_index_from_slot_index(slot_idx);

    // Atomically set the bit to 1 (free)
    // Release semantics ensure that any writes to the slot happen-before
    // the next thread that allocates it (via acquire in allocate_one_lock_free)
    words[word_idx].fetch_or(1ULL << bit_idx, std::memory_order_release);
    return 0;
}
