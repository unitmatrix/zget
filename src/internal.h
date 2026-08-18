#ifndef ZGET_INTERNAL_H
#define ZGET_INTERNAL_H

#include "zget.h"

#include <curl/curl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ZGET_DEFAULT_METADATA (8u * 1024u * 1024u)
#define ZGET_TAIL_SIZE 131072u

struct zget_source {
    void *ctx;
    /*
     * ZIP code asks for semantic byte ranges through this narrow interface.
     * It deliberately knows nothing about libcurl, redirects, or headers, so
     * metadata parsers remain testable without emulating a seekable file.
     */
    int (*read_range)(void *ctx, uint64_t offset, uint64_t length,
                      zget_write_cb cb, void *userdata);
};

/*
 * One context owns one reusable curl handle and every retained string below.
 * archive_size and strong_etag are fixed by the first valid range response and
 * constrain all later responses so metadata and payload cannot be mixed across
 * different versions of a remote object.
 */
struct zget_ctx {
    CURL *curl;
    char *url;
    char *effective_url;
    char *strong_etag;
    uint64_t archive_size;
    uint64_t cd_offset;
    uint64_t cd_size;
    uint64_t entry_count;
    uint64_t http_requests;
    bool ready;
    zget_options options;
    struct zget_source source;
    int error;
    char message[256];
};

void zget_set_error(struct zget_ctx *ctx, int error, const char *fmt, ...);
bool zget_u64_add(uint64_t a, uint64_t b, uint64_t *result);
bool zget_range_valid(uint64_t offset, uint64_t length, uint64_t total);
uint16_t zget_le16(const unsigned char *p);
uint32_t zget_le32(const unsigned char *p);
uint64_t zget_le64(const unsigned char *p);
bool zget_valid_utf8(const unsigned char *s, size_t n);
char *zget_strdup(const char *s);

int zget_http_init(struct zget_ctx *ctx);
int zget_http_read(void *opaque, uint64_t offset, uint64_t length,
                   zget_write_cb cb, void *userdata);
int zget_http_read_suffix(struct zget_ctx *ctx, uint64_t length,
                          zget_write_cb cb, void *userdata);

int zget_parse_tail(struct zget_ctx *ctx, const unsigned char *tail,
                    size_t length, uint64_t tail_offset);
int zget_find_in_cd(struct zget_ctx *ctx, const char *member,
                    zget_entry *entry);
int zget_read_local_header(struct zget_ctx *ctx, const zget_entry *entry,
                           uint64_t *data_offset);
int zget_extract_payload(struct zget_ctx *ctx, const zget_entry *entry,
                         uint64_t data_offset, zget_write_cb cb, void *userdata);

#endif
