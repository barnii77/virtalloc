#include "virtalloc/fast_hash.h"

uint64_t fnv1a_hash(const size_t len, const char data[static len]) {
    uint64_t hash = 0xcbf29ce484222325;

    // This is what the original slow version looks like. As you may be able to guess, the compiler is (understandably)
    // unable to auto-vectorize the loop and processing just one byte at a time is consistently about 8 times slower
    // than doing what I actually do below regarding processing 8 bytes at a time when possible (yes, I benchmarked it).
    // for (size_t i = 0; i < len; i++) {
    //     hash ^= data[i];
    //     hash *= 0x100000001b3;
    // }

    // fast hash for multiples of 8 bytes (processes 8 bytes at a time)
    size_t i;
    for (i = 0; i < len / sizeof(uint64_t); i++) {
        hash ^= ((uint64_t *) data)[i];
        hash *= 0x100000001b3;
    }
    // slow hash for the rest (processes 1 byte at a time)
    for (i *= sizeof(uint64_t); i < len; i++) {
        hash ^= data[i];
        hash *= 0x100000001b3;
    }

    return hash;
}
