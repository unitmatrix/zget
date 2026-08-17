#include "internal.h"
#include "zip/zip_parse.h"

#include <stdlib.h>
#include <string.h>

#define EOCD_SIG 0x06054b50u
#define ZIP64_LOCATOR_SIG 0x07064b50u
#define ZIP64_EOCD_SIG 0x06064b50u
#define CD_SIG 0x02014b50u
#define LOCAL_SIG 0x04034b50u
#define ZIP64_EXTRA 0x0001u

/*
 * Fixed-record parsers are deliberately pure: callers provide contiguous
 * fixed-size bytes, and no parser knows whether they came from HTTP or a fuzz
 * buffer. Variable fields are handled by the streaming layer below.
 */
int zget_zip_parse_cd_fixed(const unsigned char *p, size_t length,
                            struct zget_cd_fixed *out)
{
    if (p == NULL || out == NULL || length < 46 || zget_le32(p) != CD_SIG)
        return ZGET_EZIP;
    out->version_needed = zget_le16(p + 6);
    out->flags = zget_le16(p + 8);
    out->method = zget_le16(p + 10);
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

struct buffer {
    unsigned char *data;
    size_t capacity;
    size_t length;
};

static int buffer_write(void *opaque, const void *data, size_t length)
{
    struct buffer *b = opaque;
    if (length > b->capacity - b->length)
        return 1;
    memcpy(b->data + b->length, data, length);
    b->length += length;
    return 0;
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
         * Sentinel fields make the adjacent ZIP64 locator authoritative. v0.1
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

/*
 * Resolve the tail into one trusted Central Directory interval. ZIP64 often
 * fits in the suffix already; otherwise only its 56-byte fixed prefix is
 * fetched, preserving the normal four-request profile where possible.
 */
int zget_parse_tail(struct zget_ctx *ctx, const unsigned char *tail,
                    size_t length, uint64_t tail_offset)
{
    struct zget_eocd eocd;
    unsigned char record[56];
    struct buffer b = {record, sizeof(record), 0};
    int rc = zget_zip_parse_eocd(tail, length, tail_offset, ctx->archive_size, &eocd);
    if (rc != ZGET_OK) {
        zget_set_error(ctx, rc, rc == ZGET_EUNSUPPORTED ?
                       "multi-disk ZIP archives are unsupported" : "malformed ZIP EOCD");
        return rc;
    }
    if (eocd.zip64) {
        if (eocd.zip64_offset >= tail_offset &&
            eocd.zip64_offset - tail_offset <= length - 56) {
            memcpy(record, tail + (size_t)(eocd.zip64_offset - tail_offset), 56);
            b.length = 56;
        } else {
            rc = ctx->source.read_range(ctx->source.ctx, eocd.zip64_offset, 56,
                                        buffer_write, &b);
            if (rc != ZGET_OK)
                return rc;
        }
        rc = zget_zip_parse_zip64(record, b.length, &eocd);
        if (rc != ZGET_OK) {
            zget_set_error(ctx, rc, rc == ZGET_EUNSUPPORTED ?
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
                zget_set_error(ctx, ZGET_EZIP, "malformed ZIP64 record length");
                return ZGET_EZIP;
            }
        }
    }
    /*
     * Both addition and containment precede the HTTP request. The directory
     * must also end before EOCD, preventing metadata from overlapping the tail
     * records or wrapping around uint64_t.
     */
    if (!zget_range_valid(eocd.cd_offset, eocd.cd_size, ctx->archive_size) ||
        eocd.cd_offset + eocd.cd_size > eocd.eocd_offset) {
        zget_set_error(ctx, ZGET_EZIP, "central directory lies outside the archive");
        return ZGET_EZIP;
    }
    if ((ctx->options.max_metadata_bytes != 0 &&
         eocd.cd_size > ctx->options.max_metadata_bytes) || eocd.cd_size > SIZE_MAX) {
        zget_set_error(ctx, ZGET_ELIMIT, "central directory exceeds metadata limit");
        return ZGET_ELIMIT;
    }
    ctx->cd_offset = eocd.cd_offset;
    ctx->cd_size = eocd.cd_size;
    ctx->entry_count = eocd.entries;
    return ZGET_OK;
}

/*
 * Central Directory data may split at any byte boundary in libcurl callbacks:
 *
 *   fixed header -> name -> extra -> comment -> next fixed header
 *
 * Only one entry is live. The filename buffer is reused across entries, extras
 * are retained only for an exact match, and comments are skipped. Memory is
 * therefore bounded by one ZIP entry rather than the archive entry count.
 */
enum cd_state { CD_HEADER, CD_NAME, CD_EXTRA, CD_COMMENT, CD_COMPLETE };
struct cd_parser {
    struct zget_ctx *ctx;
    const unsigned char *target;
    size_t target_len;
    zget_entry *result;
    enum cd_state state;
    unsigned char header[46];
    size_t have;
    unsigned char *name;
    unsigned char *extra;
    size_t name_capacity;
    size_t name_pos, extra_pos, comment_pos;
    uint16_t name_len, extra_len, comment_len, flags, method, disk16;
    uint32_t crc, compressed32, size32, offset32;
    uint64_t entries;
    bool matching;
};

static int cd_fail(struct cd_parser *p, int error, const char *message)
{
    zget_set_error(p->ctx, error, "%s", message);
    return 1;
}

static int cd_finish_match(struct cd_parser *p)
{
    uint64_t size, compressed, offset;
    uint32_t disk;
    int rc;
    if ((p->flags & 0x0041u) != 0)
        return cd_fail(p, ZGET_EUNSUPPORTED, "encrypted ZIP entries are unsupported");
    if (p->method != 0 && p->method != 8)
        return cd_fail(p, ZGET_ECOMPRESSION, "unsupported ZIP compression method");
    /*
     * Central Directory sizes and CRC are the source of truth, including when
     * the Local Header uses zeros because a data descriptor follows payload.
     */
    rc = zget_zip_parse_zip64_extra(p->extra, p->extra_len,
                                    p->size32, p->compressed32, p->offset32,
                                    p->disk16, &size, &compressed, &offset, &disk);
    if (rc != ZGET_OK)
        return cd_fail(p, ZGET_EZIP, "malformed ZIP64 entry extra field");
    if (disk != 0)
        return cd_fail(p, ZGET_EUNSUPPORTED, "multi-disk ZIP entries are unsupported");
    if (!zget_range_valid(offset, 30, p->ctx->archive_size))
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
    bool utf8 = (p->flags & (1u << 11)) != 0;
    bool eligible = true;
    size_t i;
    /*
     * UTF-8-flagged names must be well-formed. Legacy non-ASCII bytes are not
     * converted in v0.1, so they are deliberately ineligible for a match rather
     * than compared under a locale or silently mistaken for UTF-8.
     */
    if (utf8) {
        if (!zget_valid_utf8(p->name, p->name_len))
            return cd_fail(p, ZGET_EZIP, "entry name marked UTF-8 is invalid");
    } else {
        for (i = 0; i < p->name_len; ++i)
            if (p->name[i] >= 0x80) { eligible = false; break; }
    }
    p->matching = eligible && p->name_len == p->target_len &&
                  memcmp(p->name, p->target, p->target_len) == 0;
    if (p->matching) {
        /* ZIP64 extras matter only for the retained entry; all others are skipped. */
        p->extra = malloc(p->extra_len == 0 ? 1 : p->extra_len);
        if (p->extra == NULL)
            return cd_fail(p, ZGET_ENOMEM, "could not allocate extra-field buffer");
    } else {
        ++p->entries;
    }
    p->extra_pos = 0;
    p->state = CD_EXTRA;
    return 0;
}

static int cd_extra_done(struct cd_parser *p)
{
    if (p->matching)
        return cd_finish_match(p);
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
    if (p->entries >= p->ctx->entry_count)
        return cd_fail(p, ZGET_EZIP, "central directory has more entries than EOCD");
    if (zget_zip_parse_cd_fixed(p->header, sizeof(p->header), &h) != ZGET_OK)
        return cd_fail(p, ZGET_EZIP, "invalid central directory signature");
    p->flags = h.flags; p->method = h.method; p->crc = h.crc32;
    p->compressed32 = h.compressed_size; p->size32 = h.uncompressed_size;
    p->name_len = h.name_length; p->extra_len = h.extra_length;
    p->comment_len = h.comment_length; p->disk16 = h.disk;
    p->offset32 = h.local_offset;
    /* The three lengths are promoted before addition to avoid narrow overflow. */
    record_length = 46u + (uint64_t)p->name_len + p->extra_len + p->comment_len;
    if (record_length > p->ctx->cd_size)
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
static int cd_write(void *opaque, const void *data, size_t length)
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
                return 1;
        } else if (p->state == CD_NAME) {
            need = p->name_len - p->name_pos; take = length < need ? length : need;
            memcpy(p->name + p->name_pos, in, take);
            p->name_pos += take; in += take; length -= take;
            if (p->name_pos == p->name_len) {
                if (cd_name_done(p) != 0)
                    return 1;
                if (p->extra_len == 0 && cd_extra_done(p) != 0)
                    return 1;
            }
        } else if (p->state == CD_EXTRA) {
            need = p->extra_len - p->extra_pos; take = length < need ? length : need;
            if (p->matching)
                memcpy(p->extra + p->extra_pos, in, take);
            p->extra_pos += take; in += take; length -= take;
            if (p->extra_pos == p->extra_len) {
                if (cd_extra_done(p) != 0)
                    return 1;
            }
        } else if (p->state == CD_COMMENT) {
            need = p->comment_len - p->comment_pos; take = length < need ? length : need;
            p->comment_pos += take; in += take; length -= take;
            if (p->comment_pos == p->comment_len) {
                p->have = 0; p->state = CD_HEADER;
            }
        }
    }
    /* -1 asks the HTTP layer to cancel the remaining CD bytes successfully. */
    return p->state == CD_COMPLETE ? -1 : 0;
}

