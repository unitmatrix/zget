#include "internal.h"
#include "source/http.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/*
 * Global initialization is owned by the embedding application rather than by
 * individual contexts. Keep our count aligned with libcurl's own acquisition
 * count so zget composes safely with other libraries that also use libcurl.
 *
 * This counter intentionally needs no lock: the public contract requires init
 * and cleanup during single-threaded startup and shutdown. Between those
 * phases it is read-only, so distinct contexts can be used by distinct threads.
 */
static unsigned int global_references;

int zget_global_init(void)
{
    int rc;

    if (global_references == UINT_MAX)
        return ZGET_ELIMIT;
    rc = zget_http_global_init();
    if (rc != ZGET_OK)
        return rc;
    ++global_references;
    return ZGET_OK;
}

void zget_global_cleanup(void)
{
    if (global_references == 0)
        return;
    --global_references;
    zget_http_global_cleanup();
}

struct tail_buffer {
    unsigned char *data;
    size_t capacity;
    size_t length;
};

static enum zget_source_action tail_write(void *opaque, const void *data,
                                          size_t length)
{
    struct tail_buffer *tail = opaque;
    if (length > tail->capacity - tail->length)
        return ZGET_SOURCE_ERROR;
    memcpy(tail->data + tail->length, data, length);
    tail->length += length;
    return ZGET_SOURCE_CONTINUE;
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
    size_t copy_size;

    /* Defaults survive for every field beyond the caller's struct_size. */
    zget_options_init(&ctx->options);
    if (options == NULL)
        return ZGET_OK;
    if (options->struct_size < ZGET_OPTIONS_V1_SIZE) {
        zget_error_set(&ctx->error, ZGET_EINVAL,
                       "zget_options struct is too small");
        return ZGET_EINVAL;
    }
    copy_size = options->struct_size;
    if (copy_size > sizeof(ctx->options))
        copy_size = sizeof(ctx->options);
    memcpy(&ctx->options, options, copy_size);

    /* Internal code always sees the library's complete, normalized structure. */
    ctx->options.struct_size = sizeof(ctx->options);
    if (ctx->options.max_metadata_bytes == 0)
        ctx->options.max_metadata_bytes = ZGET_DEFAULT_METADATA;
    if (ctx->options.max_redirects == 0)
        ctx->options.max_redirects = 8;
    if (ctx->options.max_redirects > 50) {
        zget_error_set(&ctx->error, ZGET_EINVAL,
                       "max_redirects must not exceed 50");
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
    struct zget_http_options http_options;
    uint64_t request_size, source_size, tail_offset;
    int rc;
    if (out_ctx == NULL)
        return ZGET_EINVAL;
    /*
     * Establish a safe ownership state before validating anything else. A
     * caller can therefore unconditionally close *out_ctx after any failure
     * without accidentally reusing the pointer that it supplied.
     */
    *out_ctx = NULL;
    if (archive_url == NULL || archive_url[0] == '\0')
        return ZGET_EINVAL;
    if (global_references == 0)
        return ZGET_ENOTINITIALIZED;
    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL)
        return ZGET_ENOMEM;
    if ((rc = copy_options(ctx, options)) != ZGET_OK)
        goto fail;
    http_options.max_requests = ctx->options.max_http_requests;
    http_options.max_redirects = ctx->options.max_redirects;
    if ((rc = zget_http_source_open(archive_url, &http_options, &ctx->error,
                                    &ctx->source)) != ZGET_OK)
        goto fail;

    /*
     * EOCD is at least 22 bytes and can follow a 65535-byte archive comment.
     * A 128 KiB suffix also normally contains the ZIP64 locator and fixed EOCD,
     * while keeping the first request bounded for very large archives.
     */
    request_size = ZGET_TAIL_SIZE;
    if (ctx->options.max_metadata_bytes < request_size)
        request_size = ctx->options.max_metadata_bytes;
    if (request_size < 22) {
        zget_error_set(&ctx->error, ZGET_ELIMIT,
                       "metadata limit is too small for ZIP EOCD");
        rc = ZGET_ELIMIT;
        goto fail;
    }
    tail.capacity = (size_t)request_size;
    tail.data = malloc(tail.capacity);
    if (tail.data == NULL) {
        rc = ZGET_ENOMEM;
        goto fail;
    }
    if ((rc = zget_source_read_suffix(ctx->source, request_size,
                                      tail_write, &tail)) != ZGET_OK)
        goto fail;
    if (!zget_source_get_size(ctx->source, &source_size) ||
        tail.length > source_size) {
        zget_error_set(&ctx->error, ZGET_EHTTP,
                       "source size unavailable after suffix request");
        rc = ZGET_EHTTP;
        goto fail;
    }
    tail_offset = source_size - tail.length;
    if ((rc = zget_parse_tail(ctx, tail.data, tail.length, tail_offset)) != ZGET_OK)
        goto fail;
    ctx->ready = true;
    free(tail.data);
    *out_ctx = ctx;
    return ZGET_OK;
fail:
    free(tail.data);
    if (ctx->error.code == ZGET_OK)
        zget_error_set(&ctx->error, rc, "%s", zget_error_string(rc));
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
    /*
     * Failed lookups must not leave metadata from an earlier successful call
     * looking usable. Clear the output before validating the context or name.
     */
    if (entry != NULL)
        memset(entry, 0, sizeof(*entry));
    if (ctx == NULL || member_path == NULL || entry == NULL)
        return ZGET_EINVAL;
    if (!ctx->ready)
        return ctx->error.code != ZGET_OK ? ctx->error.code : ZGET_EINVAL;
    ctx->error.code = ZGET_OK;
    ctx->error.message[0] = '\0';
    length = strlen(member_path);
    /* Member names are archive identifiers: compare exact bytes, without path normalization. */
    if (length == 0 || length > UINT16_MAX ||
        !zget_valid_utf8((const unsigned char *)member_path, length)) {
        zget_error_set(&ctx->error, ZGET_EINVAL,
                       "member path must be non-empty, valid UTF-8, and at most 65535 bytes");
        return ZGET_EINVAL;
    }
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
        return ctx->error.code != ZGET_OK ? ctx->error.code : ZGET_EINVAL;
    ctx->error.code = ZGET_OK;
    ctx->error.message[0] = '\0';
    /* zget_entry is public, so reapply the hard codec/encryption whitelist here. */
    if ((entry->flags & 0x0041u) != 0) {
        zget_error_set(&ctx->error, ZGET_EUNSUPPORTED,
                       "encrypted ZIP entries are unsupported");
        return ZGET_EUNSUPPORTED;
    }
    if (entry->compression_method != 0 && entry->compression_method != 8) {
        zget_error_set(&ctx->error, ZGET_ECOMPRESSION,
                       "unsupported ZIP compression method");
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
    /*
     * Only context-owned state ends here. Process-wide libcurl state is paired
     * by zget_global_init()/zget_global_cleanup(), never by a context destructor.
     */
    if (ctx == NULL)
        return;
    zget_source_close(ctx->source);
    free(ctx);
}

int zget_last_error(const zget_ctx *ctx)
{
    return ctx == NULL ? ZGET_EINVAL : ctx->error.code;
}

const char *zget_last_error_message(const zget_ctx *ctx)
{
    return ctx == NULL ? "invalid zget context" : ctx->error.message;
}

const char *zget_error_string(int error)
{
    static const char *const messages[] = {
        "success", "invalid argument", "HTTP error", "HTTP Range unsupported",
        "remote object changed", "malformed ZIP", "unsupported ZIP feature",
        "member not found", "unsupported compression", "CRC32 mismatch",
        "decompression error", "output I/O error", "resource limit exceeded",
        "out of memory", "zget is not initialized"
    };
    return error >= 0 && (size_t)error < sizeof(messages) / sizeof(messages[0]) ?
           messages[error] : "unknown zget error";
}

const char *zget_version(void)
{
    return ZGET_VERSION_STRING;
}
