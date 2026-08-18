#include "internal.h"

#include <stdlib.h>
#include <string.h>

struct tail_buffer {
    unsigned char *data;
    size_t capacity;
    size_t length;
};

static int tail_write(void *opaque, const void *data, size_t length)
{
    struct tail_buffer *tail = opaque;
    if (length > tail->capacity - tail->length)
        return 1;
    memcpy(tail->data + tail->length, data, length);
    tail->length += length;
    return 0;
}

void zget_options_init(zget_options *options)
{
    if (options == NULL)
        return;
    memset(options, 0, sizeof(*options));
    options->struct_size = sizeof(*options);
    options->max_metadata_bytes = ZGET_DEFAULT_METADATA;
    options->max_redirects = 8;
}

static int copy_options(struct zget_ctx *ctx, const zget_options *options)
{
    zget_options_init(&ctx->options);
    if (options == NULL)
        return ZGET_OK;
    if (options->struct_size < sizeof(*options)) {
        zget_set_error(ctx, ZGET_EINVAL, "zget_options struct is too small");
        return ZGET_EINVAL;
    }
    ctx->options = *options;
    if (ctx->options.max_metadata_bytes == 0)
        ctx->options.max_metadata_bytes = ZGET_DEFAULT_METADATA;
    if (ctx->options.max_redirects == 0)
        ctx->options.max_redirects = 8;
    if (ctx->options.max_redirects > 50) {
        zget_set_error(ctx, ZGET_EINVAL, "max_redirects must not exceed 50");
        return ZGET_EINVAL;
    }
    return ZGET_OK;
}

/* Fetch and resolve only the archive tail; member lookup remains a later range. */
int zget_open_url_ex(const char *archive_url, const zget_options *options,
                     zget_ctx **out_ctx)
{
    struct zget_ctx *ctx;
    struct tail_buffer tail = {0};
    uint64_t request_size, tail_offset;
    int rc;
    if (out_ctx == NULL || archive_url == NULL || archive_url[0] == '\0')
        return ZGET_EINVAL;
    *out_ctx = NULL;
    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
        return ZGET_ENOMEM;
    ctx->url = zget_strdup(archive_url);
    if (ctx->url == NULL) {
        rc = ZGET_ENOMEM;
        goto fail;
    }
    if ((rc = copy_options(ctx, options)) != ZGET_OK)
        goto fail;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        zget_set_error(ctx, ZGET_EHTTP, "could not initialize libcurl");
        rc = ZGET_EHTTP;
        goto fail;
    }
    if ((rc = zget_http_init(ctx)) != ZGET_OK)
        goto fail;
    /* ZIP consumes ranges through the source abstraction, never through curl. */
    ctx->source.ctx = ctx;
    ctx->source.read_range = zget_http_read;

    /*
     * EOCD is at least 22 bytes and can follow a 65535-byte archive comment.
     * A 128 KiB suffix also normally contains the ZIP64 locator and fixed EOCD,
     * while keeping the first request bounded for very large archives.
     */
    request_size = ZGET_TAIL_SIZE;
    if (ctx->options.max_metadata_bytes < request_size)
        request_size = ctx->options.max_metadata_bytes;
    if (request_size < 22) {
        zget_set_error(ctx, ZGET_ELIMIT, "metadata limit is too small for ZIP EOCD");
        rc = ZGET_ELIMIT;
        goto fail;
    }
    tail.capacity = (size_t)request_size;
    tail.data = malloc(tail.capacity);
    if (tail.data == NULL) {
        rc = ZGET_ENOMEM;
        goto fail;
    }
    if ((rc = zget_http_read_suffix(ctx, request_size, tail_write, &tail)) != ZGET_OK)
        goto fail;
    tail_offset = ctx->archive_size - tail.length;
    if ((rc = zget_parse_tail(ctx, tail.data, tail.length, tail_offset)) != ZGET_OK)
        goto fail;
    ctx->ready = true;
    free(tail.data);
    *out_ctx = ctx;
    return ZGET_OK;
fail:
    free(tail.data);
    if (ctx->error == ZGET_OK)
        zget_set_error(ctx, rc, "%s", zget_error_string(rc));
    /*
     * Unlike zget_open_url(), the explicit API transfers this failed context to
     * the caller so the detailed diagnostic survives. zget_close() accepts it.
     */
    *out_ctx = ctx;
    return rc;
}

zget_ctx *zget_open_url(const char *archive_url, const zget_options *options)
{
    zget_ctx *ctx = NULL;
    if (zget_open_url_ex(archive_url, options, &ctx) != ZGET_OK) {
        zget_close(ctx);
        return NULL;
    }
    return ctx;
}

