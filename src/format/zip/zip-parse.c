#include "format/zip/zip-parse.h"

#include "util.h"
#include "zget.h"

#include <stdbool.h>
#include <string.h>

#define EOCD_SIG 0x06054b50u
#define ZIP64_LOCATOR_SIG 0x07064b50u
#define ZIP64_EOCD_SIG 0x06064b50u
#define CD_SIG 0x02014b50u
#define LOCAL_SIG 0x04034b50u
#define ZIP64_EXTRA 0x0001u

/*
 * Fixed-record parsers are deliberately pure: callers provide contiguous
 * fixed-size bytes, and no parser knows whether they came from a source or a
 * fuzz buffer. Variable fields are handled by the streaming layer below.
 */
int zget_zip_parse_cd_fixed(const unsigned char *p, size_t length,
                            struct zget_cd_fixed *out)
{
    if (p == NULL || out == NULL || length < 46 || zget_le32(p) != CD_SIG)
        return ZGET_EZIP;
    out->version_needed = zget_le16(p + 6);
    out->flags = zget_le16(p + 8);
    out->method = zget_le16(p + 10);
    out->modified_time = zget_le16(p + 12);
    out->modified_date = zget_le16(p + 14);
    out->crc32 = zget_le32(p + 16);
    out->compressed_size = zget_le32(p + 20);
    out->uncompressed_size = zget_le32(p + 24);
    out->name_length = zget_le16(p + 28);
    out->extra_length = zget_le16(p + 30);
    out->comment_length = zget_le16(p + 32);
    out->disk = zget_le16(p + 34);
    out->local_offset = zget_le32(p + 42);
    return ZGET_OK;
}

int zget_zip_parse_local_fixed(const unsigned char *p, size_t length,
                               struct zget_local_fixed *out)
{
    if (p == NULL || out == NULL || length < 30 || zget_le32(p) != LOCAL_SIG)
        return ZGET_EZIP;
    out->flags = zget_le16(p + 6);
    out->method = zget_le16(p + 8);
    out->name_length = zget_le16(p + 26);
    out->extra_length = zget_le16(p + 28);
    return ZGET_OK;
}

int zget_zip_parse_eocd(const unsigned char *tail, size_t length,
                        uint64_t tail_offset, uint64_t archive_size,
                        struct zget_eocd *out)
{
    size_t pos;
    uint16_t disk, cd_disk, entries_disk, entries, comment;
    uint32_t cd_size, cd_offset;
    if (tail == NULL || out == NULL || length < 22)
        return ZGET_EZIP;

    /*
     * EOCD has no forward pointer, so search backward from the object end. The
     * comment-length equality is essential: signature bytes may legally occur
     * inside the archive comment and must not be accepted as an EOCD candidate.
     */
    pos = length - 22;
    for (;;) {
        if (zget_le32(tail + pos) == EOCD_SIG &&
            (size_t)zget_le16(tail + pos + 20) == length - pos - 22)
            break;
        if (pos == 0)
            return ZGET_EZIP;
        --pos;
    }
    disk = zget_le16(tail + pos + 4);
    cd_disk = zget_le16(tail + pos + 6);
    entries_disk = zget_le16(tail + pos + 8);
    entries = zget_le16(tail + pos + 10);
    cd_size = zget_le32(tail + pos + 12);
    cd_offset = zget_le32(tail + pos + 16);
    comment = zget_le16(tail + pos + 20);
    (void)comment;
    if (disk != 0 || cd_disk != 0 || entries_disk != entries)
        return ZGET_EUNSUPPORTED;

