#include "virtalloc/fast_hash.h"
#include "virtalloc/checksum.h"

size_t get_checksum(const size_t length, const char data[static length]) {
    const uint64_t h = fnv1a_hash(length, data);
    return h;
}
