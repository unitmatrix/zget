#include "internal.h"
#include "format/format.h"
#include "source/http.h"
#include "source/source.h"

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

int zget_open_url_ex(const char *archive_url, const zget_options *options,
                     zget_ctx **out_ctx)
{
    struct zget_ctx *ctx;
    struct zget_format_options format_options;
    struct zget_http_options http_options;
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
     * Public orchestration selects a source, then delegates all container
     * discovery to the format layer. This is the only point where those two
     * independently testable halves are joined.
     */
    format_options.max_metadata_bytes = ctx->options.max_metadata_bytes;
    format_options.max_output_size = ctx->options.max_output_size;
    if ((rc = zget_format_open(ctx->source, &format_options, &ctx->error,
                               &ctx->format)) != ZGET_OK)
        goto fail;
    ctx->ready = true;
    *out_ctx = ctx;
    return ZGET_OK;
fail:
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

int zget_extract_member(zget_ctx *ctx, const char *member_path,
                        zget_write_cb write_cb, void *userdata)
{
    if (ctx == NULL || member_path == NULL || write_cb == NULL)
        return ZGET_EINVAL;
    if (!ctx->ready)
        return ctx->error.code != ZGET_OK ? ctx->error.code : ZGET_EINVAL;
    ctx->error.code = ZGET_OK;
    ctx->error.message[0] = '\0';
    return zget_format_extract_member(ctx->format, member_path,
                                      write_cb, userdata);
}

int zget_list(zget_ctx *ctx, const char *member_path,
              zget_list_cb list_cb, void *userdata)
{
    if (ctx == NULL || list_cb == NULL)
        return ZGET_EINVAL;
    if (!ctx->ready)
        return ctx->error.code != ZGET_OK ? ctx->error.code : ZGET_EINVAL;
    ctx->error.code = ZGET_OK;
    ctx->error.message[0] = '\0';
    return zget_format_list(ctx->format, member_path, list_cb, userdata);
}

int zget_find(zget_ctx *ctx, const char *member_path, zget_entry *entry)
{
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
    return zget_format_find(ctx->format, member_path, entry);
}

int zget_extract(zget_ctx *ctx, const zget_entry *entry,
                 zget_write_cb write_cb, void *userdata)
{
    if (ctx == NULL || entry == NULL || write_cb == NULL)
        return ZGET_EINVAL;
    if (!ctx->ready)
        return ctx->error.code != ZGET_OK ? ctx->error.code : ZGET_EINVAL;
    ctx->error.code = ZGET_OK;
    ctx->error.message[0] = '\0';
    return zget_format_extract(ctx->format, entry, write_cb, userdata);
}

int zget_get(const char *archive_url, const char *member_path,
             zget_write_cb write_cb, void *userdata)
{
    zget_ctx *ctx = NULL;
    int rc;
    /* Reject invalid convenience-API arguments before they can cause I/O. */
    if (archive_url == NULL || archive_url[0] == '\0' || member_path == NULL ||
        member_path[0] == '\0' || write_cb == NULL)
        return ZGET_EINVAL;
    rc = zget_open_url_ex(archive_url, NULL, &ctx);
    if (rc == ZGET_OK)
        rc = zget_extract_member(ctx, member_path, write_cb, userdata);
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
    zget_format_close(ctx->format);
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
