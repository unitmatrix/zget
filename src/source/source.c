#include "source/source-private.h"

#include "zget.h"

void zget_source_init(struct zget_source *source,
                      const struct zget_source_ops *ops,
                      struct zget_error_state *error)
{
    source->ops = ops;
    source->error = error;
    source->size = 0;
    source->size_known = false;
}

int zget_source_read_range(struct zget_source *source, uint64_t offset,
                           uint64_t length, zget_source_write_cb write_cb,
                           void *userdata)
{
    if (source == NULL || source->ops == NULL ||
        source->ops->read_range == NULL || write_cb == NULL || length == 0)
        return ZGET_EINVAL;
    /*
     * Once a backend has established the object size, reject invalid requests
     * before they reach transport code. Formats still validate hostile offsets
     * themselves so they can report a format-specific diagnostic.
     */
    if (source->size_known &&
        (offset > source->size || length > source->size - offset)) {
        zget_error_set(source->error, ZGET_EINVAL,
                       "range lies outside the source object");
        return ZGET_EINVAL;
    }
    return source->ops->read_range(source, offset, length, write_cb, userdata);
}

int zget_source_read_suffix(struct zget_source *source, uint64_t length,
                            zget_source_write_cb write_cb, void *userdata)
{
    if (source == NULL || source->ops == NULL ||
        source->ops->read_suffix == NULL || write_cb == NULL || length == 0)
        return ZGET_EINVAL;
    return source->ops->read_suffix(source, length, write_cb, userdata);
}

bool zget_source_get_size(const struct zget_source *source, uint64_t *size)
{
    if (source == NULL || size == NULL || !source->size_known)
        return false;
    *size = source->size;
    return true;
}

void zget_source_close(struct zget_source *source)
{
    if (source != NULL && source->ops != NULL && source->ops->close != NULL)
        source->ops->close(source);
}
