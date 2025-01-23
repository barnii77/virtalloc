#ifndef FAST_HASH_H
#define FAST_HASH_H

#include <stddef.h>
#include <stdint.h>

uint64_t fnv1a_hash(size_t len, const char data[static len]);

#endif
