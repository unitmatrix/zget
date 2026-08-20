#ifndef ZGET_FORMAT_PRIVATE_H
#define ZGET_FORMAT_PRIVATE_H

#include "format/format.h"

/*
 * The ZIP implementation embeds this header as its first member. The source
 * and error state are borrowed from zget_ctx and therefore outlive the format
 * object. Keeping ownership at the context makes teardown order explicit.
 */
struct zget_format {
    struct zget_source *source;
    struct zget_error_state *error;
    struct zget_format_options options;
};

#endif