int zget_find(zget_ctx *ctx, const char *member_path, zget_entry *entry)
{
    size_t length;
    if (ctx == NULL || member_path == NULL || entry == NULL)
        return ZGET_EINVAL;
    if (!ctx->ready)
        return ctx->error != ZGET_OK ? ctx->error : ZGET_EINVAL;
    ctx->error = ZGET_OK;
    ctx->message[0] = '\0';
    length = strlen(member_path);
    /* Member names are archive identifiers: compare exact bytes, without path normalization. */
    if (length == 0 || length > UINT16_MAX ||
        !zget_valid_utf8((const unsigned char *)member_path, length)) {
        zget_set_error(ctx, ZGET_EINVAL,
                       "member path must be non-empty, valid UTF-8, and at most 65535 bytes");
        return ZGET_EINVAL;
    }
    memset(entry, 0, sizeof(*entry));
    return zget_find_in_cd(ctx, member_path, entry);
}

int zget_extract(zget_ctx *ctx, const zget_entry *entry,
                 zget_write_cb write_cb, void *userdata)
{
    uint64_t data_offset;
    int rc;
    if (ctx == NULL || entry == NULL || write_cb == NULL)
        return ZGET_EINVAL;
    if (!ctx->ready)
        return ctx->error != ZGET_OK ? ctx->error : ZGET_EINVAL;
    ctx->error = ZGET_OK;
    ctx->message[0] = '\0';
    /* zget_entry is public, so reapply the hard codec/encryption whitelist here. */
    if ((entry->flags & 0x0041u) != 0) {
        zget_set_error(ctx, ZGET_EUNSUPPORTED, "encrypted ZIP entries are unsupported");
        return ZGET_EUNSUPPORTED;
    }
    if (entry->compression_method != 0 && entry->compression_method != 8) {
        zget_set_error(ctx, ZGET_ECOMPRESSION, "unsupported ZIP compression method");
        return ZGET_ECOMPRESSION;
    }
    rc = zget_read_local_header(ctx, entry, &data_offset);
    if (rc != ZGET_OK)
        return rc;
    return zget_extract_payload(ctx, entry, data_offset, write_cb, userdata);
}

int zget_get(const char *archive_url, const char *member_path,
             zget_write_cb write_cb, void *userdata)
{
    zget_ctx *ctx = NULL;
    zget_entry entry;
    int rc;
    /* Reject invalid convenience-API arguments before they can cause I/O. */
    if (archive_url == NULL || archive_url[0] == '\0' || member_path == NULL ||
        member_path[0] == '\0' || write_cb == NULL)
        return ZGET_EINVAL;
    rc = zget_open_url_ex(archive_url, NULL, &ctx);
    if (rc == ZGET_OK)
        rc = zget_find(ctx, member_path, &entry);
    if (rc == ZGET_OK)
        rc = zget_extract(ctx, &entry, write_cb, userdata);
    zget_close(ctx);
    return rc;
}

void zget_close(zget_ctx *ctx)
{
    /* All context-owned allocations, including failed-open state, end here. */
    if (ctx == NULL)
        return;
    if (ctx->curl != NULL)
        curl_easy_cleanup(ctx->curl);
    /*
     * Do not call curl_global_cleanup() here. libcurl's global state belongs
     * to the process, not to one zget context. A caller may have several
     * contexts alive at once, and closing any one of them must not tear down
     * state still needed by the others. The process will reclaim this small
     * global allocation set on exit; avoiding cleanup is also the portable way
     * to keep independent contexts safe without imposing a threading API on
     * this C99 library.
     */
    free(ctx->url);
    free(ctx->effective_url);
    free(ctx->strong_etag);
    free(ctx);
}

int zget_last_error(const zget_ctx *ctx)
{
    return ctx == NULL ? ZGET_EINVAL : ctx->error;
}

const char *zget_last_error_message(const zget_ctx *ctx)
{
    return ctx == NULL ? "invalid zget context" : ctx->message;
}

const char *zget_error_string(int error)
{
    static const char *const messages[] = {
        "success", "invalid argument", "HTTP error", "HTTP Range unsupported",
        "remote object changed", "malformed ZIP", "unsupported ZIP feature",
        "member not found", "unsupported compression", "CRC32 mismatch",
        "decompression error", "output I/O error", "resource limit exceeded",
        "out of memory"
    };
    return error >= 0 && (size_t)error < sizeof(messages) / sizeof(messages[0]) ?
           messages[error] : "unknown zget error";
}

const char *zget_version(void)
{
    return "0.1.0";
}
