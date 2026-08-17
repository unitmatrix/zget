#ifndef ZGET_ZIP_PARSE_H
#define ZGET_ZIP_PARSE_H

#include <stddef.h>
#include <stdint.h>

struct zget_eocd {
    uint64_t cd_offset;
    uint64_t cd_size;
    uint64_t entries;
    uint64_t eocd_offset;
    uint64_t zip64_offset;
    uint64_t zip64_record_size;
    int zip64;
};

struct zget_cd_fixed {
    uint16_t version_needed, flags, method, name_length, extra_length;
    uint16_t comment_length, disk;
    uint32_t crc32, compressed_size, uncompressed_size, local_offset;
};

struct zget_local_fixed {
    uint16_t flags, method, name_length, extra_length;
};

int zget_zip_parse_eocd(const unsigned char *tail, size_t length,
                        uint64_t tail_offset, uint64_t archive_size,
                        struct zget_eocd *out);
int zget_zip_parse_zip64(const unsigned char *record, size_t length,
                         struct zget_eocd *out);
int zget_zip_parse_zip64_extra(const unsigned char *extra, size_t length,
                               uint32_t size32, uint32_t compressed32,
                               uint32_t offset32, uint16_t disk16,
                               uint64_t *size, uint64_t *compressed,
                               uint64_t *offset, uint32_t *disk);
int zget_zip_parse_cd_fixed(const unsigned char *data, size_t length,
                            struct zget_cd_fixed *out);
int zget_zip_parse_local_fixed(const unsigned char *data, size_t length,
                               struct zget_local_fixed *out);
int zget_zip_fuzz_cd_stream(const unsigned char *data, size_t length,
                            size_t chunk_size);

#endif
