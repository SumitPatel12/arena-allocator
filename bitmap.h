// We'll have a bitmap that keeps track of the slot allocation state from the arena allocator.
// Ok, so from searching around a bit, bitmaps are generally a collection of cache words or just words. Words being 64
// or 32 bit in size depending on the machine architecture. We'll just consider 64 bit for this implementation since
// that's what most modern machines pack.
//
// Oh and of course Bitmaps require bit manipulation and I suck at bit manipulation. :pulling_out_hair:

#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
