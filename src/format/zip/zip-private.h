#ifndef ZGET_FORMAT_ZIP_PRIVATE_H
#define ZGET_FORMAT_ZIP_PRIVATE_H

#include "format/format-private.h"

struct zget_zip_format {
    struct zget_format format;
    uint64_t cd_offset;
    uint64_t cd_size;
    uint64_t entry_count;
};

/*
 * A Central Directory lookup produces this private, immutable extraction plan.
 * It lives only for one synchronous extraction, so hostile offsets can be
 * validated where they are parsed without exposing a forgeable public handle.
 */
struct zget_zip_entry {
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    uint64_t local_header_offset;
    uint32_t crc32;
    uint16_t compression_method;
    uint16_t flags;
};

int zget_zip_extract_payload(struct zget_zip_format *zip,
                             const struct zget_zip_entry *entry,
                             uint64_t data_offset,
                             zget_write_cb write_cb, void *userdata);

#endif
