#ifndef ZGET_H
#define ZGET_H

#include <stddef.h>
#include <stdint.h>
#include "zget_version.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * libzget is pre-1.0: public API and ABI may change between minor releases.
 * Patch releases preserve the ABI of their minor release line.
 *
 * Global initialization and cleanup must run during single-threaded process
 * startup and shutdown. One context must not be used concurrently or
 * reentrantly; after initialization, separate contexts are independent and may
 * be used by separate threads.
 */

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

/** Opaque handle for one open remote archive. Release it with zget_close(). */
typedef struct zget_ctx zget_ctx;

/** Result codes used by fallible public operations and diagnostics. */
typedef enum zget_error {
    ZGET_OK = 0,                /**< Operation completed successfully. */
    ZGET_EINVAL = 1,           /**< An argument, URL, or requested range is invalid. */
    ZGET_EHTTP = 2,            /**< HTTP setup, transfer, or response validation failed. */
    ZGET_ERANGE = 3,           /**< The server cannot provide usable byte ranges. */
    ZGET_ECHANGED = 4,         /**< The remote object changed between requests. */
    ZGET_EZIP = 5,             /**< ZIP metadata or member data is malformed. */
    ZGET_EUNSUPPORTED = 6,     /**< A ZIP feature such as encryption is unsupported. */
    ZGET_ENOTFOUND = 7,        /**< No exact member-name match was found. */
    ZGET_ECOMPRESSION = 8,     /**< The member's compression method is unsupported. */
    ZGET_ECRC = 9,             /**< Uncompressed output failed CRC32 validation. */
    ZGET_EDEFLATE = 10,        /**< DEFLATE initialization or decoding failed. */
    ZGET_EIO = 11,             /**< An output or listing callback rejected data. */
    ZGET_ELIMIT = 12,          /**< A configured resource limit was exceeded. */
    ZGET_ENOMEM = 13,          /**< Memory allocation failed. */
    ZGET_ENOTINITIALIZED = 14  /**< zget_global_init() has not been acquired. */
} zget_error;

/**
 * Receives one immutable fragment of uncompressed member data.
 *
 * `userdata` is the value passed to the extraction function. `data` is owned by
 * libzget and remains valid only until the callback returns; the callback must
 * copy bytes it needs to retain. Calls are synchronous and may use different
 * buffers. Return zero after consuming all `size` bytes. Returning nonzero
 * aborts extraction with ZGET_EIO.
 */
typedef int (*zget_write_cb)(void *userdata, const void *data, size_t size);

/** `zget_member_info.name` is known to contain valid UTF-8. */
#define ZGET_MEMBER_NAME_UTF8 0x00000001u
/** The modification-time fields contain validated local calendar components. */
#define ZGET_MEMBER_HAS_MODIFIED_TIME 0x00000002u

/**
 * One format-neutral member record produced during a streaming listing.
 *
 * The record and `name` are immutable, owned by libzget, and valid only during
 * the listing callback. `name` is length-delimited and is not necessarily
 * NUL-terminated. When ZGET_MEMBER_NAME_UTF8 is absent, preserve or escape the
 * raw bytes rather than guessing an encoding. Modification fields are zero
 * unless ZGET_MEMBER_HAS_MODIFIED_TIME is set. Ignore unknown flag bits.
 */
typedef struct zget_member_info {
    size_t struct_size;         /**< Bytes supplied by the running library. */
    const char *name;           /**< Borrowed, length-delimited member name. */
    size_t name_length;         /**< Number of bytes in `name`. */
    uint64_t uncompressed_size; /**< Expected uncompressed member size. */
    uint32_t flags;             /**< ZGET_MEMBER_* flags; unknown bits are reserved. */
    uint16_t modified_year;     /**< Local year, when the time flag is set. */
    uint8_t modified_month;     /**< Local month in [1, 12]. */
    uint8_t modified_day;       /**< Local day in [1, 31], validated for month. */
    uint8_t modified_hour;      /**< Local hour in [0, 23]. */
    uint8_t modified_minute;    /**< Local minute in [0, 59]. */
    uint8_t modified_second;    /**< Local second in [0, 59]. */
} zget_member_info;

/** Minimum `struct_size` containing every currently defined member-info field. */
#define ZGET_MEMBER_INFO_V1_SIZE \
    (offsetof(zget_member_info, modified_second) + sizeof(uint8_t))

/**
 * Receives one immutable member record at a time.
 *
 * `userdata` is the value passed to zget_list(). The record and its name become
 * invalid when the callback returns. Return zero to continue listing; returning
 * nonzero stops immediately with ZGET_EIO.
 */
typedef int (*zget_list_cb)(void *userdata, const zget_member_info *member);

/**
 * Limits and HTTP policy copied when an archive is opened.
 *
 * Always call zget_options_init() before overriding fields. `struct_size` lets
 * a newer library preserve defaults for fields unknown to an older caller.
 */
typedef struct zget_options {
    size_t struct_size;          /**< Initialized by zget_options_init(); do not alter. */
    uint64_t max_output_size;    /**< Per extraction; zero means unlimited. */
    uint64_t max_metadata_bytes; /**< ZIP metadata limit; zero selects 8 MiB. */
    uint32_t max_http_requests;  /**< Per context, including open; zero is unlimited. */
    uint32_t max_redirects;      /**< Zero selects 8; values above 50 are invalid. */
} zget_options;

