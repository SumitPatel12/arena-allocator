// We'll have a bitmap that keeps track of the slot allocation state from the arena allocator.
// Ok, so from searching around a bit, bitmaps are generally a collection of cache words or just words. Words being 64
// or 32 bit in size depending on the machine architecture. We'll just consider 64 bit for this implementation since
// that's what most modern machines pack.
//
// Oh and of course Bitmaps require bit manipulation and I suck at bit manipulation. :pulling_out_hair:

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

struct Bitmap {
    uint32_t num_slots;
    std::vector<uint64_t> words;

    explicit Bitmap(uint32_t num_slots) : num_slots(num_slots) {
        if (num_slots % 64 != 0) {
            perror("number of slots must be a multiple of 64");
            exit(EXIT_FAILURE);
        }

        // Since each word is 64 bits long we just get the number by dividing slots by 64.
        size_t num_words = num_slots / 64;
        // Initially all of the pages are free.
        words.assign(num_words, (uint64_t)0);
    }
};
