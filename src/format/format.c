#include "format/format.h"
#include "format/zip/zip.h"

int zget_format_open(struct zget_source *source,
                     struct zget_error_state *error,
                     struct zget_format **out_format)
{
    if (out_format == NULL)
        return ZGET_EINVAL;
    *out_format = NULL;
    if (source == NULL || error == NULL)
        return ZGET_EINVAL;

    /*
     * ZIP is the sole supported container, so selection is direct. Keeping this
     * narrow façade gives public orchestration a format-neutral vocabulary
     * without introducing a registry, vtable, or caller-visible extension API.
     */
    return zget_zip_format_open(source, error, out_format);
}

int zget_format_extract_member(struct zget_format *format, const char *member,
                               zget_write_cb write_cb, void *userdata)
{
    if (format == NULL || member == NULL || write_cb == NULL)
        return ZGET_EINVAL;
    return zget_zip_format_extract_member(format, member, write_cb, userdata);
}

int zget_format_list(struct zget_format *format, zget_list_cb list_cb,
                     void *userdata)
{
    if (format == NULL || list_cb == NULL)
        return ZGET_EINVAL;
    return zget_zip_format_list(format, list_cb, userdata);
}

void zget_format_close(struct zget_format *format)
{
    /* zget_format_open() is the sole constructor and always creates a ZIP object. */
    if (format != NULL)
        zget_zip_format_close(format);
}