/** Minimum `struct_size` accepted for the original options layout. */
#define ZGET_OPTIONS_V1_SIZE \
    (offsetof(zget_options, max_redirects) + sizeof(uint32_t))

/**
 * Acquire zget's process-wide HTTP backend during single-threaded application
 * startup. Every successful call must be matched by zget_global_cleanup()
 * after all zget contexts have been closed and application threads using zget
 * have stopped. Multiple acquisitions are supported and reference-counted.
 *
 * The lifecycle is deliberately explicit: libcurl's global state belongs to
 * the process, not to an individual zget_ctx, and some libcurl builds require
 * initialization before any additional threads are started.
 *
 * Returns ZGET_OK, ZGET_EHTTP, or ZGET_ELIMIT.
 */
ZGET_API int zget_global_init(void);

/**
 * Release one successful zget_global_init() acquisition.
 *
 * Call this during single-threaded shutdown after closing every context. An
 * unmatched call is harmless, but does not replace correct lifecycle pairing.
 */
ZGET_API void zget_global_cleanup(void);

/**
 * Initialize `options`, including `struct_size`, to documented defaults.
 * Passing NULL is a no-op. The caller retains ownership of the structure.
 */
ZGET_API void zget_options_init(zget_options *options);

/**
 * Open an HTTP(S) archive URL using copied `options` or defaults when NULL.
 *
 * zget_global_init() must already be acquired. On success, returns a new context
 * owned by the caller and eventually released with zget_close(). On failure,
 * returns NULL and discards detailed diagnostics; use zget_open_url_ex() when
 * diagnostics are required. The URL and options need not outlive this call.
 */
ZGET_API zget_ctx *zget_open_url(const char *archive_url,
                                 const zget_options *options);

/**
 * Diagnostic form of zget_open_url().
 *
 * `out_ctx` is required and is set to NULL before other validation. On success,
 * returns ZGET_OK and stores a ready context. On most failures after allocation,
 * it stores a diagnostic context whose error can be inspected; that context is
 * not usable for archive operations. In every case, the caller owns any
 * non-NULL result and must pass it to zget_close(). The URL and options are
 * copied as needed and may be changed or released after this call.
 */
ZGET_API int zget_open_url_ex(const char *archive_url,
                              const zget_options *options,
                              zget_ctx **out_ctx);

/**
 * Find an exact, case-sensitive member name and stream its uncompressed data.
 *
 * `member_path` must be nonempty UTF-8 of at most 65535 bytes. The callback is
 * invoked synchronously and must not reenter the same context. Data already
 * accepted by the callback cannot be rolled back if a later size, DEFLATE, or
 * CRC check fails; only ZGET_OK guarantees complete validated output. Reusing a
 * context avoids reopening the URL but performs a fresh metadata scan. Returns
 * ZGET_OK on success or another zget_error code; use the context diagnostics
 * for details.
 */
ZGET_API int zget_extract_member(zget_ctx *ctx, const char *member_path,
                                 zget_write_cb write_cb, void *userdata);

/**
 * Stream member metadata to `list_cb`.
 *
 * A NULL `member_path` lists all entries. A non-NULL path uses the same exact
 * validation and matching rules as extraction, emits the first match, and
 * returns ZGET_ENOTFOUND if none exists. Records are borrowed only for their
 * synchronous callback. The callback must not reenter the same context. Returns
 * ZGET_OK on success or another zget_error code; use the context diagnostics
 * for details.
 */
ZGET_API int zget_list(zget_ctx *ctx, const char *member_path,
                       zget_list_cb list_cb, void *userdata);

/**
 * Open, extract, and close one member using default options.
 *
 * Global initialization is still required. This convenience call returns a
 * zget_error code but discards detailed context diagnostics. Callback ownership
 * and partial-output rules are identical to zget_extract_member().
 */
ZGET_API int zget_get(const char *archive_url, const char *member_path,
                      zget_write_cb write_cb, void *userdata);

/**
 * Release a successful or diagnostic context. Passing NULL is a no-op. Any
 * pointers borrowed from the context become invalid when this function returns.
 */
ZGET_API void zget_close(zget_ctx *ctx);

/**
 * Return the context's current zget_error code, or ZGET_EINVAL when `ctx` is
 * NULL. A successful context operation resets the stored code to ZGET_OK.
 */
ZGET_API int zget_last_error(const zget_ctx *ctx);

/**
 * Return an immutable, process-lifetime description for a result code.
 * Unknown integer values return a stable generic description. Never free the
 * returned string.
 */
ZGET_API const char *zget_error_string(int error);

/**
 * Return the context's borrowed detailed diagnostic string.
 *
 * The pointer remains valid until the next operation on `ctx` or zget_close(),
 * whichever occurs first, and must not be modified or freed. The string may be
 * empty after success. Passing NULL returns a static diagnostic string.
 */
ZGET_API const char *zget_last_error_message(const zget_ctx *ctx);

/** Return the immutable, process-lifetime runtime library version string. */
ZGET_API const char *zget_version(void);

#ifdef __cplusplus
}
#endif
#endif
