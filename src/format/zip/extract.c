#include "format/zip/zip-private.h"

#include <limits.h>
#include <string.h>
#include <zlib.h>

/*
 * One extractor bridges the compressed range callback to caller output. It
 * retains only zlib's window and a fixed output buffer; produced bytes and CRC
 * are updated together so every byte accepted by the caller is validated.
 */
struct extractor {
    struct zget_zip_format *zip;
    const struct zget_zip_entry *entry;
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
    if (length > UINT64_MAX - x->produced) {
        zget_error_set(x->zip->format.error, ZGET_EZIP,
                       "uncompressed size overflows uint64_t");
        return 1;
    }
    if (length != 0 && x->output(x->userdata, data, length) != 0) {
        zget_error_set(x->zip->format.error, ZGET_ECALLBACK,
                       "output callback failed");
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

static enum zget_source_action store_write(void *opaque, const void *data,
                                           size_t length)
{
    return emit(opaque, data, length) == 0 ?
           ZGET_SOURCE_CONTINUE : ZGET_SOURCE_ERROR;
}

static enum zget_source_action deflate_write(void *opaque, const void *data,
                                             size_t length)
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
        zget_error_set(x->zip->format.error, ZGET_ECOMPRESSION,
                       "trailing bytes after DEFLATE stream");
        return ZGET_SOURCE_ERROR;
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
                zget_error_set(x->zip->format.error, ZGET_ECOMPRESSION,
                               "invalid DEFLATE stream");
                return ZGET_SOURCE_ERROR;
            }
            have = sizeof(output) - x->stream.avail_out;
            if (emit(x, output, have) != 0)
                return ZGET_SOURCE_ERROR;
            if (zr == Z_STREAM_END) {
                x->stream_end = true;
                if (x->stream.avail_in != 0 || length != part) {
                    zget_error_set(x->zip->format.error, ZGET_ECOMPRESSION,
                                   "trailing bytes after DEFLATE stream");
                    return ZGET_SOURCE_ERROR;
                }
                break;
            }
        } while (x->stream.avail_in != 0);
        input += part;
        length -= part;
    }
    return ZGET_SOURCE_CONTINUE;
}

/* Stream one exact compressed range through STORE or raw DEFLATE and CRC32. */
int zget_zip_extract_payload(struct zget_zip_format *zip,
                             const struct zget_zip_entry *entry,
                             uint64_t data_offset,
                             zget_write_cb cb, void *userdata)
{
    struct extractor x;
    int rc;
    memset(&x, 0, sizeof(x));
    x.zip = zip;
    x.entry = entry;
    x.output = cb;
    x.userdata = userdata;
    x.crc = crc32(0L, Z_NULL, 0);

    if (entry->compression_method == 8) {
        /* ZIP stores raw DEFLATE blocks with no zlib or gzip wrapper/trailer. */
        if (inflateInit2(&x.stream, -MAX_WBITS) != Z_OK) {
            zget_error_set(zip->format.error, ZGET_ECOMPRESSION,
                           "could not initialize zlib");
            return ZGET_ECOMPRESSION;
        }
        x.inflate_initialized = true;
    }
    /*
     * Fetch exactly the compressed size recorded in the Central Directory.
     * This intentionally stops before any data descriptor following the member.
     */
    if (entry->compressed_size != 0) {
        rc = zget_source_read_range(zip->format.source, data_offset,
                                    entry->compressed_size,
                                    entry->compression_method == 0 ?
                                    store_write : deflate_write, &x);
        if (rc != ZGET_OK)
            goto done;
    }
    /* A fully consumed source range is not proof that DEFLATE reached its end. */
    if (entry->compression_method == 8 && !x.stream_end) {
        zget_error_set(zip->format.error, ZGET_ECOMPRESSION,
                       "truncated DEFLATE stream");
        rc = ZGET_ECOMPRESSION;
        goto done;
    }
    if (x.produced != entry->uncompressed_size) {
        zget_error_set(zip->format.error, ZGET_EZIP,
                       "uncompressed size does not match ZIP metadata");
        rc = ZGET_EZIP;
        goto done;
    }
    /* CRC is over uncompressed output, exactly as recorded by the ZIP entry. */
    if ((uint32_t)x.crc != entry->crc32) {
        zget_error_set(zip->format.error, ZGET_ECRC, "CRC32 mismatch");
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
