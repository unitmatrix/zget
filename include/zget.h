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

/** Result codes returned by public operations. */
typedef enum zget_error {
    ZGET_OK = 0,              /**< Operation completed successfully. */
    ZGET_EINVAL = 1,         /**< An argument, URL, or requested range is invalid. */
    ZGET_EHTTP = 2,          /**< HTTP setup, transfer, or response validation failed. */
    ZGET_ERANGE = 3,         /**< The server cannot provide usable byte ranges. */
    ZGET_ECHANGED = 4,       /**< The remote object changed between requests. */
    ZGET_EZIP = 5,           /**< ZIP metadata or member data is malformed. */
    ZGET_EUNSUPPORTED = 6,   /**< A ZIP feature such as encryption is unsupported. */
    ZGET_ENOTFOUND = 7,      /**< No exact member-name match was found. */
    ZGET_ECOMPRESSION = 8,   /**< The compression method or data is unsupported. */
    ZGET_ECRC = 9,           /**< Uncompressed output failed CRC32 validation. */
    ZGET_ECALLBACK = 10,     /**< An output or listing callback rejected data. */
    ZGET_ENOMEM = 11         /**< Memory allocation failed. */
} zget_error;

/**
 * Receives one immutable fragment of uncompressed member data.
 *
 * `userdata` is the value passed to zget_get(). `data` is owned by libzget and
 * remains valid only until the callback returns. Calls are synchronous and may
 * use different buffers. Return zero after consuming all `size` bytes. A
 * nonzero return aborts extraction with ZGET_ECALLBACK.
 */
typedef int (*zget_write_cb)(void *userdata, const void *data, size_t size);

/** One semantic member record produced during a streaming listing. */
typedef struct zget_member_info {
    const char *name;           /**< Borrowed, NUL-terminated valid UTF-8 name. */
    size_t name_length;         /**< UTF-8 byte length, excluding the terminator. */
    uint64_t compressed_size;   /**< Compressed member size in bytes. */
    uint64_t uncompressed_size; /**< Uncompressed member size in bytes. */
    uint32_t crc32;             /**< CRC32 of the uncompressed member data. */
    uint16_t compression_method; /**< Numeric ZIP compression method identifier. */
    int64_t mtime;              /**< Modification time as Unix UTC seconds. */
} zget_member_info;

/**
 * Receives one immutable member record at a time.
 *
 * `userdata` is the value passed to zget_list(). The record and its name become
 * invalid when the callback returns. Return zero to continue listing. A
 * nonzero return stops immediately with ZGET_ECALLBACK.
 */
typedef int (*zget_list_cb)(void *userdata, const zget_member_info *member);

/**
 * Find an exact, case-sensitive UTF-8 member name and stream its contents.
 *
 * Names are matched against the same resolved UTF-8 representation emitted by
 * zget_list(), without path, Unicode, slash, or locale normalization. The first
 * matching Central Directory entry wins. Output accepted before a later size,
 * decompression, or CRC failure cannot be rolled back; only ZGET_OK guarantees
 * complete validated output.
 */
ZGET_API int zget_get(const char *archive_url, const char *member_name,
                      zget_write_cb write_cb, void *userdata);

/**
 * Stream every archive member in Central Directory order.
 *
 * Records and names are borrowed only for their synchronous callback. The
 * operation retains metadata for only one entry at a time.
 */
ZGET_API int zget_list(const char *archive_url, zget_list_cb list_cb,
                       void *userdata);

/**
 * Return an immutable, process-lifetime description for a result code.
 * Unknown integer values return a stable generic description.
 */
ZGET_API const char *zget_error_string(int error);

/** Return the immutable, process-lifetime runtime library version string. */
ZGET_API const char *zget_version(void);

#ifdef __cplusplus
}
#endif
#endif
