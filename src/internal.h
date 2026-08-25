#ifndef ZGET_INTERNAL_H
#define ZGET_INTERNAL_H

#include "error.h"

struct zget_format;
struct zget_source;

/* One short-lived archive operation owns one source and format instance. */
struct zget_operation {
    struct zget_source *source;
    struct zget_format *format;
    struct zget_error_state error;
};

#endif
