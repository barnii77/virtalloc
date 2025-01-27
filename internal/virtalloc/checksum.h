#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <stddef.h>

size_t get_checksum(size_t length, const char data[static length]);

#endif
