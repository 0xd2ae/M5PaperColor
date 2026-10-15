#pragma once
#include <stddef.h>
#include <stdint.h>
/* Returns an allocated, NUL-terminated body; rejects damaged or oversized gzip. */
char *pc_gzip_decode(const uint8_t *input,size_t length,size_t maximum);
