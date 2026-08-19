#ifndef ZGET_SOURCE_PRIVATE_H
#define ZGET_SOURCE_PRIVATE_H

#include "error.h"
#include "source/source.h"

struct zget_source_ops {
    int (*read_range)(struct zget_source *source, uint64_t offset,
                      uint64_t length, zget_source_write_cb write_cb,
                      void *userdata);
    int (*read_suffix)(struct zget_source *source, uint64_t length,
                       zget_source_write_cb write_cb, void *userdata);
    void (*close)(struct zget_source *source);
};

/*
 * Backends embed this header as their first member. Only source code sees the
 * operations table and mutable size; formats receive an opaque pointer.
 */
struct zget_source {
    const struct zget_source_ops *ops;
    struct zget_error_state *error;
    uint64_t size;
    bool size_known;
};

void zget_source_init(struct zget_source *source,
                      const struct zget_source_ops *ops,
                      struct zget_error_state *error);

#endif
