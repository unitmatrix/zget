#include "format/format.h"
#include "source/source-private.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct memory_source {
    struct zget_source source;
    const unsigned char *data;
    size_t length;
    unsigned int reads;
    bool closed;
};

struct output_buffer {
    unsigned char data[16];
    size_t length;
};

struct member_buffer {
    char name[16];
    size_t name_length;
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    uint32_t crc32;
    uint16_t compression_method;
    int64_t mtime;
    unsigned int count;
};

static int deliver(struct memory_source *memory, uint64_t offset,
                   uint64_t length, zget_source_write_cb write_cb,
                   void *userdata)
{
    enum zget_source_action action;

    if (offset > memory->length || length > memory->length - offset)
        return ZGET_EINVAL;
    ++memory->reads;
    action = write_cb(userdata, memory->data + (size_t)offset, (size_t)length);
    if (action != ZGET_SOURCE_ERROR)
        return ZGET_OK;

    /* Format callbacks set the useful diagnostic before rejecting input. */
    return memory->source.error->code != ZGET_OK ?
           memory->source.error->code : ZGET_EZIP;
}

static int memory_read_range(struct zget_source *source, uint64_t offset,
                             uint64_t length,
                             zget_source_write_cb write_cb, void *userdata)
{
    return deliver((struct memory_source *)source, offset, length,
                   write_cb, userdata);
}

static int memory_read_suffix(struct zget_source *source, uint64_t length,
                              zget_source_write_cb write_cb, void *userdata)
{
    struct memory_source *memory = (struct memory_source *)source;
    uint64_t take = length < memory->length ? length : memory->length;

    return deliver(memory, memory->length - take, take, write_cb, userdata);
}

static void memory_close(struct zget_source *source)
{
    ((struct memory_source *)source)->closed = true;
}

static const struct zget_source_ops memory_ops = {
    memory_read_range,
    memory_read_suffix,
    memory_close
};

static void put16(unsigned char *p, uint16_t value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
}

static void put32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

static size_t make_stored_zip(unsigned char *zip)
{
    static const unsigned char name[] = "a.txt";
    static const unsigned char payload[] = "hello";
    const uint32_t crc = 0x3610a686u;
    size_t pos = 0, cd_offset, cd_size;

    /* Local Header followed immediately by one uncompressed payload. */
    memset(zip, 0, 113);
    put32(zip + pos, 0x04034b50u);
    put16(zip + pos + 4, 20);
    put32(zip + pos + 14, crc);
    put32(zip + pos + 18, sizeof(payload) - 1);
    put32(zip + pos + 22, sizeof(payload) - 1);
    put16(zip + pos + 26, sizeof(name) - 1);
    pos += 30;
    memcpy(zip + pos, name, sizeof(name) - 1);
    pos += sizeof(name) - 1;
    memcpy(zip + pos, payload, sizeof(payload) - 1);
    pos += sizeof(payload) - 1;

    /* The Central Directory is the lookup authority for size, CRC, and offset. */
    cd_offset = pos;
    put32(zip + pos, 0x02014b50u);
    put16(zip + pos + 4, 20);
    put16(zip + pos + 6, 20);
    /* 2026-04-08 13:40 in the ZIP-local packed DOS representation. */
    put16(zip + pos + 12, (uint16_t)((13u << 11) | (40u << 5)));
    put16(zip + pos + 14,
          (uint16_t)(((2026u - 1980u) << 9) | (4u << 5) | 8u));
    put32(zip + pos + 16, crc);
    put32(zip + pos + 20, sizeof(payload) - 1);
    put32(zip + pos + 24, sizeof(payload) - 1);
    put16(zip + pos + 28, sizeof(name) - 1);
    put32(zip + pos + 42, 0);
    pos += 46;
    memcpy(zip + pos, name, sizeof(name) - 1);
    pos += sizeof(name) - 1;
    cd_size = pos - cd_offset;

    put32(zip + pos, 0x06054b50u);
    put16(zip + pos + 8, 1);
    put16(zip + pos + 10, 1);
    put32(zip + pos + 12, (uint32_t)cd_size);
    put32(zip + pos + 16, (uint32_t)cd_offset);
    pos += 22;
    return pos;
}

static int collect(void *userdata, const void *data, size_t length)
{
    struct output_buffer *output = userdata;

    if (length > sizeof(output->data) - output->length)
        return 1;
    memcpy(output->data + output->length, data, length);
    output->length += length;
    return 0;
}

static int reject_output(void *userdata, const void *data, size_t length)
{
    (void)userdata;
    (void)data;
    (void)length;
    return 1;
}

static int collect_member(void *userdata, const zget_member_info *member)
{
    struct member_buffer *output = userdata;

    if (member->name_length > sizeof(output->name))
        return 1;
    memcpy(output->name, member->name, member->name_length);
    output->name_length = member->name_length;
    output->compressed_size = member->compressed_size;
    output->uncompressed_size = member->uncompressed_size;
    output->crc32 = member->crc32;
    output->compression_method = member->compression_method;
    output->mtime = member->mtime;
    ++output->count;
    return 0;
}

static int reject_member(void *userdata, const zget_member_info *member)
{
    (void)userdata;
    (void)member;
    return 1;
}

int main(void)
{
    unsigned char bytes[113];
    struct zget_error_state error = {0};
    struct memory_source memory = {0};
    struct zget_format *format = NULL;
    struct output_buffer output = {0};
    struct member_buffer members = {0};
    int rc;

    memory.data = bytes;
    memory.length = make_stored_zip(bytes);
    zget_source_init(&memory.source, &memory_ops, &error);
    memory.source.size = memory.length;
    memory.source.size_known = true;

    rc = zget_format_open(&memory.source, &error, &format);
    if (rc != ZGET_OK || format == NULL || memory.reads != 1)
        goto fail;
    /* Extraction keeps the ZIP locator entirely inside the format engine. */
    memset(&output, 0, sizeof(output));
    rc = zget_format_extract_member(format, "a.txt", collect, &output);
    if (rc != ZGET_OK || output.length != 5 ||
        memcmp(output.data, "hello", 5) != 0 || memory.reads != 4)
        goto fail;

    /* Listing traverses the same format engine and borrows one record at a time. */
    rc = zget_format_list(format, collect_member, &members);
    if (rc != ZGET_OK || members.count != 1 ||
        members.name_length != 5 || memcmp(members.name, "a.txt", 5) != 0 ||
        members.compressed_size != 5 || members.uncompressed_size != 5 ||
        members.crc32 != 0x3610a686u || members.compression_method != 0 ||
        members.mtime != INT64_C(1775655600) || memory.reads != 5)
        goto fail;
    rc = zget_format_list(format, reject_member, NULL);
    if (rc != ZGET_ECALLBACK || memory.reads != 6)
        goto fail;
    rc = zget_format_extract_member(format, "a.txt", reject_output, NULL);
    if (rc != ZGET_ECALLBACK || memory.reads != 9)
        goto fail;

    zget_format_close(format);
    zget_source_close(&memory.source);
    return memory.closed ? 0 : 1;

fail:
    fprintf(stderr, "format test failed: rc=%d, error=%s\n",
            rc, error.message);
    zget_format_close(format);
    zget_source_close(&memory.source);
    return 1;
}
