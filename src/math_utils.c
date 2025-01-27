#include "virtalloc/math_utils.h"

size_t round_to_power_of_2(size_t x) {
    // since overflow is technically UB, handle this case separately
    if (x == 0)
        return 1;
    // decrement x in preparation for the bit magic to follow (this handles the case where x is already a power of 2)
    x--;
    // this evil bit magic propagates the leftmost 1 bit to all bits to the right of that, e.g. 0b001010 -> 0b001111
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x |= x >> 32;
    // x now looks like 0b001111, add 1 to get next power of 2, e.g. 0b001111 -> 0b010000
    return ++x;
}