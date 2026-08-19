#ifndef ZGET_SOURCE_H
#define ZGET_SOURCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct zget_source;

/*
 * Range consumers distinguish intentional early completion from failure.
 * Formats can therefore stop a metadata stream without teaching transports
 * what was found or relying on a magic callback return value.
 */
enum zget_source_action {
    ZGET_SOURCE_CONTINUE = 0,
    ZGET_SOURCE_STOP,
    ZGET_SOURCE_ERROR
};

typedef enum zget_source_action (*zget_source_write_cb)(
    void *userdata, const void *data, size_t size);

int zget_source_read_range(struct zget_source *source, uint64_t offset,
                           uint64_t length, zget_source_write_cb write_cb,
                           void *userdata);
int zget_source_read_suffix(struct zget_source *source, uint64_t length,
                            zget_source_write_cb write_cb, void *userdata);
bool zget_source_get_size(const struct zget_source *source, uint64_t *size);
void zget_source_close(struct zget_source *source);

#endif
