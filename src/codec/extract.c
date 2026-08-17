#include "internal.h"

#include <limits.h>
#include <string.h>
#include <zlib.h>

/*
 * One extractor bridges the compressed range callback to caller output. It
 * retains only zlib's window and a fixed output buffer; produced bytes and CRC
 * are updated together so every byte accepted by the caller is validated.
 */
struct extractor {
    struct zget_ctx *ctx;
    const zget_entry *entry;
    zget_write_cb output;
    void *userdata;
    z_stream stream;
    uint64_t produced;
    uLong crc;
    bool inflate_initialized;
    bool stream_end;
};

static int emit(struct extractor *x, const unsigned char *data, size_t length)
{
    /* Enforce the limit before exposing bytes; a failed call produces nothing. */
    if (length > UINT64_MAX - x->produced ||
        (x->ctx->options.max_output_size != 0 &&
         x->produced + length > x->ctx->options.max_output_size)) {
        zget_set_error(x->ctx, ZGET_ELIMIT, "output size limit exceeded");
        return 1;
    }
    if (length != 0 && x->output(x->userdata, data, length) != 0) {
        zget_set_error(x->ctx, ZGET_EIO, "output callback failed");
        return 1;
    }
    {
        const unsigned char *p = data;
        size_t left = length;

        /* zlib's crc32 length is uInt even when the callback uses size_t. */
        while (left != 0) {
            uInt part = left > UINT_MAX ? UINT_MAX : (uInt)left;
            x->crc = crc32(x->crc, p, part);
            p += part;
            left -= part;
        }
    }
    x->produced += length;
    return 0;
}

static int store_write(void *opaque, const void *data, size_t length)
{
    return emit(opaque, data, length);
}

static int deflate_write(void *opaque, const void *data, size_t length)
{
    struct extractor *x = opaque;
    const unsigned char *input = data;
    unsigned char output[65536];

    /*
     * A ZIP member contains exactly one raw DEFLATE stream. Reaching stream end
     * before the advertised compressed range is exhausted is malformed rather
     * than permission to ignore trailing bytes.
     */
    if (x->stream_end) {
        zget_set_error(x->ctx, ZGET_EDEFLATE, "trailing bytes after DEFLATE stream");
        return 1;
    }
    while (length != 0) {
        uInt part = length > UINT_MAX ? UINT_MAX : (uInt)length;
        x->stream.next_in = (Bytef *)(uintptr_t)input;
        x->stream.avail_in = part;
        do {
            int zr;
            size_t have;
            x->stream.next_out = output;
            x->stream.avail_out = sizeof(output);
            zr = inflate(&x->stream, Z_NO_FLUSH);
            if (zr != Z_OK && zr != Z_STREAM_END) {
                zget_set_error(x->ctx, ZGET_EDEFLATE, "invalid DEFLATE stream");
                return 1;
            }
            have = sizeof(output) - x->stream.avail_out;
            if (emit(x, output, have) != 0)
                return 1;
            if (zr == Z_STREAM_END) {
                x->stream_end = true;
                if (x->stream.avail_in != 0 || length != part) {
                    zget_set_error(x->ctx, ZGET_EDEFLATE,
                                   "trailing bytes after DEFLATE stream");
                    return 1;
                }
                break;
            }
        } while (x->stream.avail_in != 0);
        input += part;
        length -= part;
    }
    return 0;
}

/* Stream one exact compressed range through STORE or raw DEFLATE and CRC32. */
int zget_extract_payload(struct zget_ctx *ctx, const zget_entry *entry,
                         uint64_t data_offset, zget_write_cb cb, void *userdata)
{
    struct extractor x;
    int rc;
    memset(&x, 0, sizeof(x));
    x.ctx = ctx;
    x.entry = entry;
    x.output = cb;
    x.userdata = userdata;
    x.crc = crc32(0L, Z_NULL, 0);

    if (ctx->options.max_output_size != 0 &&
        entry->uncompressed_size > ctx->options.max_output_size) {
        zget_set_error(ctx, ZGET_ELIMIT, "member exceeds output size limit");
        return ZGET_ELIMIT;
    }
    if (entry->compression_method == 8) {
        /* ZIP stores raw DEFLATE blocks with no zlib or gzip wrapper/trailer. */
        if (inflateInit2(&x.stream, -MAX_WBITS) != Z_OK) {
            zget_set_error(ctx, ZGET_EDEFLATE, "could not initialize zlib");
            return ZGET_EDEFLATE;
        }
        x.inflate_initialized = true;
    }
    /*
     * Fetch exactly the compressed size recorded in the Central Directory.
     * This intentionally stops before any data descriptor following the member.
     */
    if (entry->compressed_size != 0) {
        rc = ctx->source.read_range(ctx->source.ctx, data_offset,
                                    entry->compressed_size,
                                    entry->compression_method == 0 ?
                                    store_write : deflate_write, &x);
        if (rc != ZGET_OK)
            goto done;
    }
    /* A fully consumed HTTP range is not proof that the DEFLATE stream ended. */
    if (entry->compression_method == 8 && !x.stream_end) {
        zget_set_error(ctx, ZGET_EDEFLATE, "truncated DEFLATE stream");
        rc = ZGET_EDEFLATE;
        goto done;
    }
    if (x.produced != entry->uncompressed_size) {
        zget_set_error(ctx, ZGET_EZIP, "uncompressed size does not match ZIP metadata");
        rc = ZGET_EZIP;
        goto done;
    }
    /* CRC is over uncompressed output, exactly as recorded by the ZIP entry. */
    if ((uint32_t)x.crc != entry->crc32) {
        zget_set_error(ctx, ZGET_ECRC, "CRC32 mismatch");
        rc = ZGET_ECRC;
        goto done;
    }
    rc = ZGET_OK;
done:
    /* inflateEnd is required after every successful inflateInit2, including errors. */
    if (x.inflate_initialized)
        (void)inflateEnd(&x.stream);
    return rc;
}
