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

static const int tab64[64] = {
    63, 0, 58, 1, 59, 47, 53, 2,
    60, 39, 48, 27, 54, 33, 42, 3,
    61, 51, 37, 40, 49, 18, 28, 20,
    55, 30, 34, 11, 43, 14, 22, 4,
    62, 57, 46, 52, 38, 26, 32, 41,
    50, 36, 17, 19, 29, 10, 13, 21,
    56, 45, 25, 31, 35, 16, 9, 12,
    44, 24, 15, 8, 23, 7, 6, 5
};

int ilog2l(size_t value) {
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value |= value >> 32;
    return tab64[(value - (value >> 1)) * 0x07EDD5E59A4E28C2 >> 58];
}
