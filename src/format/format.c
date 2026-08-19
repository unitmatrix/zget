#include "format/format-private.h"
#include "format/zip/zip.h"

void zget_format_init(struct zget_format *format,
                      const struct zget_format_ops *ops,
                      struct zget_source *source,
                      const struct zget_format_options *options,
                      struct zget_error_state *error)
{
    format->ops = ops;
    format->source = source;
    format->error = error;
    format->options = *options;
}

int zget_format_open(struct zget_source *source,
                     const struct zget_format_options *options,
                     struct zget_error_state *error,
                     struct zget_format **out_format)
{
    if (out_format == NULL)
        return ZGET_EINVAL;
    *out_format = NULL;
    if (source == NULL || options == NULL || error == NULL)
        return ZGET_EINVAL;

    /*
     * ZIP is currently the sole engine, so selection is deliberately direct.
     * A future probe dispatcher belongs here; sources and public orchestration
     * will not need to change when another range-friendly format is admitted.
     */
    return zget_zip_format_open(source, options, error, out_format);
}

int zget_format_find(struct zget_format *format, const char *member,
                     zget_entry *entry)
{
    if (format == NULL || format->ops == NULL || format->ops->find == NULL ||
        member == NULL || entry == NULL)
        return ZGET_EINVAL;
    return format->ops->find(format, member, entry);
}

int zget_format_extract(struct zget_format *format, const zget_entry *entry,
                        zget_write_cb write_cb, void *userdata)
{
    if (format == NULL || format->ops == NULL ||
        format->ops->extract == NULL || entry == NULL || write_cb == NULL)
        return ZGET_EINVAL;
    return format->ops->extract(format, entry, write_cb, userdata);
}

void zget_format_close(struct zget_format *format)
{
    if (format != NULL && format->ops != NULL && format->ops->close != NULL)
        format->ops->close(format);
}