    memset(out, 0, sizeof(*out));
    out->eocd_offset = tail_offset + pos;
    out->cd_offset = cd_offset;
    out->cd_size = cd_size;
    out->entries = entries;
    out->zip64 = entries == UINT16_MAX || cd_size == UINT32_MAX ||
                 cd_offset == UINT32_MAX;
    if (out->zip64) {
        const unsigned char *loc;

        /*
         * Sentinel fields make the adjacent ZIP64 locator authoritative. zget
         * accepts only disk zero and one total disk; following a locator into a
         * different object or volume would violate the range-source model.
         */
        if (pos < 20)
            return ZGET_EZIP;
        loc = tail + pos - 20;
        if (zget_le32(loc) != ZIP64_LOCATOR_SIG || zget_le32(loc + 4) != 0 ||
            zget_le32(loc + 16) != 1)
            return ZGET_EUNSUPPORTED;
        out->zip64_offset = zget_le64(loc + 8);
        if (!zget_range_valid(out->zip64_offset, 56, archive_size) ||
            out->zip64_offset >= out->eocd_offset)
            return ZGET_EZIP;
    }
    return ZGET_OK;
}

int zget_zip_parse_zip64(const unsigned char *p, size_t length,
                         struct zget_eocd *out)
{
    uint64_t record_size, total_length;
    uint32_t disk, cd_disk;
    uint64_t entries_disk;
    if (p == NULL || out == NULL || length < 56 || zget_le32(p) != ZIP64_EOCD_SIG)
        return ZGET_EZIP;
    /* Only the 56-byte fixed prefix is needed; an extensible sector may follow. */
    record_size = zget_le64(p + 4);
    if (record_size < 44 || !zget_u64_add(record_size, 12, &total_length))
        return ZGET_EZIP;
    out->zip64_record_size = record_size;
    disk = zget_le32(p + 16);
    cd_disk = zget_le32(p + 20);
    entries_disk = zget_le64(p + 24);
    out->entries = zget_le64(p + 32);
    out->cd_size = zget_le64(p + 40);
    out->cd_offset = zget_le64(p + 48);
    if (disk != 0 || cd_disk != 0 || entries_disk != out->entries)
        return ZGET_EUNSUPPORTED;
    return ZGET_OK;
}

static int take64(const unsigned char **p, size_t *left, uint64_t *out)
{
    if (*left < 8)
        return ZGET_EZIP;
    *out = zget_le64(*p);
    *p += 8;
    *left -= 8;
    return ZGET_OK;
}

int zget_zip_parse_zip64_extra(const unsigned char *extra, size_t length,
                               uint32_t size32, uint32_t compressed32,
                               uint32_t offset32, uint16_t disk16,
                               uint64_t *size, uint64_t *compressed,
                               uint64_t *offset, uint32_t *disk)
{
    size_t pos = 0;
    bool needed = size32 == UINT32_MAX || compressed32 == UINT32_MAX ||
                  offset32 == UINT32_MAX || disk16 == UINT16_MAX;
    *size = size32; *compressed = compressed32; *offset = offset32; *disk = disk16;
    /*
     * ZIP64 values are present only for corresponding saturated fixed fields
     * and appear in that fixed order. Consuming an unconditional layout would
     * shift later values and could turn a size into an attacker-chosen offset.
     * Unknown extra fields are length-checked and skipped without interpretation.
     */
    while (pos < length) {
        uint16_t id, n;
        const unsigned char *p;
        size_t left;
        if (length - pos < 4)
            return ZGET_EZIP;
        id = zget_le16(extra + pos);
        n = zget_le16(extra + pos + 2);
        pos += 4;
        if (n > length - pos)
            return ZGET_EZIP;
        if (id != ZIP64_EXTRA) {
            pos += n;
            continue;
        }
        p = extra + pos;
        left = n;
        if (size32 == UINT32_MAX && take64(&p, &left, size) != ZGET_OK)
            return ZGET_EZIP;
        if (compressed32 == UINT32_MAX && take64(&p, &left, compressed) != ZGET_OK)
            return ZGET_EZIP;
        if (offset32 == UINT32_MAX && take64(&p, &left, offset) != ZGET_OK)
            return ZGET_EZIP;
        if (disk16 == UINT16_MAX) {
            if (left < 4)
                return ZGET_EZIP;
            *disk = zget_le32(p);
        }
        return ZGET_OK;
    }
    return needed ? ZGET_EZIP : ZGET_OK;
}
