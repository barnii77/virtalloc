#include "virtalloc/fast_hash.h"
#include "virtalloc/checksum.h"

int get_checksum(const size_t length, const char data[static length]) {
    const uint64_t h = fnv1a_hash(length, data);
    return (int) h + (int) (h >> 32);
}
