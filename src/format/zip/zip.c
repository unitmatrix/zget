#include "format/zip/zip-private.h"
#include "format/zip/zip-parse.h"

#include "util.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define ZIP_TAIL_SIZE 131072u

struct buffer {
    unsigned char *data;
    size_t capacity;
    size_t length;
};

static const uint16_t cp437_high[128] = {
    0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
    0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
    0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
    0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
    0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
    0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
    0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
    0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
    0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
    0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
    0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
    0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
    0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0
};

static enum zget_source_action buffer_write(void *opaque, const void *data,
                                            size_t length)
{
    struct buffer *b = opaque;
    if (length > b->capacity - b->length)
        return ZGET_SOURCE_ERROR;
    memcpy(b->data + b->length, data, length);
    b->length += length;
    return ZGET_SOURCE_CONTINUE;
}

/*
 * Resolve the tail into one trusted Central Directory interval. ZIP64 often
 * fits in the suffix already; otherwise only its 56-byte fixed prefix is
 * fetched, preserving the normal four-request profile where possible.
 */
static int zip_parse_tail(struct zget_zip_format *zip,
                          const unsigned char *tail, size_t length,
                          uint64_t tail_offset)
{
    struct zget_eocd eocd;
    unsigned char record[56];
    struct buffer b = {record, sizeof(record), 0};
    uint64_t archive_size;
    int rc;

    if (!zget_source_get_size(zip->format.source, &archive_size)) {
        zget_error_set(zip->format.error, ZGET_EINVAL,
                       "source size is unavailable");
        return ZGET_EINVAL;
    }
    rc = zget_zip_parse_eocd(tail, length, tail_offset, archive_size, &eocd);
    if (rc != ZGET_OK) {
        zget_error_set(zip->format.error, rc, rc == ZGET_EUNSUPPORTED ?
                       "multi-disk ZIP archives are unsupported" :
                       "malformed ZIP EOCD");
        return rc;
    }
    if (eocd.zip64) {
        if (eocd.zip64_offset >= tail_offset &&
            eocd.zip64_offset - tail_offset <= length - 56) {
            memcpy(record, tail + (size_t)(eocd.zip64_offset - tail_offset), 56);
            b.length = 56;
        } else {
            rc = zget_source_read_range(zip->format.source,
                                        eocd.zip64_offset, 56,
                                        buffer_write, &b);
            if (rc != ZGET_OK)
                return rc;
        }
        rc = zget_zip_parse_zip64(record, b.length, &eocd);
        if (rc != ZGET_OK) {
            zget_error_set(zip->format.error, rc, rc == ZGET_EUNSUPPORTED ?
                           "multi-disk ZIP64 archives are unsupported" :
                           "malformed ZIP64 EOCD");
            return rc;
        }
        {
            uint64_t record_end;
            /* The variable-size ZIP64 record must end exactly at its locator. */
            if (!zget_u64_add(eocd.zip64_offset, 12, &record_end) ||
                !zget_u64_add(record_end, eocd.zip64_record_size, &record_end) ||
                record_end != eocd.eocd_offset - 20) {
                zget_error_set(zip->format.error, ZGET_EZIP,
                               "malformed ZIP64 record length");
                return ZGET_EZIP;
            }
        }
    }
    /*
     * Both addition and containment precede the source request. The directory
     * must also end before EOCD, preventing metadata from overlapping the tail
     * records or wrapping around uint64_t.
     */
    if (!zget_range_valid(eocd.cd_offset, eocd.cd_size, archive_size) ||
        eocd.cd_offset + eocd.cd_size > eocd.eocd_offset) {
        zget_error_set(zip->format.error, ZGET_EZIP,
                       "central directory lies outside the archive");
        return ZGET_EZIP;
    }
    zip->cd_offset = eocd.cd_offset;
    zip->cd_size = eocd.cd_size;
    zip->entry_count = eocd.entries;
    return ZGET_OK;
}

