#ifndef ZGET_FORMAT_H
#define ZGET_FORMAT_H

#include "error.h"
#include "source/source.h"
#include "zget.h"

#include <stdint.h>

struct zget_format;

/*
 * Format engines receive only limits that affect parsing or extraction. HTTP
 * request and redirect policy stays with the source that enforces it.
 */
struct zget_format_options {
    uint64_t max_metadata_bytes;
    uint64_t max_output_size;
};

int zget_format_open(struct zget_source *source,
                     const struct zget_format_options *options,
                     struct zget_error_state *error,
                     struct zget_format **out_format);
int zget_format_find(struct zget_format *format, const char *member,
                     zget_entry *entry);
int zget_format_extract(struct zget_format *format, const zget_entry *entry,
                        zget_write_cb write_cb, void *userdata);
void zget_format_close(struct zget_format *format);

#endif
