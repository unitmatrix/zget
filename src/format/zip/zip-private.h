#ifndef ZGET_FORMAT_ZIP_PRIVATE_H
#define ZGET_FORMAT_ZIP_PRIVATE_H

#include "format/format-private.h"

struct zget_zip_format {
    struct zget_format format;
    uint64_t cd_offset;
    uint64_t cd_size;
    uint64_t entry_count;
};

int zget_zip_extract_payload(struct zget_zip_format *zip,
                             const zget_entry *entry, uint64_t data_offset,
                             zget_write_cb write_cb, void *userdata);

#endif