/*
 * Central Directory data may split at any byte boundary in source callbacks:
 *
 *   fixed header -> name -> extra -> comment -> next fixed header
 *
 * Only one entry is live. Name and extra-field buffers are reused across
 * entries, and comments are skipped. Memory is therefore bounded by the
 * largest individual ZIP entry rather than the archive entry count.
 */
enum cd_state { CD_HEADER, CD_NAME, CD_EXTRA, CD_COMMENT, CD_COMPLETE };
struct cd_parser {
    struct zget_zip_format *zip;
    const unsigned char *target;
    size_t target_len;
    struct zget_zip_entry *result;
    zget_list_cb list_cb;
    void *list_userdata;
    enum cd_state state;
    unsigned char header[46];
    size_t have;
    unsigned char *name;
    unsigned char *extra;
    unsigned char *resolved;
    size_t name_capacity, extra_capacity, resolved_capacity;
    size_t resolved_length;
    size_t name_pos, extra_pos, comment_pos;
    uint16_t name_len, extra_len, comment_len, flags, method, disk16;
    uint16_t modified_time, modified_date;
    uint32_t crc, compressed32, size32, offset32;
    uint64_t entries;
    uint64_t archive_size;
    bool matching;
};

static int cd_fail(struct cd_parser *p, int error, const char *message)
{
    zget_error_set(p->zip->format.error, error, "%s", message);
    return 1;
}

static bool extra_fields_valid(const unsigned char *extra, size_t length)
{
    size_t pos = 0;

    while (pos < length) {
        uint16_t field_length;

        if (length - pos < 4)
            return false;
        field_length = zget_le16(extra + pos + 2);
        pos += 4;
        if (field_length > length - pos)
            return false;
        pos += field_length;
    }
    return true;
}

static int resolved_reserve(struct cd_parser *p, size_t capacity)
{
    unsigned char *larger;

    if (capacity <= p->resolved_capacity)
        return 0;
    larger = realloc(p->resolved, capacity);
    if (larger == NULL)
        return cd_fail(p, ZGET_ENOMEM, "could not allocate resolved entry name");
    p->resolved = larger;
    p->resolved_capacity = capacity;
    return 0;
}

