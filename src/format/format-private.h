#ifndef ZGET_FORMAT_PRIVATE_H
#define ZGET_FORMAT_PRIVATE_H

#include "format/format.h"

/*
 * The ZIP implementation embeds this header as its first member. The source
 * and error state are borrowed from the active operation and outlive the format
 * object. Keeping ownership at the operation makes teardown order explicit.
 */
struct zget_format {
    struct zget_source *source;
    struct zget_error_state *error;
};

#endif
