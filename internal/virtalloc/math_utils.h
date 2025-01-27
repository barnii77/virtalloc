#ifndef MATH_UTILS_H
#define MATH_UTILS_H

#include <stddef.h>

#define align_to(size, align) ((((size) + (align) - 1) / (align)) * (align))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) < (b) ? (b) : (a))

size_t round_to_power_of_2(size_t x);

#endif