static size_t utf8_encode(uint16_t codepoint, unsigned char *output)
{
    if (codepoint < 0x80) {
        output[0] = (unsigned char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        output[0] = (unsigned char)(0xc0u | (codepoint >> 6));
        output[1] = (unsigned char)(0x80u | (codepoint & 0x3fu));
        return 2;
    }
    output[0] = (unsigned char)(0xe0u | (codepoint >> 12));
    output[1] = (unsigned char)(0x80u | ((codepoint >> 6) & 0x3fu));
    output[2] = (unsigned char)(0x80u | (codepoint & 0x3fu));
    return 3;
}

static bool unicode_path_name(struct cd_parser *p,
                              const unsigned char **name, size_t *length)
{
    uLong raw_crc = crc32(0L, Z_NULL, 0);
    size_t pos = 0;

    if (p->name_len != 0)
        raw_crc = crc32(raw_crc, p->name, p->name_len);
    while (pos < p->extra_len) {
        uint16_t id = zget_le16(p->extra + pos);
        uint16_t field_length = zget_le16(p->extra + pos + 2);
        const unsigned char *data = p->extra + pos + 4;

        pos += 4u + field_length;
        if (id != 0x7075u || field_length < 5 || data[0] != 1 ||
            zget_le32(data + 1) != (uint32_t)raw_crc)
            continue;
        *name = data + 5;
        *length = field_length - 5u;
        if (zget_valid_utf8(*name, *length) &&
            (*length == 0 || memchr(*name, '\0', *length) == NULL))
            return true;
    }
    return false;
}

static int resolve_name(struct cd_parser *p)
{
    const unsigned char *source = p->name;
    size_t source_length = p->name_len;
    bool utf8 = (p->flags & (1u << 11)) != 0;
    size_t i, length = 0;

    if (utf8) {
        if (!zget_valid_utf8(source, source_length))
            return cd_fail(p, ZGET_EZIP, "entry name marked UTF-8 is invalid");
    } else if (!unicode_path_name(p, &source, &source_length)) {
        if (resolved_reserve(p, (size_t)p->name_len * 3u + 1u) != 0)
            return 1;
        for (i = 0; i < p->name_len; ++i) {
            uint16_t codepoint = p->name[i] < 0x80 ?
                                 p->name[i] : cp437_high[p->name[i] - 0x80];
            length += utf8_encode(codepoint, p->resolved + length);
        }
        p->resolved[length] = '\0';
        p->resolved_length = length;
        if (memchr(p->resolved, '\0', length) != NULL)
            return cd_fail(p, ZGET_EZIP, "entry name contains an embedded NUL");
        return 0;
    }

    if (source_length != 0 && memchr(source, '\0', source_length) != NULL)
        return cd_fail(p, ZGET_EZIP, "entry name contains an embedded NUL");
    if (resolved_reserve(p, source_length + 1u) != 0)
        return 1;
    if (source_length != 0)
        memcpy(p->resolved, source, source_length);
    p->resolved[source_length] = '\0';
    p->resolved_length = source_length;
    return 0;
}

static bool ntfs_mtime(struct cd_parser *p, int64_t *mtime)
{
    static const uint64_t unix_epoch = UINT64_C(116444736000000000);
    static const uint64_t ticks_per_second = UINT64_C(10000000);
    size_t pos = 0;

    while (pos < p->extra_len) {
        uint16_t id = zget_le16(p->extra + pos);
        uint16_t field_length = zget_le16(p->extra + pos + 2);
        const unsigned char *data = p->extra + pos + 4;
        size_t nested = 4;

        pos += 4u + field_length;
        if (id != 0x000au || field_length < 4)
            continue;
        while (nested < field_length) {
            uint16_t tag, attribute_length;
            uint64_t ticks, delta;

            if (field_length - nested < 4)
                break;
            tag = zget_le16(data + nested);
            attribute_length = zget_le16(data + nested + 2);
            nested += 4;
            if (attribute_length > field_length - nested)
                break;
            if (tag == 0x0001u && attribute_length == 24) {
                ticks = zget_le64(data + nested);
                if (ticks >= unix_epoch)
                    *mtime = (int64_t)((ticks - unix_epoch) / ticks_per_second);
                else {
                    delta = unix_epoch - ticks;
                    *mtime = -(int64_t)(delta / ticks_per_second);
                    if (delta % ticks_per_second != 0)
                        --*mtime;
                }
                return true;
            }
            nested += attribute_length;
        }
    }
    return false;
}

static bool extended_mtime(struct cd_parser *p, int64_t *mtime)
{
    size_t pos = 0;

    while (pos < p->extra_len) {
        uint16_t id = zget_le16(p->extra + pos);
        uint16_t field_length = zget_le16(p->extra + pos + 2);
        const unsigned char *data = p->extra + pos + 4;

        pos += 4u + field_length;
        if (id == 0x5455u && field_length >= 5 && (data[0] & 1u) != 0) {
            uint32_t raw = zget_le32(data + 1);

            /* Decode the signed wire value without implementation-defined casts. */
            *mtime = raw <= INT32_MAX ? (int64_t)raw :
                     -(int64_t)(UINT32_MAX - raw) - 1;
            return true;
        }
    }
    return false;
}

static bool dos_mtime(struct cd_parser *p, int64_t *mtime)
{
    static const uint8_t month_days[] =
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned int year = 1980u + (p->modified_date >> 9);
    unsigned int month = (p->modified_date >> 5) & 0x0fu;
    unsigned int day = p->modified_date & 0x1fu;
    unsigned int hour = p->modified_time >> 11;
    unsigned int minute = (p->modified_time >> 5) & 0x3fu;
    unsigned int second = (p->modified_time & 0x1fu) * 2u;
    unsigned int maximum_day;
    int64_t adjusted_year, era, year_of_era, day_of_year, day_of_era, days;

    if (month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59)
        return false;
    maximum_day = month_days[month - 1];
    if (month == 2 && year % 4u == 0 &&
        (year % 100u != 0 || year % 400u == 0))
        ++maximum_day;
    if (day < 1 || day > maximum_day)
        return false;

    adjusted_year = year - (month <= 2 ? 1 : 0);
    era = adjusted_year / 400;
    year_of_era = adjusted_year - era * 400;
    day_of_year = (153 * ((int64_t)month + (month > 2 ? -3 : 9)) + 2) / 5 +
                  day - 1;
    day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 +
                 day_of_year;
    days = era * 146097 + day_of_era - 719468;
    *mtime = days * 86400 + hour * 3600 + minute * 60 + second;
    return true;
}

static int resolve_mtime(struct cd_parser *p, int64_t *mtime)
{
    if (ntfs_mtime(p, mtime) || extended_mtime(p, mtime) ||
        dos_mtime(p, mtime))
        return 0;
    return cd_fail(p, ZGET_EZIP, "entry has an invalid modification time");
}

static int cd_resolve_entry(struct cd_parser *p, uint64_t *size,
                            uint64_t *compressed, uint64_t *offset)
{
    uint32_t disk;
    int rc;

    /*
     * ZIP64 values appear in the extra field only for saturated fixed fields.
     * Resolve the complete tuple so malformed layouts and split-volume entries
     * are rejected consistently by both lookup and listing.
     */
    rc = zget_zip_parse_zip64_extra(p->extra, p->extra_len,
                                    p->size32, p->compressed32, p->offset32,
                                    p->disk16, size, compressed, offset, &disk);
    if (rc != ZGET_OK)
        return cd_fail(p, ZGET_EZIP, "malformed ZIP64 entry extra field");
    if (disk != 0)
        return cd_fail(p, ZGET_EUNSUPPORTED, "multi-disk ZIP entries are unsupported");
    return 0;
}

static int cd_emit_listing_entry(struct cd_parser *p)
{
    zget_member_info member;
    uint64_t size, compressed, offset;

    if (cd_resolve_entry(p, &size, &compressed, &offset) != 0)
        return 1;
    (void)offset;
    memset(&member, 0, sizeof(member));
    member.name = (const char *)p->resolved;
    member.name_length = p->resolved_length;
    member.compressed_size = compressed;
    member.uncompressed_size = size;
    member.crc32 = p->crc;
    member.compression_method = p->method;
    if (resolve_mtime(p, &member.mtime) != 0)
        return 1;
    /* The borrowed name remains valid until this synchronous callback returns. */
    if (p->list_cb(p->list_userdata, &member) != 0)
        return cd_fail(p, ZGET_ECALLBACK, "listing callback rejected entry");
    return 0;
}

static int cd_finish_match(struct cd_parser *p)
{
    uint64_t size, compressed, offset;

    if ((p->flags & 0x0041u) != 0)
        return cd_fail(p, ZGET_EUNSUPPORTED, "encrypted ZIP entries are unsupported");
    if (p->method != 0 && p->method != 8)
        return cd_fail(p, ZGET_ECOMPRESSION, "unsupported ZIP compression method");
    if (cd_resolve_entry(p, &size, &compressed, &offset) != 0)
        return 1;
    if (!zget_range_valid(offset, 30, p->archive_size))
        return cd_fail(p, ZGET_EZIP, "local header offset is outside the archive");
    p->result->compressed_size = compressed;
    p->result->uncompressed_size = size;
    p->result->local_header_offset = offset;
    p->result->crc32 = p->crc;
    p->result->compression_method = p->method;
    p->result->flags = p->flags;
    p->state = CD_COMPLETE;
    return 0;
}

static int cd_name_done(struct cd_parser *p)
{
    unsigned char *larger;

    if (p->extra_len > p->extra_capacity) {
        /* Reuse the largest buffer instead of allocating once per listed entry. */
        larger = realloc(p->extra, p->extra_len);
        if (larger == NULL)
            return cd_fail(p, ZGET_ENOMEM, "could not allocate extra-field buffer");
        p->extra = larger;
        p->extra_capacity = p->extra_len;
    }
    p->extra_pos = 0;
    p->state = CD_EXTRA;
    return 0;
}

static int cd_extra_done(struct cd_parser *p)
{
    if (!extra_fields_valid(p->extra, p->extra_len))
        return cd_fail(p, ZGET_EZIP, "malformed ZIP entry extra fields");
    if (resolve_name(p) != 0)
        return 1;
    p->matching = p->target != NULL &&
                  p->resolved_length == p->target_len &&
                  memcmp(p->resolved, p->target, p->target_len) == 0;
    if (p->matching && p->result != NULL)
        return cd_finish_match(p);
    if (p->list_cb != NULL) {
        if (cd_emit_listing_entry(p) != 0)
            return 1;
    }
    ++p->entries;
    p->comment_pos = 0;
    if (p->comment_len == 0) {
        p->have = 0;
        p->state = CD_HEADER;
    } else {
        p->state = CD_COMMENT;
    }
    return 0;
}

static int cd_header_done(struct cd_parser *p)
{
    struct zget_cd_fixed h;
    uint64_t record_length;
    unsigned char *larger;
    if (p->entries >= p->zip->entry_count)
        return cd_fail(p, ZGET_EZIP, "central directory has more entries than EOCD");
    if (zget_zip_parse_cd_fixed(p->header, sizeof(p->header), &h) != ZGET_OK)
        return cd_fail(p, ZGET_EZIP, "invalid central directory signature");
    p->flags = h.flags; p->method = h.method; p->crc = h.crc32;
    p->modified_time = h.modified_time; p->modified_date = h.modified_date;
    p->compressed32 = h.compressed_size; p->size32 = h.uncompressed_size;
    p->name_len = h.name_length; p->extra_len = h.extra_length;
    p->comment_len = h.comment_length; p->disk16 = h.disk;
    p->offset32 = h.local_offset;
    /* The three lengths are promoted before addition to avoid narrow overflow. */
    record_length = 46u + (uint64_t)p->name_len + p->extra_len + p->comment_len;
    if (record_length > p->zip->cd_size)
        return cd_fail(p, ZGET_EZIP, "central directory entry exceeds directory bounds");
    if (p->name_len > p->name_capacity) {
        /* Grow monotonically so large directories do not allocate once per entry. */
        larger = realloc(p->name, p->name_len);
        if (larger == NULL)
            return cd_fail(p, ZGET_ENOMEM, "could not allocate entry-name buffer");
        p->name = larger;
        p->name_capacity = p->name_len;
    }
    p->name_pos = 0;
    p->state = CD_NAME;
    if (p->name_len == 0) {
        if (cd_name_done(p) != 0)
            return 1;
        if (p->extra_len == 0 && cd_extra_done(p) != 0)
            return 1;
    }
    return 0;
}

/* Consume arbitrary callback fragments until an entry ends or a match is found. */
static enum zget_source_action cd_write(void *opaque, const void *data,
                                        size_t length)
{
    struct cd_parser *p = opaque;
    const unsigned char *in = data;

    /* No branch assumes a complete header or variable field arrived at once. */
    while (length != 0 && p->state != CD_COMPLETE) {
        size_t need, take;
        if (p->state == CD_HEADER) {
            need = 46 - p->have; take = length < need ? length : need;
            memcpy(p->header + p->have, in, take);
            p->have += take; in += take; length -= take;
            if (p->have == 46 && cd_header_done(p) != 0)
                return ZGET_SOURCE_ERROR;
        } else if (p->state == CD_NAME) {
            need = p->name_len - p->name_pos; take = length < need ? length : need;
            memcpy(p->name + p->name_pos, in, take);
            p->name_pos += take; in += take; length -= take;
            if (p->name_pos == p->name_len) {
                if (cd_name_done(p) != 0)
                    return ZGET_SOURCE_ERROR;
                if (p->extra_len == 0 && cd_extra_done(p) != 0)
                    return ZGET_SOURCE_ERROR;
            }
        } else if (p->state == CD_EXTRA) {
            need = p->extra_len - p->extra_pos; take = length < need ? length : need;
            memcpy(p->extra + p->extra_pos, in, take);
            p->extra_pos += take; in += take; length -= take;
            if (p->extra_pos == p->extra_len) {
                if (cd_extra_done(p) != 0)
                    return ZGET_SOURCE_ERROR;
            }
        } else if (p->state == CD_COMMENT) {
            need = p->comment_len - p->comment_pos; take = length < need ? length : need;
            p->comment_pos += take; in += take; length -= take;
            if (p->comment_pos == p->comment_len) {
                p->have = 0; p->state = CD_HEADER;
            }
        }
    }
    /* The source cancels cleanly without learning why the format is finished. */
    return p->state == CD_COMPLETE ? ZGET_SOURCE_STOP : ZGET_SOURCE_CONTINUE;
}

int zget_zip_fuzz_cd_stream(const unsigned char *data, size_t length,
                            size_t chunk_size)
{
    struct zget_error_state error = {0};
    struct zget_zip_format zip = {0};
    struct cd_parser p = {0};
    struct zget_zip_entry entry;
    size_t offset = 0;
    enum zget_source_action action = ZGET_SOURCE_CONTINUE;
    zip.format.error = &error;
    zip.cd_size = length;
    zip.entry_count = UINT64_MAX;
    p.zip = &zip;
    p.target = (const unsigned char *)"target";
    p.target_len = 6;
    p.result = &entry;
    p.state = CD_HEADER;
    p.archive_size = UINT64_MAX;
    if (chunk_size == 0)
        chunk_size = 1;
    while (offset < length) {
        size_t part = length - offset < chunk_size ? length - offset : chunk_size;
        action = cd_write(&p, data + offset, part);
        if (action != ZGET_SOURCE_CONTINUE)
            break;
        offset += part;
    }
    free(p.name);
    free(p.extra);
    free(p.resolved);
    return (int)action;
}

static int zip_walk_cd(struct zget_zip_format *zip, struct cd_parser *p)
{
    int rc;

    p->zip = zip;
    p->state = CD_HEADER;
    if (!zget_source_get_size(zip->format.source, &p->archive_size)) {
        zget_error_set(zip->format.error, ZGET_EINVAL,
                       "source size is unavailable");
        return ZGET_EINVAL;
    }
    if (zip->entry_count == 0) {
        if (zip->cd_size == 0)
            return ZGET_OK;
        zget_error_set(zip->format.error, ZGET_EZIP,
                       "empty central directory has unexpected data");
        return ZGET_EZIP;
    }
    if (zip->cd_size == 0) {
        zget_error_set(zip->format.error, ZGET_EZIP,
                       "central directory is missing");
        return ZGET_EZIP;
    }
    rc = zget_source_read_range(zip->format.source,
                                zip->cd_offset, zip->cd_size,
                                cd_write, p);
    /* Parser scratch storage is owned here on success, failure, and early stop. */
    free(p->name);
    free(p->extra);
    free(p->resolved);
    p->name = NULL;
    p->extra = NULL;
    p->resolved = NULL;
    if (rc != ZGET_OK || p->state == CD_COMPLETE)
        return rc;
    if (p->state != CD_HEADER || p->have != 0 ||
        p->entries != zip->entry_count) {
        zget_error_set(zip->format.error, ZGET_EZIP,
                       "central directory is truncated or inconsistent");
        return ZGET_EZIP;
    }
    return ZGET_OK;
}

static int zip_find_in_cd(struct zget_zip_format *zip, const char *member,
                          struct zget_zip_entry *entry)
{
    struct cd_parser p = {0};
    int rc;

    p.target = (const unsigned char *)member;
    p.target_len = strlen(member);
    p.result = entry;
    /*
     * The first exact match wins by policy. The callback stops the source after
     * retaining its metadata, avoiding later entries without coupling ZIP to a
     * particular transport or exposing duplicate detection state.
     */
    rc = zip_walk_cd(zip, &p);
    if (p.state == CD_COMPLETE)
        return ZGET_OK;
    if (rc != ZGET_OK)
        return rc;
    zget_error_set(zip->format.error, ZGET_ENOTFOUND, "member not found");
    return ZGET_ENOTFOUND;
}

static int zip_validate_member(struct zget_format *format, const char *member)
{
    size_t length = strlen(member);

    /* Resolved ZIP names are exact UTF-8 identifiers, never normalized paths. */
    if (length == 0 || !zget_valid_utf8((const unsigned char *)member, length)) {
        zget_error_set(format->error, ZGET_EINVAL,
                       "member name must be non-empty valid UTF-8");
        return ZGET_EINVAL;
    }
    return ZGET_OK;
}

int zget_zip_format_list(struct zget_format *format, zget_list_cb list_cb,
                         void *userdata)
{
    struct zget_zip_format *zip = (struct zget_zip_format *)format;
    struct cd_parser p = {0};

    p.list_cb = list_cb;
    p.list_userdata = userdata;
    return zip_walk_cd(zip, &p);
}

static int zip_read_local_header(struct zget_zip_format *zip,
                                 const struct zget_zip_entry *entry,
                                 uint64_t *data_offset)
{
    unsigned char header[30];
    struct buffer b = {header, sizeof(header), 0};
    struct zget_local_fixed h;
    uint64_t archive_size, offset;
    int rc;

    if (!zget_source_get_size(zip->format.source, &archive_size)) {
        zget_error_set(zip->format.error, ZGET_EINVAL,
                       "source size is unavailable");
        return ZGET_EINVAL;
    }
    if (!zget_range_valid(entry->local_header_offset, sizeof(header),
                          archive_size)) {
        zget_error_set(zip->format.error, ZGET_EZIP,
                       "local header offset is outside the archive");
        return ZGET_EZIP;
    }
    rc = zget_source_read_range(zip->format.source,
                                entry->local_header_offset,
                                sizeof(header), buffer_write, &b);
    if (rc != ZGET_OK)
        return rc;
    if (zget_zip_parse_local_fixed(header, b.length, &h) != ZGET_OK) {
        zget_error_set(zip->format.error, ZGET_EZIP,
                       "malformed local file header");
        return ZGET_EZIP;
    }
    if ((h.flags & 0x0041u) != 0 || h.method != entry->compression_method) {
        zget_error_set(zip->format.error,
                       (h.flags & 0x0041u) ? ZGET_EUNSUPPORTED : ZGET_EZIP,
                       (h.flags & 0x0041u) ?
                       "encrypted ZIP entries are unsupported" :
                       "local and central compression methods differ");
        return zip->format.error->code;
    }
    /*
     * The Local Header contributes only its fixed size and two variable lengths:
     *
     *   payload = local_offset + 30 + name_length + extra_length
     *
     * Perform every addition with overflow checks, then validate the complete
     * compressed payload interval before issuing its range request. Sizes and CRC
     * remain those from the Central Directory, so data descriptors need no scan.
     */
    if (!zget_u64_add(entry->local_header_offset, 30u, &offset) ||
        !zget_u64_add(offset, h.name_length, &offset) ||
        !zget_u64_add(offset, h.extra_length, &offset) ||
        !zget_range_valid(offset, entry->compressed_size, archive_size)) {
        zget_error_set(zip->format.error, ZGET_EZIP,
                       "member payload lies outside the archive");
        return ZGET_EZIP;
    }
    *data_offset = offset;
    return ZGET_OK;
}

static int zip_find(struct zget_format *format, const char *member,
                    struct zget_zip_entry *entry)
{
    struct zget_zip_format *zip = (struct zget_zip_format *)format;
    int rc;

    /*
     * ZIP names are byte identifiers, not filesystem paths. Keep matching
     * exact and case-sensitive; normalization here could select a different
     * entry from the one named by the caller.
     */
    rc = zip_validate_member(format, member);
    if (rc != ZGET_OK)
        return rc;
    return zip_find_in_cd(zip, member, entry);
}

static int zip_extract(struct zget_format *format,
                       const struct zget_zip_entry *entry,
                       zget_write_cb write_cb, void *userdata)
{
    struct zget_zip_format *zip = (struct zget_zip_format *)format;
    uint64_t data_offset;
    int rc;

    /* Keep the feature whitelist next to decoder selection for auditability. */
    if ((entry->flags & 0x0041u) != 0) {
        zget_error_set(format->error, ZGET_EUNSUPPORTED,
                       "encrypted ZIP entries are unsupported");
        return ZGET_EUNSUPPORTED;
    }
    if (entry->compression_method != 0 && entry->compression_method != 8) {
        zget_error_set(format->error, ZGET_ECOMPRESSION,
                       "unsupported ZIP compression method");
        return ZGET_ECOMPRESSION;
    }
    rc = zip_read_local_header(zip, entry, &data_offset);
    if (rc != ZGET_OK)
        return rc;
    return zget_zip_extract_payload(zip, entry, data_offset,
                                    write_cb, userdata);
}

int zget_zip_format_extract_member(struct zget_format *format,
                                   const char *member,
                                   zget_write_cb write_cb, void *userdata)
{
    struct zget_zip_entry entry;
    int rc;

    /*
     * Keep the ZIP locator on the stack: the format-neutral public operation
     * needs no caller-visible allocation or lifetime beyond this extraction.
     */
    memset(&entry, 0, sizeof(entry));
    rc = zip_find(format, member, &entry);
    if (rc != ZGET_OK)
        return rc;
    return zip_extract(format, &entry, write_cb, userdata);
}

void zget_zip_format_close(struct zget_format *format)
{
    free((struct zget_zip_format *)format);
}

int zget_zip_format_open(struct zget_source *source,
                         struct zget_error_state *error,
                         struct zget_format **out_format)
{
    struct zget_zip_format *zip;
    struct buffer tail = {0};
    uint64_t request_size = ZIP_TAIL_SIZE;
    uint64_t source_size, tail_offset;
    int rc;

    if (out_format == NULL)
        return ZGET_EINVAL;
    *out_format = NULL;
    if (source == NULL || error == NULL)
        return ZGET_EINVAL;

    zip = calloc(1, sizeof(*zip));
    if (zip == NULL) {
        zget_error_set(error, ZGET_ENOMEM, "could not allocate ZIP format state");
        return ZGET_ENOMEM;
    }
    zip->format.source = source;
    zip->format.error = error;

    /*
     * EOCD is at least 22 bytes and can follow a 65535-byte archive comment.
     * A 128 KiB suffix also normally contains the ZIP64 locator and fixed EOCD,
     * keeping the common open path to one bounded metadata request.
     */
    tail.capacity = (size_t)request_size;
    tail.data = malloc(tail.capacity);
    if (tail.data == NULL) {
        zget_error_set(error, ZGET_ENOMEM,
                       "could not allocate ZIP tail buffer");
        rc = ZGET_ENOMEM;
        goto fail;
    }
    rc = zget_source_read_suffix(source, request_size, buffer_write, &tail);
    if (rc != ZGET_OK)
        goto fail;
    if (!zget_source_get_size(source, &source_size) ||
        tail.length > source_size) {
        zget_error_set(error, ZGET_EINVAL,
                       "source size unavailable after suffix request");
        rc = ZGET_EINVAL;
        goto fail;
    }
    tail_offset = source_size - tail.length;
    rc = zip_parse_tail(zip, tail.data, tail.length, tail_offset);
    if (rc != ZGET_OK)
        goto fail;

    free(tail.data);
    *out_format = &zip->format;
    return ZGET_OK;

fail:
    free(tail.data);
    free(zip);
    return rc;
}