int zget_zip_fuzz_cd_stream(const unsigned char *data, size_t length,
                            size_t chunk_size)
{
    struct zget_ctx ctx = {0};
    struct cd_parser p = {0};
    zget_entry entry;
    size_t offset = 0;
    int rc = 0;
    ctx.archive_size = UINT64_MAX;
    ctx.cd_size = length;
    ctx.entry_count = UINT64_MAX;
    p.ctx = &ctx;
    p.target = (const unsigned char *)"target";
    p.target_len = 6;
    p.result = &entry;
    p.state = CD_HEADER;
    if (chunk_size == 0)
        chunk_size = 1;
    while (offset < length) {
        size_t part = length - offset < chunk_size ? length - offset : chunk_size;
        rc = cd_write(&p, data + offset, part);
        if (rc != 0)
            break;
        offset += part;
    }
    free(p.name);
    free(p.extra);
    return rc;
}

int zget_find_in_cd(struct zget_ctx *ctx, const char *member, zget_entry *entry)
{
    struct cd_parser p = {0};
    int rc;
    p.ctx = ctx;
    p.target = (const unsigned char *)member;
    p.target_len = strlen(member);
    p.result = entry;
    p.state = CD_HEADER;
    if (ctx->entry_count == 0) {
        zget_set_error(ctx, ZGET_ENOTFOUND, "member not found");
        return ZGET_ENOTFOUND;
    }
    /*
     * The first exact match wins by policy. cd_write returns the internal stop
     * sentinel after retaining its metadata, allowing libcurl to terminate the
     * directory transfer without scanning later entries or detecting duplicates.
     */
    rc = ctx->source.read_range(ctx->source.ctx, ctx->cd_offset, ctx->cd_size,
                                cd_write, &p);
    /* Parser scratch storage is owned here on success, failure, and early stop. */
    free(p.name);
    free(p.extra);
    if (p.state == CD_COMPLETE)
        return ZGET_OK;
    if (rc != ZGET_OK)
        return rc;
    if (p.state != CD_HEADER || p.have != 0 || p.entries != ctx->entry_count) {
        zget_set_error(ctx, ZGET_EZIP, "central directory is truncated or inconsistent");
        return ZGET_EZIP;
    }
    zget_set_error(ctx, ZGET_ENOTFOUND, "member not found");
    return ZGET_ENOTFOUND;
}

