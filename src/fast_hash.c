#include "virtalloc/fast_hash.h"

uint64_t fnv1a_hash(const size_t len, const char data[static len]) {
    uint64_t hash = 0xcbf29ce484222325;

    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 0x100000001b3;
    }

    return hash;
}
