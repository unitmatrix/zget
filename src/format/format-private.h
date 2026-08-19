#ifndef ZGET_FORMAT_PRIVATE_H
#define ZGET_FORMAT_PRIVATE_H

#include "format/format.h"

struct zget_format_ops {
    /* Core operation: no format-specific locator crosses this boundary. */
    int (*extract_member)(struct zget_format *format, const char *member,
                          zget_write_cb write_cb, void *userdata);

    /*
     * These hooks preserve the v0.1 ZIP metadata API. New format engines need
     * not encode their locators into zget_entry to implement extract_member.
     */
    int (*find)(struct zget_format *format, const char *member,
                zget_entry *entry);
    int (*extract)(struct zget_format *format, const zget_entry *entry,
                   zget_write_cb write_cb, void *userdata);
    void (*close)(struct zget_format *format);
};

/*
 * Engines embed this header as their first member. The source and error state
 * are borrowed from zget_ctx and therefore outlive the format object. Keeping
 * ownership at the context avoids cycles and makes teardown order obvious.
 */
struct zget_format {
    const struct zget_format_ops *ops;
    struct zget_source *source;
    struct zget_error_state *error;
    struct zget_format_options options;
};

void zget_format_init(struct zget_format *format,
                      const struct zget_format_ops *ops,
                      struct zget_source *source,
                      const struct zget_format_options *options,
                      struct zget_error_state *error);

#endif
