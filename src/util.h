#ifndef ZGET_UTIL_H
#define ZGET_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool zget_u64_add(uint64_t a, uint64_t b, uint64_t *result);
bool zget_range_valid(uint64_t offset, uint64_t length, uint64_t total);
uint16_t zget_le16(const unsigned char *p);
uint32_t zget_le32(const unsigned char *p);
uint64_t zget_le64(const unsigned char *p);
bool zget_valid_utf8(const unsigned char *s, size_t n);
char *zget_strdup(const char *s);

#endif