int zget_read_local_header(struct zget_ctx *ctx, const zget_entry *entry,
                           uint64_t *data_offset)
{
    unsigned char header[30];
    struct buffer b = {header, sizeof(header), 0};
    struct zget_local_fixed h;
    uint64_t offset;
    int rc = ctx->source.read_range(ctx->source.ctx, entry->local_header_offset,
                                    sizeof(header), buffer_write, &b);
    if (rc != ZGET_OK)
        return rc;
    if (zget_zip_parse_local_fixed(header, b.length, &h) != ZGET_OK) {
        zget_set_error(ctx, ZGET_EZIP, "malformed local file header");
        return ZGET_EZIP;
    }
    if ((h.flags & 0x0041u) != 0 || h.method != entry->compression_method) {
        zget_set_error(ctx, (h.flags & 0x0041u) ? ZGET_EUNSUPPORTED : ZGET_EZIP,
                       (h.flags & 0x0041u) ? "encrypted ZIP entries are unsupported" :
                       "local and central compression methods differ");
        return ctx->error;
    }
    /*
     * The Local Header contributes only its fixed size and two variable lengths:
     *
     *   payload = local_offset + 30 + name_length + extra_length
     *
     * Perform every addition with overflow checks, then validate the complete
     * compressed payload interval before issuing its HTTP request. Sizes and CRC
     * remain those from the Central Directory, so data descriptors need no scan.
     */
    if (!zget_u64_add(entry->local_header_offset, 30u, &offset) ||
        !zget_u64_add(offset, h.name_length, &offset) ||
        !zget_u64_add(offset, h.extra_length, &offset) ||
        !zget_range_valid(offset, entry->compressed_size, ctx->archive_size)) {
        zget_set_error(ctx, ZGET_EZIP, "member payload lies outside the archive");
        return ZGET_EZIP;
    }
    *data_offset = offset;
    return ZGET_OK;
}
