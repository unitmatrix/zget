#ifndef ZGET_H
#define ZGET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(ZGET_SHARED)
# if defined(ZGET_BUILDING)
#  define ZGET_API __declspec(dllexport)
# else
#  define ZGET_API __declspec(dllimport)
# endif
#elif defined(__GNUC__) || defined(__clang__)
# define ZGET_API __attribute__((visibility("default")))
#else
# define ZGET_API
#endif

#define ZGET_VERSION_MAJOR 0
#define ZGET_VERSION_MINOR 1
#define ZGET_VERSION_PATCH 0

typedef struct zget_ctx zget_ctx;

typedef enum zget_error {
    ZGET_OK = 0,
    ZGET_EINVAL,
    ZGET_EHTTP,
    ZGET_ERANGE,
    ZGET_ECHANGED,
    ZGET_EZIP,
    ZGET_EUNSUPPORTED,
    ZGET_ENOTFOUND,
    ZGET_ECOMPRESSION,
    ZGET_ECRC,
    ZGET_EDEFLATE,
    ZGET_EIO,
    ZGET_ELIMIT,
    ZGET_ENOMEM
} zget_error;

/*
 * Output is borrowed for the duration of the call and may be reused as soon as
 * the callback returns. Return zero after consuming all bytes; any other value
 * aborts extraction and is reported as an output error.
 */
typedef int (*zget_write_cb)(void *userdata, const void *data, size_t size);

typedef struct zget_options {
    size_t struct_size;
    uint64_t max_output_size;    /* zero means unlimited */
    uint64_t max_metadata_bytes; /* zero selects a safe default */
    uint32_t max_http_requests;  /* zero means unlimited */
    uint32_t max_redirects;      /* zero selects 8 */
} zget_options;

/*
 * Extraction metadata returned by zget_find(). Fields are public for ABI
 * simplicity, but callers should pass the structure back unchanged.
 */
typedef struct zget_entry {
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    uint64_t local_header_offset;
    uint32_t crc32;
    uint16_t compression_method;
    uint16_t flags;
} zget_entry;

/* Initialize all fields, including struct_size, before overriding limits. */
ZGET_API void zget_options_init(zget_options *options);

/*
 * Open and parse the archive tail. The returned context owns all retained URL,
 * validator, and HTTP state and must be released with zget_close(). A context
 * is not safe for concurrent use; distinct contexts are independent.
 */
ZGET_API zget_ctx *zget_open_url(const char *archive_url,
                                 const zget_options *options);

/*
 * Diagnostic form of zget_open_url(). On most failures, *out_ctx retains the
 * detailed error and still belongs to the caller; always pass it to
 * zget_close(). Invalid arguments and allocation failure may leave it NULL.
 */
ZGET_API int zget_open_url_ex(const char *archive_url,
                              const zget_options *options,
                              zget_ctx **out_ctx);

/* entry receives only the metadata needed to fetch and validate one member. */
ZGET_API int zget_find(zget_ctx *ctx, const char *member_path,
                       zget_entry *entry);

/* Stream exactly entry's uncompressed bytes; no archive-sized buffer is used. */
ZGET_API int zget_extract(zget_ctx *ctx, const zget_entry *entry,
                          zget_write_cb write_cb, void *userdata);
ZGET_API int zget_get(const char *archive_url, const char *member_path,
                      zget_write_cb write_cb, void *userdata);
ZGET_API void zget_close(zget_ctx *ctx);
ZGET_API int zget_last_error(const zget_ctx *ctx);
ZGET_API const char *zget_error_string(int error);
ZGET_API const char *zget_last_error_message(const zget_ctx *ctx);
ZGET_API const char *zget_version(void);

#ifdef __cplusplus
}
#endif
#endif
