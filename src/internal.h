#ifndef ZGET_INTERNAL_H
#define ZGET_INTERNAL_H

#include "error.h"
#include "zget.h"

#include <stdbool.h>

#define ZGET_DEFAULT_METADATA (8u * 1024u * 1024u)

struct zget_format;
struct zget_source;

/*
 * The public context coordinates two independently owned internal objects.
 * Format engines borrow the source and shared error state, so close the format
 * first and the source second. No transport- or format-specific state belongs
 * in this public-API orchestration object.
 */
struct zget_ctx {
    struct zget_source *source;
    struct zget_format *format;
    bool ready;
    zget_options options;
    struct zget_error_state error;
};

#endif
