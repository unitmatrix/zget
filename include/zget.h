#ifndef ZGET_H
#define ZGET_H

#include <stddef.h>
#include <stdint.h>
#include "zget_version.h"

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
    ZGET_ENOMEM,
    ZGET_ENOTINITIALIZED
} zget_error;

/*
 * Output is borrowed for the duration of the call and may be reused as soon as
 * the callback returns. Return zero after consuming all bytes; any other value
 * aborts extraction and is reported as an output error.
 */
typedef int (*zget_write_cb)(void *userdata, const void *data, size_t size);

/* The member name is known to be valid UTF-8, rather than legacy raw bytes. */
#define ZGET_MEMBER_NAME_UTF8 0x00000001u
/* The local calendar components below contain a validated modification time. */
#define ZGET_MEMBER_HAS_MODIFIED_TIME 0x00000002u

/*
 * One format-neutral member record produced during a streaming listing.
 * struct_size records the bytes supplied by the running library so fields can
 * be appended compatibly. name is borrowed, is not necessarily NUL-terminated,
 * and remains valid only until the callback returns. When the UTF-8 flag is
 * absent, consumers should preserve or escape the bytes instead of guessing a
 * legacy encoding. Unknown flag bits must be ignored.
 */
typedef struct zget_member_info {
    size_t struct_size;
    const char *name;
    size_t name_length;
    uint64_t uncompressed_size;
    uint32_t flags;
    uint16_t modified_year;
    uint8_t modified_month;
    uint8_t modified_day;
    uint8_t modified_hour;
    uint8_t modified_minute;
    uint8_t modified_second;
} zget_member_info;

/* Frozen boundary of the fields published with the listing API. */
#define ZGET_MEMBER_INFO_V1_SIZE \
    (offsetof(zget_member_info, modified_second) + sizeof(uint8_t))

/* Return zero to continue listing; any other value aborts with ZGET_EIO. */
typedef int (*zget_list_cb)(void *userdata, const zget_member_info *member);

/*
 * This structure is append-only. zget_options_init() records the caller's
 * known size so newer libraries can preserve defaults for fields that were not
 * present when the caller was compiled.
 */
typedef struct zget_options {
    size_t struct_size;
    uint64_t max_output_size;    /* zero means unlimited */
    uint64_t max_metadata_bytes; /* zero selects a safe default */
    uint32_t max_http_requests;  /* zero means unlimited */
    uint32_t max_redirects;      /* zero selects 8 */
} zget_options;

/* Frozen boundary of the fields published in the original options ABI. */
#define ZGET_OPTIONS_V1_SIZE \
    (offsetof(zget_options, max_redirects) + sizeof(uint32_t))

/*
 * ZIP extraction metadata retained for v0.1 source and binary compatibility.
 * New code that only needs a named member should use zget_extract_member().
 * Callers using this structure must pass it back unchanged.
 */
typedef struct zget_entry {
    uint64_t compressed_size;
    uint64_t uncompressed_size;
    uint64_t local_header_offset;
    uint32_t crc32;
    uint16_t compression_method;
    uint16_t flags;
} zget_entry;

/*
 * Acquire zget's process-wide HTTP backend during single-threaded application
 * startup. Every successful call must be matched by zget_global_cleanup()
 * after all zget contexts have been closed and application threads using zget
 * have stopped. Multiple acquisitions are supported and reference-counted.
 *
 * The lifecycle is deliberately explicit: libcurl's global state belongs to
 * the process, not to an individual zget_ctx, and some libcurl builds require
 * initialization before any additional threads are started.
 */
ZGET_API int zget_global_init(void);
ZGET_API void zget_global_cleanup(void);

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
 * zget_close(). When no diagnostic context is available, *out_ctx is set to
 * NULL rather than retaining its previous value.
 */
ZGET_API int zget_open_url_ex(const char *archive_url,
                              const zget_options *options,
                              zget_ctx **out_ctx);

/*
 * Locate one exact member through the selected format engine and stream its
 * validated contents. Reusing a context avoids reopening the remote object;
 * each call still performs the format's metadata lookup for that member.
 */
ZGET_API int zget_extract_member(zget_ctx *ctx, const char *member_path,
                                 zget_write_cb write_cb, void *userdata);

/*
 * Stream member metadata through the selected format engine. A NULL
 * member_path lists every member; a non-NULL path emits the first exact match
 * and stops the metadata transfer. Only one member record is live at a time.
 */
ZGET_API int zget_list(zget_ctx *ctx, const char *member_path,
                       zget_list_cb list_cb, void *userdata);

/*
 * v0.1 ZIP compatibility API. entry receives the ZIP metadata needed to fetch
 * and validate one member. It is cleared before validation and remains zeroed
 * when lookup fails.
 */
ZGET_API int zget_find(zget_ctx *ctx, const char *member_path,
                       zget_entry *entry);

/* Stream a ZIP entry returned by zget_find(); no archive-sized buffer is used. */
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
