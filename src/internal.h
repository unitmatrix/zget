#ifndef ZGET_INTERNAL_H
#define ZGET_INTERNAL_H

#include "error.h"
#include "source/source.h"
#include "util.h"
#include "zget.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ZGET_DEFAULT_METADATA (8u * 1024u * 1024u)
#define ZGET_TAIL_SIZE 131072u

/*
 * The public context coordinates one source with format state. Transport-owned
 * details stay behind the opaque source pointer; the ZIP fields remain here
 * only until the format boundary is isolated in the next architectural task.
 */
struct zget_ctx {
    struct zget_source *source;
    uint64_t cd_offset;
    uint64_t cd_size;
    uint64_t entry_count;
    bool ready;
    zget_options options;
    struct zget_error_state error;
};

int zget_parse_tail(struct zget_ctx *ctx, const unsigned char *tail,
                    size_t length, uint64_t tail_offset);
int zget_find_in_cd(struct zget_ctx *ctx, const char *member,
                    zget_entry *entry);
int zget_read_local_header(struct zget_ctx *ctx, const zget_entry *entry,
                           uint64_t *data_offset);
int zget_extract_payload(struct zget_ctx *ctx, const zget_entry *entry,
                         uint64_t data_offset, zget_write_cb cb, void *userdata);

#endif
