#ifndef ZGET_SOURCE_HTTP_PARSE_H
#define ZGET_SOURCE_HTTP_PARSE_H

#include <stdbool.h>
#include <stdint.h>

/* Pure HTTP grammar parser kept separate from libcurl for unit fuzzing. */
bool zget_parse_content_range(const char *value, uint64_t *start,
                              uint64_t *end, uint64_t *total);

#endif
